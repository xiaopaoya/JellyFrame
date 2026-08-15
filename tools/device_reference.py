"""Desktop-only JFDP/1 reference-host control semantics.

This module is deliberately not a physical transport. It gives the CLI a
durable reference implementation for the bounded install/lifecycle operations
that a board transport must later implement against the same protocol contract.
"""

from __future__ import annotations

import json
import zlib
from pathlib import Path

import app_registry


REFERENCE_FORMAT_VERSION = 0
REFERENCE_DIRECTORY = ".jfdp_reference"
MAX_REFERENCE_LOG_ENTRIES = 256
DEFAULT_CHUNK_BYTES = 1024
MAX_CHUNK_BYTES = 4096


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


def start_install(store: Path, bundle_path: Path, allow_downgrade: bool) -> tuple[dict, bytes]:
    bundle, bundle_info = read_bundle(bundle_path)
    state = load_runtime_state(store)
    transaction_id = state["nextTransactionId"]
    state["nextTransactionId"] = transaction_id + 1
    save_runtime_state(store, state)

    transactions_root(store).mkdir(parents=True, exist_ok=True)
    staging_path(store, transaction_id).write_bytes(b"")
    summary = bundle_info["summary"]
    record = {
        "format": "jellyframe.device_reference.transaction",
        "formatVersion": REFERENCE_FORMAT_VERSION,
        "transactionId": transaction_id,
        "appId": summary["id"],
        "bundleBytes": len(bundle),
        "bundleCrc32": zlib.crc32(bundle) & 0xffffffff,
        "receivedBytes": 0,
        "allowDowngrade": allow_downgrade,
    }
    save_transaction(store, record)
    append_log(store, summary["id"], "install-begin", f"transaction={transaction_id}")
    return record, bundle


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
    path = staging_path(store, transaction_id)
    with path.open("ab") as output:
        while record["receivedBytes"] < record["bundleBytes"]:
            if pause_after_chunks is not None and chunks_written >= pause_after_chunks:
                break
            offset = record["receivedBytes"]
            size = min(chunk_bytes, record["bundleBytes"] - offset)
            output.write(bundle[offset:offset + size])
            record["receivedBytes"] += size
            chunks_written += 1
    save_transaction(store, record)
    return {
        "transactionId": transaction_id,
        "phase": "receiving",
        "receivedBytes": record["receivedBytes"],
        "expectedBytes": record["bundleBytes"],
        "chunkCount": chunks_written,
        "complete": record["receivedBytes"] == record["bundleBytes"],
    }


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
