"""Desktop-only JFDP/1 reference-host control semantics.

This module is deliberately not a physical transport. It gives the CLI a
durable reference implementation for the bounded install/lifecycle operations
that a board transport must later implement against the same protocol contract.
"""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

import app_registry


REFERENCE_FORMAT_VERSION = 0
REFERENCE_DIRECTORY = ".jfdp_reference"
MAX_REFERENCE_LOG_ENTRIES = 256
DEFAULT_CHUNK_BYTES = 1024
MAX_CHUNK_BYTES = 4096
JFDP_MAGIC = b"JFDP"
JFDP_PROTOCOL_VERSION = 1
JFDP_MAX_PAYLOAD_BYTES = 4096
JFDP_FRAME_FLAG_RESPONSE = 1
JFDP_HEADER_BYTES = 24
JFDP_PAYLOAD_VERSION = 1
JFDP_MAX_APP_ID_BYTES = 95

JFDP_MESSAGE_TYPES = {
    "discovery": 1,
    "app-list": 2,
    "install-begin": 3,
    "install-chunk": 4,
    "install-commit": 5,
    "install-abort": 6,
    "launch": 7,
    "stop": 8,
    "logs": 9,
    "recovery": 10,
    "remove": 11,
    "rollback": 12,
}
JFDP_MESSAGE_NAMES = {value: key for key, value in JFDP_MESSAGE_TYPES.items()}
JFDP_RESULT_CODES = {
    "ok": 0,
    "accepted": 1,
    "queued": 2,
    "invalid-request": 3,
    "busy": 4,
    "unsupported": 5,
    "denied": 6,
    "not-found": 7,
    "stale-session": 8,
    "stale-request": 9,
    "payload-too-large": 10,
    "integrity-failed": 11,
    "storage-full": 12,
    "cancelled": 13,
    "failed": 14,
}
JFDP_RESULT_NAMES = {value: key for key, value in JFDP_RESULT_CODES.items()}
JFDP_RESULT_COMPLETE = 1 << 0
JFDP_RESULT_ACTIVE = 1 << 1
JFDP_RESULT_LAUNCHER_ACTIVE = 1 << 2


class ReferenceDeviceError(RuntimeError):
    def __init__(self, result_code: str, message: str):
        super().__init__(message)
        self.result_code = result_code


def reference_root(store: Path) -> Path:
    return store.resolve() / REFERENCE_DIRECTORY


def transactions_root(store: Path) -> Path:
    return reference_root(store) / "transactions"


def staging_path(store: Path, transaction_id: int) -> Path:
    return transactions_root(store) / f"{transaction_id}.part"


def transaction_path(store: Path, transaction_id: int) -> Path:
    return transactions_root(store) / f"{transaction_id}.json"


def runtime_state_path(store: Path) -> Path:
    return reference_root(store) / "runtime.json"


def logs_path(store: Path) -> Path:
    return reference_root(store) / "logs.json"


def default_runtime_state() -> dict:
    return {
        "format": "jellyframe.device_reference.runtime",
        "formatVersion": REFERENCE_FORMAT_VERSION,
        "nextTransactionId": 1,
        "activeAppId": "",
        "lastFailure": {},
    }


def load_runtime_state(store: Path) -> dict:
    path = runtime_state_path(store)
    if not path.is_file():
        return default_runtime_state()
    try:
        state = app_registry.read_json(path)
    except SystemExit as error:
        raise ReferenceDeviceError("failed", str(error)) from error
    if (state.get("format") != "jellyframe.device_reference.runtime" or
            state.get("formatVersion") != REFERENCE_FORMAT_VERSION):
        raise ReferenceDeviceError("failed", f"unsupported reference runtime state: {path}")
    if not isinstance(state.get("nextTransactionId"), int) or state["nextTransactionId"] <= 0:
        raise ReferenceDeviceError("failed", f"invalid reference runtime state: {path}")
    if not isinstance(state.get("activeAppId"), str) or not isinstance(state.get("lastFailure"), dict):
        raise ReferenceDeviceError("failed", f"invalid reference runtime state: {path}")
    return state


def save_runtime_state(store: Path, state: dict) -> None:
    app_registry.atomic_write_json(runtime_state_path(store), state)


def record_failure(store: Path, operation: str, result_code: str, message: str) -> None:
    state = load_runtime_state(store)
    state["lastFailure"] = {
        "atUtc": app_registry.utc_now(),
        "operation": operation,
        "reason": result_code,
        "message": message[:160],
    }
    save_runtime_state(store, state)


def reference_metadata() -> dict:
    return {
        "endpoint": "desktop-reference",
        "transport": "reference",
        "deviceAvailable": False,
        "protocol": "JFDP/1",
        "profileId": "reference-no-device",
        "note": "Desktop control-semantics reference only; it is not a physical board or transport.",
    }


def discovery(store: Path) -> dict:
    state = load_runtime_state(store)
    result = reference_metadata()
    result.update({
        "operation": "discovery",
        "resultCode": "ok",
        "capabilities": {
            "protocolVersion": 1,
            "display": None,
            "capabilityBits": 0,
            "maxBundleBytes": app_registry.DEFAULT_MAX_BUNDLE_BYTES,
            "availableStorageBytes": None,
        },
        "referenceOperations": [
            "list", "install", "resume", "commit", "cancel", "launch", "stop",
            "rollback", "remove", "logs", "recovery",
        ],
        "pendingTransactionCount": pending_transaction_count(store),
        "activeAppId": state["activeAppId"],
    })
    return result


def transaction_record(store: Path, transaction_id: int) -> dict:
    if transaction_id <= 0:
        raise ReferenceDeviceError("invalid-request", "transaction id must be positive")
    path = transaction_path(store, transaction_id)
    if not path.is_file():
        raise ReferenceDeviceError("not-found", f"transaction is not active: {transaction_id}")
    try:
        record = app_registry.read_json(path)
    except SystemExit as error:
        raise ReferenceDeviceError("failed", str(error)) from error
    required = ("transactionId", "appId", "bundleBytes", "bundleCrc32", "receivedBytes", "allowDowngrade")
    if (record.get("format") != "jellyframe.device_reference.transaction" or
            record.get("formatVersion") != REFERENCE_FORMAT_VERSION or
            record.get("transactionId") != transaction_id or
            any(key not in record for key in required) or
            not isinstance(record["appId"], str) or not record["appId"] or
            not all(isinstance(record[key], int) and record[key] >= 0
                    for key in ("bundleBytes", "bundleCrc32", "receivedBytes")) or
            not isinstance(record["allowDowngrade"], bool) or
            record["receivedBytes"] > record["bundleBytes"]):
        raise ReferenceDeviceError("failed", f"invalid transaction metadata: {path}")
    part = staging_path(store, transaction_id)
    if not part.is_file() or part.stat().st_size != record["receivedBytes"]:
        raise ReferenceDeviceError("failed", f"transaction staging is missing or inconsistent: {transaction_id}")
    return record


def save_transaction(store: Path, record: dict) -> None:
    app_registry.atomic_write_json(transaction_path(store, int(record["transactionId"])), record)


def pending_transaction_count(store: Path) -> int:
    root = transactions_root(store)
    if not root.is_dir():
        return 0
    return len(list(root.glob("*.json")))


def append_log(store: Path, app_id: str, event: str, message: str) -> None:
    path = logs_path(store)
    if path.is_file():
        try:
            document = app_registry.read_json(path)
        except SystemExit as error:
            raise ReferenceDeviceError("failed", str(error)) from error
        entries = document.get("entries")
        if (document.get("format") != "jellyframe.device_reference.logs" or
                document.get("formatVersion") != REFERENCE_FORMAT_VERSION or
                not isinstance(entries, list)):
            raise ReferenceDeviceError("failed", f"invalid reference log store: {path}")
    else:
        document = {
            "format": "jellyframe.device_reference.logs",
            "formatVersion": REFERENCE_FORMAT_VERSION,
            "entries": [],
        }
        entries = document["entries"]
    sequence = int(entries[-1].get("sequence", 0) or 0) + 1 if entries else 1
    entries.append({
        "sequence": sequence,
        "atUtc": app_registry.utc_now(),
        "appId": app_id,
        "event": event,
        "message": message[:160],
    })
    document["entries"] = entries[-MAX_REFERENCE_LOG_ENTRIES:]
    app_registry.atomic_write_json(path, document)


def read_bundle(bundle_path: Path) -> tuple[bytes, dict]:
    try:
        bundle = app_registry.read_bundle(bundle_path, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
        bundle_info = app_registry.parse_jfapp(bundle)
    except SystemExit as error:
        raise ReferenceDeviceError("integrity-failed", str(error)) from error
    return bundle, bundle_info


def begin_install(store: Path, transaction_id: int, app_id: str, bundle_bytes: int,
                  bundle_crc32: int, allow_downgrade: bool) -> dict:
    if transaction_id <= 0 or not app_id or len(app_id.encode("utf-8")) > JFDP_MAX_APP_ID_BYTES:
        raise ReferenceDeviceError("invalid-request", "invalid install transaction or app id")
    if bundle_bytes <= 0 or bundle_bytes > app_registry.DEFAULT_MAX_BUNDLE_BYTES:
        raise ReferenceDeviceError("payload-too-large", "bundle size exceeds the reference endpoint limit")
    if transaction_path(store, transaction_id).exists() or staging_path(store, transaction_id).exists():
        raise ReferenceDeviceError("busy", f"transaction already exists: {transaction_id}")
    state = load_runtime_state(store)
    if transaction_id >= state["nextTransactionId"]:
        state["nextTransactionId"] = transaction_id + 1
        save_runtime_state(store, state)
    transactions_root(store).mkdir(parents=True, exist_ok=True)
    staging_path(store, transaction_id).write_bytes(b"")
    record = {
        "format": "jellyframe.device_reference.transaction",
        "formatVersion": REFERENCE_FORMAT_VERSION,
        "transactionId": transaction_id,
        "appId": app_id,
        "bundleBytes": bundle_bytes,
        "bundleCrc32": bundle_crc32,
        "receivedBytes": 0,
        "allowDowngrade": allow_downgrade,
    }
    save_transaction(store, record)
    append_log(store, app_id, "install-begin", f"transaction={transaction_id}")
    return record


def start_install(store: Path, bundle_path: Path, allow_downgrade: bool) -> tuple[dict, bytes]:
    bundle, bundle_info = read_bundle(bundle_path)
    state = load_runtime_state(store)
    transaction_id = state["nextTransactionId"]
    state["nextTransactionId"] = transaction_id + 1
    save_runtime_state(store, state)
    record = begin_install(
        store,
        transaction_id,
        bundle_info["summary"]["id"],
        len(bundle),
        zlib.crc32(bundle) & 0xffffffff,
        allow_downgrade,
    )
    return record, bundle


def append_chunk(store: Path, transaction_id: int, offset: int, chunk: bytes) -> dict:
    if offset < 0 or not chunk or len(chunk) > MAX_CHUNK_BYTES:
        raise ReferenceDeviceError("invalid-request", f"chunk must contain 1..{MAX_CHUNK_BYTES} bytes")
    record = transaction_record(store, transaction_id)
    if offset != record["receivedBytes"]:
        raise ReferenceDeviceError("invalid-request", "chunk offset does not match the receiving transaction")
    if len(chunk) > record["bundleBytes"] - offset:
        raise ReferenceDeviceError("payload-too-large", "chunk exceeds the declared bundle size")
    path = staging_path(store, transaction_id)
    with path.open("r+b") as output:
        output.seek(offset)
        output.write(chunk)
    record["receivedBytes"] += len(chunk)
    save_transaction(store, record)
    return {
        "transactionId": transaction_id,
        "phase": "receiving",
        "receivedBytes": record["receivedBytes"],
        "expectedBytes": record["bundleBytes"],
        "complete": record["receivedBytes"] == record["bundleBytes"],
    }


def append_bundle(store: Path, transaction_id: int, bundle: bytes, chunk_bytes: int,
                  pause_after_chunks: int | None = None) -> dict:
    if chunk_bytes <= 0 or chunk_bytes > MAX_CHUNK_BYTES:
        raise ReferenceDeviceError("invalid-request", f"chunk bytes must be 1..{MAX_CHUNK_BYTES}")
    if pause_after_chunks is not None and pause_after_chunks < 0:
        raise ReferenceDeviceError("invalid-request", "pause-after-chunks must not be negative")
    record = transaction_record(store, transaction_id)
    if len(bundle) != record["bundleBytes"] or (zlib.crc32(bundle) & 0xffffffff) != record["bundleCrc32"]:
        raise ReferenceDeviceError("integrity-failed", "resume bundle does not match the active transaction")
    chunks_written = 0
    progress = {
        "transactionId": transaction_id,
        "phase": "receiving",
        "receivedBytes": record["receivedBytes"],
        "expectedBytes": record["bundleBytes"],
        "complete": record["receivedBytes"] == record["bundleBytes"],
    }
    while not progress["complete"]:
        if pause_after_chunks is not None and chunks_written >= pause_after_chunks:
            break
        offset = int(progress["receivedBytes"])
        size = min(chunk_bytes, int(progress["expectedBytes"]) - offset)
        progress = append_chunk(store, transaction_id, offset, bundle[offset:offset + size])
        chunks_written += 1
    progress["chunkCount"] = chunks_written
    return progress


def discard_transaction(store: Path, transaction_id: int, event: str, message: str) -> dict:
    record = transaction_record(store, transaction_id)
    staging_path(store, transaction_id).unlink(missing_ok=True)
    transaction_path(store, transaction_id).unlink(missing_ok=True)
    append_log(store, record["appId"], event, message)
    return {
        "transactionId": transaction_id,
        "phase": "idle",
        "receivedBytes": record["receivedBytes"],
        "expectedBytes": record["bundleBytes"],
    }


def commit_install(store: Path, transaction_id: int) -> dict:
    record = transaction_record(store, transaction_id)
    if record["receivedBytes"] != record["bundleBytes"]:
        raise ReferenceDeviceError("invalid-request", "cannot commit an incomplete transaction")
    staged_bundle = staging_path(store, transaction_id).read_bytes()
    if len(staged_bundle) != record["bundleBytes"] or (zlib.crc32(staged_bundle) & 0xffffffff) != record["bundleCrc32"]:
        discard_transaction(store, transaction_id, "install-rejected", "staging integrity failed")
        raise ReferenceDeviceError("integrity-failed", "staging integrity failed")
    try:
        staged_summary = app_registry.parse_jfapp(staged_bundle)["summary"]
    except SystemExit as error:
        discard_transaction(store, transaction_id, "install-rejected", "staged bundle is invalid")
        raise ReferenceDeviceError("integrity-failed", str(error)) from error
    if staged_summary["id"] != record["appId"]:
        discard_transaction(store, transaction_id, "install-rejected", "staged app id does not match begin")
        raise ReferenceDeviceError("integrity-failed", "staged app id does not match begin")
    try:
        entry = app_registry.install_bundle(
            store,
            staging_path(store, transaction_id),
            app_registry.DEFAULT_MAX_APPS,
            app_registry.DEFAULT_MAX_BUNDLE_BYTES,
            allow_downgrade=record["allowDowngrade"],
        )
    except SystemExit as error:
        discard_transaction(store, transaction_id, "install-rejected", "registry commit rejected")
        raise ReferenceDeviceError("failed", str(error)) from error
    staging_path(store, transaction_id).unlink(missing_ok=True)
    transaction_path(store, transaction_id).unlink(missing_ok=True)
    append_log(store, record["appId"], "install-commit", f"transaction={transaction_id}")
    result = dict(entry)
    result["referenceTransaction"] = {
        "transactionId": transaction_id,
        "resultCode": "ok",
        "receivedBytes": record["receivedBytes"],
        "expectedBytes": record["bundleBytes"],
    }
    return result


def install(store: Path, bundle_path: Path, allow_downgrade: bool, chunk_bytes: int,
            pause_after_chunks: int | None = None) -> dict:
    record, bundle = start_install(store, bundle_path, allow_downgrade)
    progress = append_bundle(store, record["transactionId"], bundle, chunk_bytes, pause_after_chunks)
    if not progress["complete"]:
        return progress
    return commit_install(store, record["transactionId"])


def resume(store: Path, transaction_id: int, bundle_path: Path, chunk_bytes: int,
           pause_after_chunks: int | None = None) -> dict:
    bundle, _ = read_bundle(bundle_path)
    progress = append_bundle(store, transaction_id, bundle, chunk_bytes, pause_after_chunks)
    if not progress["complete"]:
        return progress
    return commit_install(store, transaction_id)


def cancel(store: Path, transaction_id: int) -> dict:
    result = discard_transaction(store, transaction_id, "install-cancel", f"transaction={transaction_id}")
    result["resultCode"] = "cancelled"
    return result


def app_state(store: Path) -> dict:
    try:
        return app_registry.app_manager_state_from_registry(store, app_registry.load_registry(store.resolve()))
    except SystemExit as error:
        raise ReferenceDeviceError("failed", str(error)) from error


def ensure_launchable(store: Path, app_id: str) -> dict:
    try:
        entry = app_registry.existing_app_entry(store, app_id)
    except SystemExit as error:
        raise ReferenceDeviceError("failed", str(error)) from error
    if entry is None:
        raise ReferenceDeviceError("not-found", f"installed app was not found: {app_id}")
    if not bool(entry.get("enabled", True)) or entry.get("status") != app_registry.APP_STATUS_INSTALLED:
        raise ReferenceDeviceError("denied", f"installed app is not launchable: {app_id}")
    return entry


def launch(store: Path, app_id: str) -> dict:
    ensure_launchable(store, app_id)
    state = load_runtime_state(store)
    state["activeAppId"] = app_id
    state["lastFailure"] = {}
    save_runtime_state(store, state)
    append_log(store, app_id, "launch", "reference launch accepted")
    return {"id": app_id, "active": True, "resultCode": "ok"}


def stop(store: Path, app_id: str | None = None) -> dict:
    state = load_runtime_state(store)
    active_app_id = state["activeAppId"]
    if app_id and active_app_id and app_id != active_app_id:
        raise ReferenceDeviceError("not-found", f"app is not active: {app_id}")
    stopped_app_id = active_app_id or app_id or ""
    state["activeAppId"] = ""
    save_runtime_state(store, state)
    if stopped_app_id:
        append_log(store, stopped_app_id, "stop", "reference stop accepted")
    return {"id": stopped_app_id, "active": False, "alreadyStopped": not bool(active_app_id), "resultCode": "ok"}


def remove(store: Path, app_id: str, keep_data: bool) -> dict:
    state = load_runtime_state(store)
    if state["activeAppId"] == app_id:
        stop(store, app_id)
    try:
        entry = app_registry.remove_app(store, app_id, delete_data=not keep_data)
    except SystemExit as error:
        raise ReferenceDeviceError("not-found", str(error)) from error
    append_log(store, app_id, "remove", "reference remove accepted")
    result = dict(entry)
    result["resultCode"] = "ok"
    return result


def rollback(store: Path, app_id: str) -> dict:
    state = load_runtime_state(store)
    if state["activeAppId"] == app_id:
        stop(store, app_id)
    try:
        entry = app_registry.rollback_app(store, app_id)
    except SystemExit as error:
        raise ReferenceDeviceError("failed", str(error)) from error
    append_log(store, app_id, "rollback", "reference rollback accepted")
    result = dict(entry)
    result["resultCode"] = "ok"
    return result


def logs(store: Path, app_id: str | None, limit: int) -> dict:
    if limit <= 0 or limit > MAX_REFERENCE_LOG_ENTRIES:
        raise ReferenceDeviceError("invalid-request", f"log limit must be 1..{MAX_REFERENCE_LOG_ENTRIES}")
    path = logs_path(store)
    if not path.is_file():
        entries: list[dict] = []
    else:
        try:
            document = app_registry.read_json(path)
        except SystemExit as error:
            raise ReferenceDeviceError("failed", str(error)) from error
        entries = document.get("entries", [])
        if not isinstance(entries, list):
            raise ReferenceDeviceError("failed", f"invalid reference log store: {path}")
    if app_id:
        entries = [entry for entry in entries if entry.get("appId") == app_id]
    return {"appId": app_id or "", "logs": entries[-limit:], "resultCode": "ok"}


def recovery(store: Path) -> dict:
    state = load_runtime_state(store)
    active_app_id = state["activeAppId"]
    return {
        "activeAppId": active_app_id,
        "launcherActive": not bool(active_app_id),
        "lastFailure": state["lastFailure"],
        "pendingTransactionCount": pending_transaction_count(store),
        "resultCode": "ok",
    }


def _jfdp_app_id_bytes(app_id: str, *, allow_empty: bool) -> bytes:
    try:
        encoded = app_id.encode("ascii")
    except UnicodeEncodeError as error:
        raise ReferenceDeviceError("invalid-request", "app id must be ASCII") from error
    if b"\0" in encoded or len(encoded) > JFDP_MAX_APP_ID_BYTES or (not allow_empty and not encoded):
        raise ReferenceDeviceError("invalid-request", "invalid app id payload")
    return encoded


def encode_jfdp_frame(message_type: str, session_id: int, request_id: int, payload: bytes = b"",
                      *, response: bool = False) -> bytes:
    if message_type not in JFDP_MESSAGE_TYPES or not (0 <= session_id <= 0xffffffff) or not (0 <= request_id <= 0xffffffff):
        raise ReferenceDeviceError("invalid-request", "invalid JFDP frame header")
    if len(payload) > JFDP_MAX_PAYLOAD_BYTES:
        raise ReferenceDeviceError("payload-too-large", "JFDP payload exceeds 4096 bytes")
    flags = JFDP_FRAME_FLAG_RESPONSE if response else 0
    header = struct.pack(
        "<4sBBHIIII",
        JFDP_MAGIC,
        JFDP_PROTOCOL_VERSION,
        JFDP_MESSAGE_TYPES[message_type],
        flags,
        session_id,
        request_id,
        len(payload),
        zlib.crc32(payload) & 0xffffffff,
    )
    return header + payload


def decode_jfdp_frame(frame: bytes) -> dict:
    if len(frame) < JFDP_HEADER_BYTES:
        raise ReferenceDeviceError("invalid-request", "truncated JFDP frame")
    magic, version, type_value, flags, session_id, request_id, payload_size, payload_crc32 = struct.unpack(
        "<4sBBHIIII", frame[:JFDP_HEADER_BYTES])
    if magic != JFDP_MAGIC or version != JFDP_PROTOCOL_VERSION:
        raise ReferenceDeviceError("invalid-request", "unsupported JFDP frame")
    if type_value not in JFDP_MESSAGE_NAMES or payload_size > JFDP_MAX_PAYLOAD_BYTES:
        raise ReferenceDeviceError("invalid-request", "invalid JFDP frame type or payload size")
    if len(frame) != JFDP_HEADER_BYTES + payload_size:
        raise ReferenceDeviceError("invalid-request", "JFDP frame size does not match its header")
    payload = frame[JFDP_HEADER_BYTES:]
    if (zlib.crc32(payload) & 0xffffffff) != payload_crc32:
        raise ReferenceDeviceError("integrity-failed", "JFDP frame payload CRC does not match")
    return {
        "type": JFDP_MESSAGE_NAMES[type_value],
        "flags": flags,
        "response": bool(flags & JFDP_FRAME_FLAG_RESPONSE),
        "sessionId": session_id,
        "requestId": request_id,
        "payload": payload,
    }


def encode_jfdp_install_begin_payload(transaction_id: int, app_id: str, bundle_bytes: int,
                                      bundle_crc32: int, allow_downgrade: bool) -> bytes:
    app_id_bytes = _jfdp_app_id_bytes(app_id, allow_empty=False)
    if transaction_id <= 0 or not (0 < bundle_bytes <= 0xffffffff) or not (0 <= bundle_crc32 <= 0xffffffff):
        raise ReferenceDeviceError("invalid-request", "invalid install begin payload")
    return struct.pack(
        "<BBBBIII",
        JFDP_PAYLOAD_VERSION,
        1 if allow_downgrade else 0,
        len(app_id_bytes),
        0,
        transaction_id,
        bundle_bytes,
        bundle_crc32,
    ) + app_id_bytes


def decode_jfdp_install_begin_payload(payload: bytes) -> dict:
    if len(payload) < 16:
        raise ReferenceDeviceError("invalid-request", "truncated install begin payload")
    version, flags, app_id_size, reserved, transaction_id, bundle_bytes, bundle_crc32 = struct.unpack(
        "<BBBBIII", payload[:16])
    if version != JFDP_PAYLOAD_VERSION or reserved != 0 or flags & ~1:
        raise ReferenceDeviceError("invalid-request", "invalid install begin payload")
    if not app_id_size or app_id_size > JFDP_MAX_APP_ID_BYTES or len(payload) != 16 + app_id_size:
        raise ReferenceDeviceError("invalid-request", "invalid install begin app id")
    try:
        app_id = payload[16:].decode("ascii")
    except UnicodeDecodeError as error:
        raise ReferenceDeviceError("invalid-request", "install begin app id must be ASCII") from error
    if "\0" in app_id or transaction_id == 0 or bundle_bytes == 0:
        raise ReferenceDeviceError("invalid-request", "invalid install begin transaction")
    return {
        "transactionId": transaction_id,
        "appId": app_id,
        "bundleBytes": bundle_bytes,
        "bundleCrc32": bundle_crc32,
        "allowDowngrade": bool(flags & 1),
    }


def encode_jfdp_install_chunk_payload(transaction_id: int, offset: int, chunk: bytes) -> bytes:
    if transaction_id <= 0 or offset < 0 or not chunk or len(chunk) > JFDP_MAX_PAYLOAD_BYTES - 12:
        raise ReferenceDeviceError("invalid-request", "invalid install chunk payload")
    return struct.pack("<BBHII", JFDP_PAYLOAD_VERSION, 0, len(chunk), transaction_id, offset) + chunk


def decode_jfdp_install_chunk_payload(payload: bytes) -> dict:
    if len(payload) < 12:
        raise ReferenceDeviceError("invalid-request", "truncated install chunk payload")
    version, reserved, chunk_size, transaction_id, offset = struct.unpack("<BBHII", payload[:12])
    if version != JFDP_PAYLOAD_VERSION or reserved != 0 or not chunk_size or len(payload) != 12 + chunk_size:
        raise ReferenceDeviceError("invalid-request", "invalid install chunk payload")
    if transaction_id == 0:
        raise ReferenceDeviceError("invalid-request", "invalid install chunk transaction")
    return {"transactionId": transaction_id, "offset": offset, "chunk": payload[12:]}


def encode_jfdp_transaction_payload(transaction_id: int) -> bytes:
    if transaction_id <= 0:
        raise ReferenceDeviceError("invalid-request", "transaction id must be positive")
    return struct.pack("<BBBBI", JFDP_PAYLOAD_VERSION, 0, 0, 0, transaction_id)


def decode_jfdp_transaction_payload(payload: bytes) -> int:
    if len(payload) != 8:
        raise ReferenceDeviceError("invalid-request", "invalid transaction payload size")
    version, zero_a, zero_b, zero_c, transaction_id = struct.unpack("<BBBBI", payload)
    if version != JFDP_PAYLOAD_VERSION or zero_a or zero_b or zero_c or transaction_id == 0:
        raise ReferenceDeviceError("invalid-request", "invalid transaction payload")
    return transaction_id


def encode_jfdp_app_id_payload(app_id: str) -> bytes:
    app_id_bytes = _jfdp_app_id_bytes(app_id, allow_empty=False)
    return bytes((JFDP_PAYLOAD_VERSION, len(app_id_bytes), 0, 0)) + app_id_bytes


def decode_jfdp_app_id_payload(payload: bytes) -> str:
    if len(payload) < 4 or payload[0] != JFDP_PAYLOAD_VERSION or payload[2] or payload[3]:
        raise ReferenceDeviceError("invalid-request", "invalid app id payload")
    app_id_size = payload[1]
    if not app_id_size or app_id_size > JFDP_MAX_APP_ID_BYTES or len(payload) != 4 + app_id_size:
        raise ReferenceDeviceError("invalid-request", "invalid app id payload")
    try:
        app_id = payload[4:].decode("ascii")
    except UnicodeDecodeError as error:
        raise ReferenceDeviceError("invalid-request", "app id must be ASCII") from error
    if "\0" in app_id:
        raise ReferenceDeviceError("invalid-request", "app id must not contain NUL")
    return app_id


def encode_jfdp_logs_request_payload(app_id: str, limit: int) -> bytes:
    app_id_bytes = _jfdp_app_id_bytes(app_id, allow_empty=True)
    if not (0 < limit <= 0xffff):
        raise ReferenceDeviceError("invalid-request", "invalid log request limit")
    return struct.pack("<BBH", JFDP_PAYLOAD_VERSION, len(app_id_bytes), limit) + app_id_bytes


def decode_jfdp_logs_request_payload(payload: bytes) -> dict:
    if len(payload) < 4:
        raise ReferenceDeviceError("invalid-request", "truncated logs request payload")
    version, app_id_size, limit = struct.unpack("<BBH", payload[:4])
    if version != JFDP_PAYLOAD_VERSION or app_id_size > JFDP_MAX_APP_ID_BYTES or not limit or len(payload) != 4 + app_id_size:
        raise ReferenceDeviceError("invalid-request", "invalid logs request payload")
    try:
        app_id = payload[4:].decode("ascii")
    except UnicodeDecodeError as error:
        raise ReferenceDeviceError("invalid-request", "logs app id must be ASCII") from error
    if "\0" in app_id:
        raise ReferenceDeviceError("invalid-request", "logs app id must not contain NUL")
    return {"appId": app_id, "limit": limit}


def encode_jfdp_operation_result(result_code: str, *, flags: int = 0, transaction_id: int = 0,
                                 received_bytes: int = 0, expected_bytes: int = 0) -> bytes:
    if result_code not in JFDP_RESULT_CODES or not all(0 <= value <= 0xffffffff
                                                        for value in (transaction_id, received_bytes, expected_bytes)):
        raise ReferenceDeviceError("invalid-request", "invalid JFDP operation result")
    return struct.pack("<BBHIII", JFDP_PAYLOAD_VERSION, JFDP_RESULT_CODES[result_code], flags,
                       transaction_id, received_bytes, expected_bytes)


def decode_jfdp_operation_result(payload: bytes) -> dict:
    if len(payload) != 16:
        raise ReferenceDeviceError("invalid-request", "invalid JFDP operation result size")
    version, result_value, flags, transaction_id, received_bytes, expected_bytes = struct.unpack("<BBHIII", payload)
    if version != JFDP_PAYLOAD_VERSION or result_value not in JFDP_RESULT_NAMES:
        raise ReferenceDeviceError("invalid-request", "invalid JFDP operation result")
    return {
        "resultCode": JFDP_RESULT_NAMES[result_value],
        "flags": flags,
        "transactionId": transaction_id,
        "receivedBytes": received_bytes,
        "expectedBytes": expected_bytes,
    }


def _jfdp_result_response(frame: dict, result_code: str, *, flags: int = 0, transaction_id: int = 0,
                          received_bytes: int = 0, expected_bytes: int = 0) -> bytes:
    return encode_jfdp_frame(
        frame["type"],
        frame["sessionId"],
        frame["requestId"],
        encode_jfdp_operation_result(
            result_code,
            flags=flags,
            transaction_id=transaction_id,
            received_bytes=received_bytes,
            expected_bytes=expected_bytes,
        ),
        response=True,
    )


def _jfdp_reference_capabilities() -> bytes:
    board_id = b"reference-no-device"
    runtime_version = b"0.6.0-dev"
    return bytes((JFDP_PAYLOAD_VERSION, len(board_id), len(runtime_version), 0)) + struct.pack(
        "<HHIII", 0, 0, 0, app_registry.DEFAULT_MAX_BUNDLE_BYTES, 0) + board_id + runtime_version


def dispatch_jfdp_frame(store: Path, request_frame: bytes) -> bytes:
    """Dispatch one bounded JFDP/1 request against the desktop reference host.

    The dispatcher intentionally exposes only typed frame payloads and result
    envelopes. Registry entries and log records remain CLI/reference-host data,
    not an accidental JSON wire format.
    """
    frame = decode_jfdp_frame(request_frame)
    if frame["response"] or frame["flags"] & ~JFDP_FRAME_FLAG_RESPONSE:
        raise ReferenceDeviceError("invalid-request", "reference endpoint accepts request frames only")
    try:
        if frame["type"] == "discovery":
            if frame["payload"]:
                raise ReferenceDeviceError("invalid-request", "discovery request payload must be empty")
            return encode_jfdp_frame(frame["type"], frame["sessionId"], frame["requestId"],
                                     _jfdp_reference_capabilities(), response=True)
        if frame["type"] == "app-list":
            if frame["payload"]:
                raise ReferenceDeviceError("invalid-request", "app list request payload must be empty")
            return _jfdp_result_response(frame, "ok")
        if frame["type"] == "install-begin":
            request = decode_jfdp_install_begin_payload(frame["payload"])
            begin_install(store, request["transactionId"], request["appId"], request["bundleBytes"],
                          request["bundleCrc32"], request["allowDowngrade"])
            return _jfdp_result_response(frame, "accepted", transaction_id=request["transactionId"],
                                         expected_bytes=request["bundleBytes"])
        if frame["type"] == "install-chunk":
            request = decode_jfdp_install_chunk_payload(frame["payload"])
            progress = append_chunk(store, request["transactionId"], request["offset"], request["chunk"])
            flags = JFDP_RESULT_COMPLETE if progress["complete"] else 0
            return _jfdp_result_response(frame, "accepted", flags=flags,
                                         transaction_id=request["transactionId"],
                                         received_bytes=progress["receivedBytes"],
                                         expected_bytes=progress["expectedBytes"])
        if frame["type"] == "install-commit":
            transaction_id = decode_jfdp_transaction_payload(frame["payload"])
            result = commit_install(store, transaction_id)
            reference = result["referenceTransaction"]
            return _jfdp_result_response(frame, "ok", flags=JFDP_RESULT_COMPLETE,
                                         transaction_id=transaction_id,
                                         received_bytes=reference["receivedBytes"],
                                         expected_bytes=reference["expectedBytes"])
        if frame["type"] == "install-abort":
            transaction_id = decode_jfdp_transaction_payload(frame["payload"])
            result = cancel(store, transaction_id)
            return _jfdp_result_response(frame, "cancelled", transaction_id=transaction_id,
                                         received_bytes=result["receivedBytes"],
                                         expected_bytes=result["expectedBytes"])
        if frame["type"] == "launch":
            app_id = decode_jfdp_app_id_payload(frame["payload"])
            launch(store, app_id)
            return _jfdp_result_response(frame, "ok", flags=JFDP_RESULT_ACTIVE)
        if frame["type"] == "stop":
            app_id = decode_jfdp_app_id_payload(frame["payload"])
            stop(store, app_id)
            return _jfdp_result_response(frame, "ok", flags=JFDP_RESULT_LAUNCHER_ACTIVE)
        if frame["type"] == "remove":
            app_id = decode_jfdp_app_id_payload(frame["payload"])
            remove(store, app_id, keep_data=False)
            return _jfdp_result_response(frame, "ok")
        if frame["type"] == "rollback":
            app_id = decode_jfdp_app_id_payload(frame["payload"])
            rollback(store, app_id)
            return _jfdp_result_response(frame, "ok")
        if frame["type"] == "logs":
            request = decode_jfdp_logs_request_payload(frame["payload"])
            logs(store, request["appId"] or None, request["limit"])
            return _jfdp_result_response(frame, "ok")
        if frame["type"] == "recovery":
            if frame["payload"]:
                raise ReferenceDeviceError("invalid-request", "recovery request payload must be empty")
            state = recovery(store)
            return _jfdp_result_response(
                frame,
                "ok",
                flags=JFDP_RESULT_LAUNCHER_ACTIVE if state["launcherActive"] else JFDP_RESULT_ACTIVE,
            )
        raise ReferenceDeviceError("unsupported", f"unsupported JFDP message: {frame['type']}")
    except ReferenceDeviceError as error:
        result_code = error.result_code if error.result_code in JFDP_RESULT_CODES else "failed"
        record_failure(store, frame["type"], result_code, str(error))
        return _jfdp_result_response(frame, result_code)
