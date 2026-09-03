#!/usr/bin/env python3
"""Exercise the WS147 JFDP/1 persistent Developer Image endpoint.

The probe emits only public JFDP/1 typed payloads. Bundle fixtures are built
by the repository packager, so the device always verifies genuine JFAPPV0
containers rather than a test-specific binary format.
"""

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError as error:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from error


HEADER_BYTES = 24
MAX_PAYLOAD_BYTES = 4096
RESPONSE_FLAG = 1
DISCOVERY, APP_LIST, INSTALL_BEGIN, INSTALL_CHUNK, INSTALL_COMMIT, INSTALL_ABORT, LAUNCH, STOP, _LOGS, RECOVERY, REMOVE, ROLLBACK = range(1, 13)
RESULT_OK = 0
RESULT_ACCEPTED = 1
RESULT_INTEGRITY_FAILED = 11
RESULT_CANCELLED = 13
RESULT_NOT_FOUND = 7


def frame(message_type, session, request, payload=b"", flags=0):
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ValueError("payload exceeds JFDP/1 maximum")
    return (b"JFDP" + bytes((1, message_type)) + struct.pack("<HIII", flags, session, request, len(payload)) +
            struct.pack("<I", zlib.crc32(payload) & 0xffffffff) + payload)


def decode_frame(value):
    if len(value) < HEADER_BYTES or value[:4] != b"JFDP" or value[4] != 1:
        raise AssertionError("invalid JFDP/1 frame")
    flags, session, request, size, checksum = struct.unpack_from("<HIIII", value, 6)
    if size > MAX_PAYLOAD_BYTES or len(value) != HEADER_BYTES + size:
        raise AssertionError("invalid JFDP/1 frame length")
    payload = value[HEADER_BYTES:]
    if zlib.crc32(payload) & 0xffffffff != checksum:
        raise AssertionError("invalid JFDP/1 payload CRC")
    return {"type": value[5], "flags": flags, "session": session, "request": request, "payload": payload}


def app_id_payload(app_id):
    raw = app_id.encode("utf-8")
    return bytes((1, len(raw), 0, 0)) + raw


def begin_payload(transaction_id, app_id, bundle, allow_downgrade=False):
    raw = app_id.encode("utf-8")
    return struct.pack("<BBBBIII", 1, 1 if allow_downgrade else 0, len(raw), 0, transaction_id,
                       len(bundle), zlib.crc32(bundle) & 0xffffffff) + raw


def chunk_payload(transaction_id, offset, bytes_):
    return struct.pack("<BBHII", 1, 0, len(bytes_), transaction_id, offset) + bytes_


def transaction_payload(transaction_id):
    return struct.pack("<BBBBI", 1, 0, 0, 0, transaction_id)


def decode_result(payload):
    if len(payload) != 16 or payload[0] != 1:
        raise AssertionError("invalid operation-result payload")
    code, flags, transaction_id, received, expected = struct.unpack_from("<BHIII", payload, 1)
    return {"code": code, "flags": flags, "transactionId": transaction_id,
            "receivedBytes": received, "expectedBytes": expected}


def decode_list(payload):
    if len(payload) < 8 or payload[0] != 1 or payload[2:4] != b"\0\0":
        raise AssertionError("invalid app-list payload")
    count = payload[1]
    generation = struct.unpack_from("<I", payload, 4)[0]
    cursor = 8
    entries = []
    for _ in range(count):
        if len(payload) - cursor < 12:
            raise AssertionError("truncated app-list entry")
        app_bytes, version_bytes, state, flags, version_code, bundle_bytes = struct.unpack_from("<BBBBII", payload, cursor)
        cursor += 12
        end = cursor + app_bytes + version_bytes
        if app_bytes == 0 or version_bytes == 0 or end > len(payload):
            raise AssertionError("invalid app-list string")
        app_id = payload[cursor:cursor + app_bytes].decode("utf-8")
        version_name = payload[cursor + app_bytes:end].decode("utf-8")
        cursor = end
        entries.append({"appId": app_id, "versionName": version_name, "versionCode": version_code,
                        "bundleBytes": bundle_bytes, "state": state, "flags": flags})
    if cursor != len(payload):
        raise AssertionError("app-list trailing bytes")
    return {"generation": generation, "entries": entries}


def decode_recovery(payload):
    if len(payload) < 16 or payload[0] != 1 or payload[13:16] != b"\0\0\0":
        raise AssertionError("invalid recovery payload")
    reason = payload[1]
    flags, generation, sequence = struct.unpack_from("<HII", payload, 2)
    app_bytes = payload[12]
    if len(payload) != 16 + app_bytes:
        raise AssertionError("invalid recovery payload length")
    app_id = payload[16:].decode("utf-8")
    return {"reason": reason, "flags": flags, "generation": generation, "sequence": sequence, "appId": app_id}


class Wire:
    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.serial = None
        self.pending = bytearray()
        self.boot_noise_deadline = 0.0
        self.capture = []
        self.reset_logs = []
        self.counters = {"txFrames": 0, "rxFrames": 0, "reconnects": 0, "timeouts": 0}

    def open(self):
        self.serial = serial.Serial()
        self.serial.port = self.port
        self.serial.baudrate = self.baud
        self.serial.timeout = 0.05
        self.serial.write_timeout = 2
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()
        # Native USB connection also exposes the immutable ROM boot banner.
        # It is emitted before the application endpoint takes ownership of
        # the stream, so drain it before the first JFDP/1 request. Every byte
        # captured after this boundary remains protocol-only.
        time.sleep(1.2)
        self.serial.reset_input_buffer()
        self.pending.clear()
        self.boot_noise_deadline = time.monotonic() + 5.0

    def close(self):
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def reconnect(self):
        self.close()
        time.sleep(0.15)
        self.open()
        self.counters["reconnects"] += 1

    def physical_reboot(self):
        """Reset through the board's normal esptool transport, never JFDP."""
        self.close()
        completed = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", self.port,
             "--before", "default_reset", "--after", "hard_reset", "chip_id"],
            text=True, capture_output=True, check=False)
        self.reset_logs.append({"returnCode": completed.returncode,
                                "stdout": completed.stdout,
                                "stderr": completed.stderr})
        if completed.returncode:
            raise RuntimeError("physical reboot failed: " + completed.stderr)
        time.sleep(0.35)
        self.open()

    def request(self, message_type, session, request, payload=b"", label="request"):
        outgoing = frame(message_type, session, request, payload)
        self.serial.write(outgoing)
        self.serial.flush()
        self.capture.append({"direction": "tx", "label": label, "hex": outgoing.hex()})
        self.counters["txFrames"] += 1
        response = self.receive(label)
        decoded = decode_frame(response)
        if decoded["flags"] != RESPONSE_FLAG or decoded["type"] != message_type or \
                decoded["session"] != session or decoded["request"] != request:
            raise AssertionError(f"correlation failed: {label}")
        return decoded["payload"]

    def receive(self, label, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.serial.read(512)
            if data:
                self.pending.extend(data)
            if len(self.pending) >= HEADER_BYTES:
                if self.pending[:4] != b"JFDP":
                    if time.monotonic() < self.boot_noise_deadline:
                        marker = self.pending.find(b"JFDP")
                        if marker >= 0:
                            del self.pending[:marker]
                        else:
                            self.pending.clear()
                            continue
                    else:
                        raise AssertionError("non-JFDP bytes on lifecycle endpoint: " + self.pending[:32].hex())
                if len(self.pending) < HEADER_BYTES:
                    continue
                if self.pending[:4] != b"JFDP":
                    raise AssertionError("non-JFDP bytes on lifecycle endpoint: " + self.pending[:32].hex())
                size = struct.unpack_from("<I", self.pending, 16)[0]
                if size > MAX_PAYLOAD_BYTES:
                    raise AssertionError("device emitted oversized frame")
                total = HEADER_BYTES + size
                if len(self.pending) >= total:
                    value = bytes(self.pending[:total])
                    del self.pending[:total]
                    self.capture.append({"direction": "rx", "label": label, "hex": value.hex()})
                    self.counters["rxFrames"] += 1
                    return value
        self.counters["timeouts"] += 1
        raise TimeoutError(label)


def run_case(results, name, callback):
    try:
        callback()
        results[name] = {"result": "pass"}
    except Exception as error:
        results[name] = {"result": "fail", "error": str(error)}
        raise


def package_fixture(repo, output, version_code, version_name):
    source = output / f"fixture-v{version_code}"
    if source.exists():
        shutil.rmtree(source)
    source.mkdir(parents=True)
    manifest = {
        "format": "jellyframe.app", "formatVersion": 0,
        "id": "org.jellyframe.device.lifecycle", "name": "Device Lifecycle",
        "version": {"name": version_name, "code": version_code}, "entry": "/index.html",
        "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.6.1", "script": "none"},
        "viewport": {"designWidth": 172, "designHeight": 320, "shape": "rect"},
        "budgets": {"maxResourceBytes": 4096, "maxDomNodes": 32, "maxCssRules": 8,
                    "maxDisplayCommands": 32, "maxTimers": 0, "maxEventListeners": 0},
        "capabilities": [],
        "targets": {"ws147": {"viewport": {"width": 172, "height": 320, "shape": "rect"},
                                "fontProfile": "tiny-plus-symbols", "output": "jfapp"}},
    }
    (source / "jellyframe.app.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (source / "index.html").write_text(
        f"<!doctype html><html><body><main id='launch-marker'>LIFECYCLE-{version_name}</main></body></html>\n",
        encoding="utf-8")
    bundle = output / f"lifecycle-{version_code}.jfapp"
    report = output / f"lifecycle-{version_code}.package.json"
    completed = subprocess.run([sys.executable, str(repo / "tools" / "package_app.py"), "--root", str(source),
                                "--output-bundle", str(bundle), "--report", str(report)],
                               cwd=repo, text=True, capture_output=True, check=False)
    (output / f"lifecycle-{version_code}.package.log").write_text(completed.stdout + completed.stderr, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(f"fixture package failed: {completed.stdout}{completed.stderr}")
    return bundle.read_bytes()


def install(wire, request_id, transaction_id, app_id, bundle, allow_downgrade=False):
    begin = decode_result(wire.request(INSTALL_BEGIN, 0x9200, request_id, begin_payload(transaction_id, app_id, bundle, allow_downgrade), "install-begin"))
    if begin["code"] != RESULT_ACCEPTED:
        return begin
    request_id += 1
    for offset in range(0, len(bundle), 512):
        part = bundle[offset:offset + 512]
        result = decode_result(wire.request(INSTALL_CHUNK, 0x9200, request_id, chunk_payload(transaction_id, offset, part), "install-chunk"))
        if result["code"] != RESULT_ACCEPTED:
            return result
        request_id += 1
    return decode_result(wire.request(INSTALL_COMMIT, 0x9200, request_id, transaction_payload(transaction_id), "install-commit"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cycles", type=int, default=30)
    parser.add_argument("--physical-reboot", action="store_true",
                        help="Use the board reset transport after durable lifecycle transitions.")
    args = parser.parse_args()
    if args.cycles < 1:
        raise SystemExit("--cycles must be positive")
    repo = Path(__file__).resolve().parents[3]
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    first = package_fixture(repo, output, 1, "1.0.0")
    second = package_fixture(repo, output, 2, "2.0.0")
    malformed = bytearray(first); malformed[-1] ^= 0x80
    (output / "lifecycle-malformed.jfapp").write_bytes(malformed)
    app_id = "org.jellyframe.device.lifecycle"
    wire = Wire(args.port, args.baud)
    results = {}
    try:
        wire.open()

        def baseline():
            assert decode_list(wire.request(APP_LIST, 0x9100, 1, label="baseline-list"))["entries"] == []
            # Recovery is a last-event diagnostic. A clean app library may
            # legitimately retain the previous controlled rejection/fallback.
            recovery = decode_recovery(wire.request(RECOVERY, 0x9100, 2, label="baseline-recovery"))
            assert 0 <= recovery["reason"] <= 6
        run_case(results, "baseline_empty_library", baseline)

        def install_update_rollback():
            assert install(wire, 10, 1001, app_id, first)["code"] == RESULT_ACCEPTED
            if args.physical_reboot:
                wire.physical_reboot()
            assert decode_list(wire.request(APP_LIST, 0x9100, 11, label="list-v1"))["entries"][0]["versionCode"] == 1
            assert decode_result(wire.request(LAUNCH, 0x9100, 12, app_id_payload(app_id), "launch-v1"))["code"] == RESULT_OK
            assert decode_result(wire.request(STOP, 0x9100, 13, app_id_payload(app_id), "stop-v1"))["code"] == RESULT_OK
            assert install(wire, 20, 1002, app_id, second)["code"] == RESULT_ACCEPTED
            if args.physical_reboot:
                wire.physical_reboot()
            listed = decode_list(wire.request(APP_LIST, 0x9100, 21, label="list-v2"))["entries"]
            assert listed[0]["versionCode"] == 2 and listed[0]["flags"] & 1
            rejected = install(wire, 30, 1003, app_id, first)
            assert rejected["code"] == RESULT_INTEGRITY_FAILED
            if args.physical_reboot:
                wire.physical_reboot()
            assert decode_list(wire.request(APP_LIST, 0x9100, 31, label="list-after-downgrade-reject"))["entries"][0]["versionCode"] == 2
            assert decode_result(wire.request(ROLLBACK, 0x9100, 32, app_id_payload(app_id), "rollback"))["code"] == RESULT_OK
            if args.physical_reboot:
                wire.physical_reboot()
            assert decode_list(wire.request(APP_LIST, 0x9100, 33, label="list-rollback"))["entries"][0]["versionCode"] == 1
        run_case(results, "install_update_downgrade_reject_rollback", install_update_rollback)

        def invalid_abort_remove():
            assert install(wire, 40, 1004, app_id, bytes(malformed))["code"] == RESULT_INTEGRITY_FAILED
            begin = decode_result(wire.request(INSTALL_BEGIN, 0x9200, 41, begin_payload(1005, app_id, second), "abort-begin"))
            assert begin["code"] == RESULT_ACCEPTED
            assert decode_result(wire.request(INSTALL_CHUNK, 0x9200, 42, chunk_payload(1005, 0, second[:128]), "abort-chunk"))["code"] == RESULT_ACCEPTED
            assert decode_result(wire.request(INSTALL_ABORT, 0x9200, 43, transaction_payload(1005), "abort"))["code"] == RESULT_CANCELLED
            assert decode_list(wire.request(APP_LIST, 0x9100, 44, label="list-after-abort"))["entries"][0]["versionCode"] == 1
            assert decode_result(wire.request(REMOVE, 0x9100, 45, app_id_payload(app_id), "remove"))["code"] == RESULT_OK
            assert decode_list(wire.request(APP_LIST, 0x9100, 46, label="list-after-remove"))["entries"] == []
            assert decode_result(wire.request(LAUNCH, 0x9100, 47, app_id_payload(app_id), "launch-removed"))["code"] != RESULT_OK
        run_case(results, "invalid_abort_remove_launcher_fallback", invalid_abort_remove)

        def repetition():
            request = 100
            transaction = 2000
            for cycle in range(args.cycles):
                assert install(wire, request, transaction, app_id, first)["code"] == RESULT_ACCEPTED
                request += 20; transaction += 1
                assert install(wire, request, transaction, app_id, second)["code"] == RESULT_ACCEPTED
                request += 20; transaction += 1
                assert decode_result(wire.request(ROLLBACK, 0x9100, request, app_id_payload(app_id), f"cycle-{cycle}-rollback"))["code"] == RESULT_OK
                request += 1
                wire.reconnect()
                assert decode_list(wire.request(APP_LIST, 0x9100, request, label=f"cycle-{cycle}-list"))["entries"][0]["versionCode"] == 1
                request += 1
                assert decode_result(wire.request(REMOVE, 0x9100, request, app_id_payload(app_id), f"cycle-{cycle}-remove"))["code"] == RESULT_OK
                request += 1
        run_case(results, "repetition_install_update_rollback_remove", repetition)
    finally:
        wire.close()
        (output / "jfdp_capture.json").write_text(json.dumps(wire.capture, indent=2), encoding="utf-8")
        (output / "physical_reboot.log").write_text(
            "\n\n".join("reset %d\n%s%s" % (index + 1, item["stdout"], item["stderr"])
                       for index, item in enumerate(wire.reset_logs)), encoding="utf-8")
        fixture_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in output.glob("*.jfapp")}
        summary = {"port": args.port, "baud": args.baud, "cycles": args.cycles, "fixtureSha256": fixture_hashes,
                   "cases": results, "hostCounters": wire.counters,
                   "physicalReboots": len(wire.reset_logs),
                   "result": "pass" if results and all(case["result"] == "pass" for case in results.values()) else "fail"}
        (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
