#!/usr/bin/env python3
"""Explicit WS147 JFDP/1 provider; no serial endpoint is ever auto-discovered."""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))

from device_image_manifest import DeviceImageManifestError, parse_device_image_manifest


PROVIDER = {"id": "jellyframe-device", "version": "0.1.0-dev"}
FORMAT = "jellyframe.device-provider"
HEADER_BYTES = 24
MAX_PAYLOAD = 4096
RESPONSE_FLAG = 1
DISCOVERY, APP_LIST, INSTALL_BEGIN, INSTALL_CHUNK, INSTALL_COMMIT, INSTALL_ABORT, LAUNCH, STOP, LOGS, RECOVERY, REMOVE, ROLLBACK = range(1, 13)
RESULT_CODES = {
    0: "ok", 1: "accepted", 2: "queued", 3: "invalid-request", 4: "busy",
    5: "unsupported", 6: "denied", 7: "not-found", 8: "stale-session",
    9: "stale-request", 10: "payload-too-large", 11: "integrity-failed",
    12: "storage-full", 13: "cancelled", 14: "failed",
}
APP_STATES = {0: "installed", 1: "disabled", 2: "failed"}
RECOVERY_REASONS = {
    0: "none", 1: "registry-invalid", 2: "staging-discarded", 3: "app-load-failure",
    4: "app-runtime-failure", 5: "app-budget-exceeded", 6: "launcher-fallback",
}


class ProviderError(RuntimeError):
    def __init__(self, code: str, message: str, exit_code: int | None = None):
        super().__init__(message)
        self.code = code
        self.exit_code = exit_code if exit_code is not None else exit_code_for(code)


def exit_code_for(result_code: str) -> int:
    if result_code in {"ok", "accepted"}:
        return 0
    if result_code == "invalid-request":
        return 2
    if result_code == "transport-unavailable":
        return 3
    if result_code == "protocol-mismatch":
        return 4
    if result_code == "provider-failed":
        return 5
    return 1


@dataclass(frozen=True)
class ProviderConfig:
    endpoint_id: str
    port: str
    baud: int
    manifest_path: Path
    manifest: dict[str, Any]


def frame(message_type: int, session: int, request: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ProviderError("invalid-request", "JFDP payload exceeds 4096 bytes")
    return (b"JFDP" + bytes((1, message_type)) + struct.pack("<HIII", 0, session, request, len(payload)) +
            struct.pack("<I", zlib.crc32(payload) & 0xffffffff) + payload)


def decode_frame(value: bytes) -> tuple[int, int, int, bytes]:
    if len(value) < HEADER_BYTES or value[:4] != b"JFDP" or value[4] != 1:
        raise ProviderError("protocol-mismatch", "device returned an invalid JFDP frame")
    flags, session, request, size, checksum = struct.unpack_from("<HIIII", value, 6)
    if size > MAX_PAYLOAD or len(value) != HEADER_BYTES + size:
        raise ProviderError("protocol-mismatch", "device returned an invalid JFDP frame length")
    payload = value[HEADER_BYTES:]
    if zlib.crc32(payload) & 0xffffffff != checksum:
        raise ProviderError("protocol-mismatch", "device returned a JFDP CRC mismatch")
    if flags != RESPONSE_FLAG:
        raise ProviderError("protocol-mismatch", "device response is not marked as a response")
    return value[5], session, request, payload


def app_id_payload(app_id: str) -> bytes:
    value = app_id.encode("utf-8")
    if not value or len(value) > 95 or b"\0" in value:
        raise ProviderError("invalid-request", "app id is invalid")
    return bytes((1, len(value), 0, 0)) + value


def transaction_payload(transaction_id: int) -> bytes:
    return struct.pack("<BBBBI", 1, 0, 0, 0, transaction_id)


def begin_payload(transaction_id: int, app_id: str, bundle: bytes, allow_downgrade: bool) -> bytes:
    app_id_bytes = app_id.encode("utf-8")
    if not app_id_bytes or len(app_id_bytes) > 95:
        raise ProviderError("invalid-request", "bundle app id is invalid")
    return struct.pack("<BBBBIII", 1, int(allow_downgrade), len(app_id_bytes), 0, transaction_id,
                       len(bundle), zlib.crc32(bundle) & 0xffffffff) + app_id_bytes


def chunk_payload(transaction_id: int, offset: int, chunk: bytes) -> bytes:
    if not chunk or len(chunk) > MAX_PAYLOAD - 12:
        raise ProviderError("invalid-request", "bundle chunk is invalid")
    return struct.pack("<BBHII", 1, 0, len(chunk), transaction_id, offset) + chunk


def decode_result(payload: bytes) -> dict[str, Any]:
    if len(payload) != 16 or payload[0] != 1:
        raise ProviderError("protocol-mismatch", "device returned an invalid operation result")
    code, flags, transaction_id, received, expected = struct.unpack_from("<BHIII", payload, 1)
    if code not in RESULT_CODES or received > expected:
        raise ProviderError("protocol-mismatch", "device returned an invalid operation result code")
    return {"resultCode": RESULT_CODES[code], "flags": flags, "transaction": {
        "id": transaction_id, "receivedBytes": received, "expectedBytes": expected,
        "complete": bool(flags & 1), "active": bool(flags & 2),
    }}


def decode_capabilities(payload: bytes) -> dict[str, Any]:
    if len(payload) < 20 or payload[0] != 1:
        raise ProviderError("protocol-mismatch", "device returned invalid capabilities")
    version, board_bytes, runtime_bytes, _reserved, width, height, capability_bits, maximum, available = \
        struct.unpack_from("<BBBBHHIII", payload)
    if version != 1 or not board_bytes or not runtime_bytes or len(payload) != 20 + board_bytes + runtime_bytes:
        raise ProviderError("protocol-mismatch", "device capability shape is invalid")
    start = 20
    try:
        board_id = payload[start:start + board_bytes].decode("utf-8")
        runtime = payload[start + board_bytes:].decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProviderError("protocol-mismatch", "device capability strings are invalid") from error
    return {"boardId": board_id, "runtimeVersion": runtime, "width": width, "height": height,
            "capabilities": capability_bits, "maxBundleBytes": maximum, "availableStorageBytes": available}


def decode_list(payload: bytes) -> tuple[int, list[dict[str, Any]]]:
    if len(payload) < 8 or payload[0] != 1 or payload[2:4] != b"\0\0":
        raise ProviderError("protocol-mismatch", "device returned invalid app list")
    count = payload[1]
    generation = struct.unpack_from("<I", payload, 4)[0]
    cursor = 8
    apps = []
    for _ in range(count):
        if len(payload) - cursor < 12:
            raise ProviderError("protocol-mismatch", "device app list is truncated")
        app_bytes, version_bytes, state, flags, version_code, bundle_bytes = struct.unpack_from("<BBBBII", payload, cursor)
        cursor += 12
        end = cursor + app_bytes + version_bytes
        if not app_bytes or not version_bytes or state not in APP_STATES or end > len(payload):
            raise ProviderError("protocol-mismatch", "device app list entry is invalid")
        try:
            app_id = payload[cursor:cursor + app_bytes].decode("utf-8")
            version_name = payload[cursor + app_bytes:end].decode("utf-8")
        except UnicodeDecodeError as error:
            raise ProviderError("protocol-mismatch", "device app list strings are invalid") from error
        cursor = end
        apps.append({"appId": app_id, "versionName": version_name, "versionCode": version_code,
                     "bundleBytes": bundle_bytes, "state": APP_STATES[state],
                     "rollbackAvailable": bool(flags & 1)})
    if cursor != len(payload) or len(apps) > 24:
        raise ProviderError("protocol-mismatch", "device app list has trailing data")
    return generation, apps


def decode_recovery(payload: bytes) -> dict[str, Any]:
    if len(payload) < 16 or payload[0] != 1 or payload[13:16] != b"\0\0\0":
        raise ProviderError("protocol-mismatch", "device returned invalid recovery detail")
    reason = payload[1]
    flags, generation, sequence = struct.unpack_from("<HII", payload, 2)
    app_bytes = payload[12]
    if reason not in RECOVERY_REASONS or len(payload) != 16 + app_bytes:
        raise ProviderError("protocol-mismatch", "device recovery detail is invalid")
    try:
        app_id = payload[16:].decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProviderError("protocol-mismatch", "device recovery app id is invalid") from error
    return {"appId": app_id, "registryGeneration": generation, "recoverySequence": sequence,
            "reason": RECOVERY_REASONS[reason], "launcherActive": bool(flags & 1),
            "appDisabled": bool(flags & 2), "rollbackAvailable": bool(flags & 4)}


class Wire:
    def __init__(self, config: ProviderConfig):
        self.config = config
        self.serial = None
        self.pending = bytearray()
        self.request_id = 1
        self.session_id = int(time.monotonic_ns() & 0xffffffff) or 1

    def open(self) -> None:
        try:
            import serial
        except ImportError as error:
            raise ProviderError("provider-failed", "pyserial is required by jellyframe-device") from error
        try:
            # COM19 is exposed through the board's USB/UART bridge. Opening a
            # configured port with pyserial's initial DTR/RTS defaults resets
            # the WS147 before those lines can be lowered. Configure the
            # unopened instance first so one-shot provider calls do not erase
            # the active Runtime session between lifecycle operations.
            connection = serial.Serial(port=None,
                                       baudrate=self.config.baud,
                                       timeout=0.05,
                                       write_timeout=2,
                                       dsrdtr=False,
                                       rtscts=False)
            connection.dtr = False
            connection.rts = False
            connection.port = self.config.port
            connection.open()
            self.serial = connection
            time.sleep(0.2)
            self.serial.reset_input_buffer()
        except Exception as error:
            raise ProviderError("transport-unavailable", "configured endpoint is unavailable") from error

    def close(self) -> None:
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def request(self, message_type: int, payload: bytes = b"") -> bytes:
        if self.serial is None:
            raise ProviderError("transport-unavailable", "configured endpoint is unavailable")
        request_id = self.request_id
        self.request_id += 1
        outgoing = frame(message_type, self.session_id, request_id, payload)
        try:
            self.serial.write(outgoing)
            self.serial.flush()
        except Exception as error:
            raise ProviderError("transport-unavailable", "failed to write configured endpoint") from error
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            try:
                data = self.serial.read(512)
            except Exception as error:
                raise ProviderError("transport-unavailable", "failed to read configured endpoint") from error
            if data:
                self.pending.extend(data)
            while len(self.pending) >= HEADER_BYTES:
                marker = self.pending.find(b"JFDP")
                if marker != 0:
                    # USB ROM reset noise is never forwarded or interpreted as a console.
                    del self.pending[:len(self.pending) if marker < 0 else marker]
                    continue
                size = struct.unpack_from("<I", self.pending, 16)[0]
                if size > MAX_PAYLOAD:
                    raise ProviderError("protocol-mismatch", "device emitted oversized JFDP response")
                total = HEADER_BYTES + size
                if len(self.pending) < total:
                    break
                raw = bytes(self.pending[:total])
                del self.pending[:total]
                response_type, session, response_request, response_payload = decode_frame(raw)
                if response_type != message_type or session != self.session_id or response_request != request_id:
                    raise ProviderError("protocol-mismatch", "device response correlation failed")
                return response_payload
        raise ProviderError("transport-unavailable", "configured endpoint timed out")


def load_config(path_text: str | None) -> ProviderConfig:
    default_path = Path(os.environ.get("JELLYFRAME_DEVICE_CONFIG", "")) if os.environ.get("JELLYFRAME_DEVICE_CONFIG") else Path(__file__).with_name("jellyframe-device.config.json")
    path = Path(path_text).expanduser() if path_text else default_path
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        if set(value) != {"endpointId", "port", "baud", "manifest"}:
            raise ValueError("configuration fields are invalid")
        endpoint_id, port = value["endpointId"], value["port"]
        baud = value["baud"]
        if (not isinstance(endpoint_id, str) or not endpoint_id.isascii() or not endpoint_id or len(endpoint_id) > 96 or
                not isinstance(port, str) or not port or not isinstance(baud, int) or not 9600 <= baud <= 1000000):
            raise ValueError("configuration values are invalid")
        manifest_path = Path(value["manifest"]).expanduser()
        manifest = parse_device_image_manifest(manifest_path.read_bytes())
    except (OSError, ValueError, json.JSONDecodeError, DeviceImageManifestError) as error:
        raise ProviderError("provider-failed", "provider configuration is invalid") from error
    return ProviderConfig(endpoint_id, port, baud, manifest_path.resolve(), manifest)


def device_from_capabilities(config: ProviderConfig, capabilities: dict[str, Any]) -> dict[str, Any]:
    manifest = config.manifest
    display = manifest["board"]["display"]
    expected = (manifest["board"]["id"], manifest["runtimeVersion"], display["width"], display["height"], manifest["storage"]["maxBundleBytes"])
    actual = (capabilities["boardId"], capabilities["runtimeVersion"], capabilities["width"], capabilities["height"], capabilities["maxBundleBytes"])
    if actual != expected:
        raise ProviderError("protocol-mismatch", "configured endpoint does not match its Developer Image manifest")
    return {
        "endpointId": config.endpoint_id,
        "boardId": manifest["board"]["id"],
        "profileId": manifest["profile"]["id"],
        "imageVersion": manifest["imageVersion"],
        "runtimeVersion": manifest["runtimeVersion"],
        "protocol": "JFDP/1",
        "connected": True,
        "capabilities": {
            "display": display,
            "featureFamilies": manifest["profile"]["featureFamilies"],
            "maxBundleBytes": capabilities["maxBundleBytes"],
            "availableStorageBytes": capabilities["availableStorageBytes"],
        },
    }


def envelope(operation: str, request_id: str, result_code: str, **extra: Any) -> dict[str, Any]:
    return {"format": FORMAT, "formatVersion": 0, "kind": "result", "operation": operation,
            "requestId": request_id, "resultCode": result_code, "provider": PROVIDER, **extra}


def operation_result_fields(result: dict[str, Any]) -> dict[str, Any]:
    """Keep the provider's optional transaction conformant for non-install calls."""
    transaction = result.get("transaction")
    if isinstance(transaction, dict) and transaction.get("id", 0):
        return {"transaction": transaction}
    return {}


def stream_event(kind: str, operation: str, request_id: str, sequence: int, **extra: Any) -> dict[str, Any]:
    return {"format": FORMAT, "formatVersion": 0, "kind": kind, "operation": operation,
            "requestId": request_id, "sequence": sequence, "provider": PROVIDER, **extra}


def fixture_device() -> dict[str, Any]:
    return {"endpointId": "fixture-ws147", "boardId": "ws147", "profileId": "rect-172x320",
            "imageVersion": "0.1.0-dev", "runtimeVersion": "0.6.0-dev", "protocol": "JFDP/1", "connected": True,
            "capabilities": {"display": {"width": 172, "height": 320, "shape": "rect"},
                             "featureFamilies": ["core.document"], "maxBundleBytes": 327680,
                             "availableStorageBytes": 163840}}


def run_fixture(args: argparse.Namespace) -> tuple[int, list[dict[str, Any]]]:
    name = args.fixture
    operation = args.operation
    if name == "no-device":
        return 0, [envelope(operation, args.request_id, "ok", devices=[])]
    if name == "image-mismatch":
        device = fixture_device(); device["profileId"] = "wrong-profile"
        return 0, [envelope(operation, args.request_id, "ok", devices=[device])]
    if name == "transport-unavailable":
        return 3, [envelope(operation, args.request_id, "transport-unavailable", message="fixture transport unavailable")]
    if name == "storage-full":
        return 1, [stream_event("result", operation, args.request_id, 1, resultCode="storage-full",
                                 transaction={"id": 1, "receivedBytes": 0, "expectedBytes": 1, "complete": False, "active": False})]
    if name == "interrupted-install":
        return 1, [stream_event("result", operation, args.request_id, 1, resultCode="cancelled",
                                 transaction={"id": 1, "receivedBytes": 64, "expectedBytes": 128, "complete": False, "active": False})]
    if name == "confirmed-cancel":
        return 0, [envelope(operation, args.request_id, "ok", cancellation={"confirmed": True})]
    if name == "unconfirmed-cancel":
        return 1, [envelope(operation, args.request_id, "failed", cancellation={"confirmed": False})]
    if name == "bounded-logs":
        events = [stream_event("log", operation, args.request_id, index + 1,
                               log={"level": "info", "appId": "org.jellyframe.fixture", "message": f"log-{index}"})
                  for index in range(min(args.limit, 3))]
        events.append(stream_event("result", operation, args.request_id, len(events) + 1, resultCode="ok",
                                   logs=[event["log"] for event in events]))
        return 0, events
    raise ProviderError("invalid-request", "unknown provider fixture")


def read_bundle_identity(bundle: Path) -> str:
    # DeviceInstallStore remains the final JFAPPV0 authority. The provider
    # reads just the standard, bounded summary so it can populate the existing
    # JFDP InstallBegin app-id field without asking the caller for a sidecar.
    if not bundle.is_absolute() or not bundle.is_file() or bundle.suffix != ".jfapp" or bundle.stat().st_size > 327680:
        raise ProviderError("invalid-request", "install requires a bounded absolute .jfapp bundle")
    raw = bundle.read_bytes()
    header_format = "<8sHHIIIIIIIIIII"
    header_size = struct.calcsize(header_format)
    if len(raw) < header_size or raw[:8] != b"JFAPPV0\0":
        raise ProviderError("integrity-failed", "bundle is not a JFAPPV0 container")
    try:
        (_magic, declared_header_bytes, format_version, _flags, summary_offset, summary_bytes,
         _index_offset, _resource_count, _strings_offset, _strings_bytes, _payload_offset,
         _payload_bytes, stored_crc, reserved) = struct.unpack_from(header_format, raw)
        if (declared_header_bytes != header_size or format_version != 0 or reserved != 0 or
                summary_bytes == 0 or summary_bytes > 4096 or summary_offset < header_size or
                summary_offset + summary_bytes > len(raw)):
            raise ValueError("invalid JFAPPV0 header")
        crc_bytes = bytearray(raw)
        crc_bytes[48:52] = b"\0\0\0\0"
        if zlib.crc32(crc_bytes) & 0xffffffff != stored_crc:
            raise ValueError("invalid JFAPPV0 checksum")
        summary = json.loads(raw[summary_offset:summary_offset + summary_bytes].decode("utf-8"),
                             object_pairs_hook=reject_duplicate_members)
        if not isinstance(summary, dict):
            raise ValueError("invalid JFAPPV0 summary")
        app_id = summary["id"]
    except (KeyError, UnicodeDecodeError, ValueError, struct.error, json.JSONDecodeError) as error:
        raise ProviderError("integrity-failed", "bundle has an invalid JFAPPV0 summary") from error
    if not isinstance(app_id, str) or not app_id or not app_id.isascii() or len(app_id) > 95:
        raise ProviderError("invalid-request", "bundle package report has an invalid app id")
    return app_id


def reject_duplicate_members(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON member")
        result[key] = value
    return result


def run_physical(args: argparse.Namespace, config: ProviderConfig) -> tuple[int, list[dict[str, Any]]]:
    wire = Wire(config)
    try:
        wire.open()
        capabilities = decode_capabilities(wire.request(DISCOVERY))
        device = device_from_capabilities(config, capabilities)
        if args.operation == "discover":
            return 0, [envelope("discover", args.request_id, "ok", devices=[device])]
        if args.selector != config.endpoint_id:
            raise ProviderError("invalid-request", "selector does not identify this configured endpoint")
        if args.operation == "info":
            return 0, [envelope("info", args.request_id, "ok", device=device)]
        if args.operation == "list":
            generation, apps = decode_list(wire.request(APP_LIST))
            return 0, [envelope("list", args.request_id, "ok", device=device, apps=apps, registryGeneration=generation)]
        if args.operation == "recovery":
            return 0, [envelope("recovery", args.request_id, "ok", device=device, recovery=decode_recovery(wire.request(RECOVERY)))]
        if args.operation in {"launch", "stop", "remove", "rollback"}:
            type_map = {"launch": LAUNCH, "stop": STOP, "remove": REMOVE, "rollback": ROLLBACK}
            result = decode_result(wire.request(type_map[args.operation], app_id_payload(args.app_id)))
            result_code = result["resultCode"]
            return exit_code_for(result_code), [envelope(args.operation, args.request_id, result_code, device=device,
                                                         **operation_result_fields(result))]
        if args.operation == "cancel":
            result = decode_result(wire.request(INSTALL_ABORT, transaction_payload(args.transaction_id)))
            confirmed = result["resultCode"] == "cancelled"
            provider_code = "ok" if confirmed else result["resultCode"]
            return exit_code_for(provider_code), [envelope("cancel", args.request_id, provider_code, device=device, cancellation={"confirmed": confirmed}, **result)]
        if args.operation == "logs":
            raise ProviderError("unsupported", "WS147 Developer Image does not yet expose typed app logs")
        if args.operation == "install":
            app_id = read_bundle_identity(args.bundle)
            bundle = args.bundle.read_bytes()
            transaction = int(time.monotonic_ns() & 0xffffffff) or 1
            events: list[dict[str, Any]] = []
            sequence = 1
            begin = decode_result(wire.request(INSTALL_BEGIN, begin_payload(transaction, app_id, bundle, args.allow_downgrade)))
            if begin["resultCode"] != "accepted":
                return exit_code_for(begin["resultCode"]), [stream_event("result", "install", args.request_id, sequence, resultCode=begin["resultCode"], device=device, transaction=begin["transaction"])]
            for offset in range(0, len(bundle), 1024):
                chunk = bundle[offset:offset + 1024]
                progress = decode_result(wire.request(INSTALL_CHUNK, chunk_payload(transaction, offset, chunk)))
                if progress["resultCode"] != "accepted":
                    return exit_code_for(progress["resultCode"]), [*events, stream_event("result", "install", args.request_id, sequence, resultCode=progress["resultCode"], device=device, transaction=progress["transaction"])]
                events.append(stream_event("progress", "install", args.request_id, sequence,
                                           progress={"completedBytes": progress["transaction"]["receivedBytes"], "totalBytes": len(bundle)}))
                sequence += 1
            committed = decode_result(wire.request(INSTALL_COMMIT, transaction_payload(transaction)))
            events.append(stream_event("result", "install", args.request_id, sequence, resultCode=committed["resultCode"],
                                       device=device, transaction=committed["transaction"]))
            return exit_code_for(committed["resultCode"]), events
        raise ProviderError("invalid-request", "unsupported operation")
    finally:
        wire.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, choices=("json", "jsonl"))
    parser.add_argument("--request-id", required=True)
    parser.add_argument("--selector")
    parser.add_argument("--config")
    parser.add_argument("--fixture", choices=("no-device", "image-mismatch", "transport-unavailable", "storage-full", "interrupted-install", "confirmed-cancel", "unconfirmed-cancel", "bounded-logs"))
    subparsers = parser.add_subparsers(dest="operation", required=True)
    for name in ("discover", "info", "list", "recovery"):
        subparsers.add_parser(name)
    install = subparsers.add_parser("install")
    install.add_argument("--bundle", type=Path, required=True)
    install.add_argument("--allow-downgrade", action="store_true")
    cancel = subparsers.add_parser("cancel")
    cancel.add_argument("--transaction-id", type=int, required=True)
    for name in ("launch", "stop", "remove", "rollback"):
        command = subparsers.add_parser(name)
        command.add_argument("--id", dest="app_id", required=True)
        if name == "remove":
            command.add_argument("--keep-data", action="store_true")
    logs = subparsers.add_parser("logs")
    logs.add_argument("--id", dest="app_id", required=True)
    logs.add_argument("--limit", type=int, default=64)
    return parser.parse_args()


def emit(events: list[dict[str, Any]], output: str) -> None:
    if output == "json":
        if len(events) != 1:
            raise ProviderError("provider-failed", "stream operation requires jsonl output")
        print(json.dumps(events[0], separators=(",", ":"), ensure_ascii=True))
    else:
        for event in events:
            print(json.dumps(event, separators=(",", ":"), ensure_ascii=True))


def terminal_error(args: argparse.Namespace, result_code: str, message: str) -> list[dict[str, Any]]:
    if args.output == "jsonl":
        return [stream_event("result", args.operation, args.request_id, 1,
                             resultCode=result_code, message=message)]
    return [envelope(args.operation, args.request_id, result_code, message=message)]


def main() -> int:
    args = parse_args()
    if not args.request_id.isascii() or not args.request_id or len(args.request_id) > 64:
        args.request_id = "invalid-request-id"
        emit(terminal_error(args, "invalid-request", "request id is invalid"), args.output)
        return 2
    if args.operation in {"install", "logs"} and args.output != "jsonl":
        emit(terminal_error(args, "invalid-request", "operation requires jsonl output"), args.output)
        return 2
    if args.operation not in {"discover"} and not args.selector and not args.fixture:
        emit(terminal_error(args, "invalid-request", "selector is required"), args.output)
        return 2
    try:
        fixture_from_environment = os.environ.get("JELLYFRAME_DEVICE_TEST_FIXTURE")
        if fixture_from_environment and not args.fixture:
            args.fixture = fixture_from_environment
        if args.fixture:
            if os.environ.get("JELLYFRAME_DEVICE_TEST_MODE") != "1":
                raise ProviderError("invalid-request", "provider fixtures require JELLYFRAME_DEVICE_TEST_MODE=1")
            if args.fixture not in {"no-device", "image-mismatch", "transport-unavailable", "storage-full",
                                    "interrupted-install", "confirmed-cancel", "unconfirmed-cancel", "bounded-logs"}:
                raise ProviderError("invalid-request", "unknown provider fixture")
            status, events = run_fixture(args)
        else:
            config = load_config(args.config)
            status, events = run_physical(args, config)
        emit(events, args.output)
        return status
    except ProviderError as error:
        print(f"jellyframe-device: {error}", file=sys.stderr)
        try:
            emit(terminal_error(args, error.code, str(error)), args.output)
        except ProviderError:
            return 5
        return error.exit_code


if __name__ == "__main__":
    sys.exit(main())
