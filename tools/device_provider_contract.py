"""Strict parser for the future Device OS tool-provider boundary.

This module validates host-process results only. It neither opens a transport
nor discovers a device, so Runtime tooling can consume a provider without
pulling board dependencies into this repository.
"""

from __future__ import annotations

import json
import re
from typing import Any


FORMAT = "jellyframe.device-provider"
FORMAT_VERSION = 0
MAX_DOCUMENT_BYTES = 64 * 1024
MAX_JSONL_BYTES = 256 * 1024
MAX_JSONL_EVENTS = 1024
MAX_LOG_RECORDS = 256
MAX_APP_LIST_ENTRIES = 24
MAX_REQUEST_ID_BYTES = 64
MAX_FEATURE_FAMILIES = 64
OPERATIONS = frozenset({"discover", "info", "list", "install", "cancel", "launch", "stop", "remove", "rollback", "logs", "recovery"})
RESULT_CODES = frozenset({"ok", "accepted", "queued", "invalid-request", "busy", "unsupported", "denied", "not-found", "stale-session", "stale-request", "payload-too-large", "integrity-failed", "storage-full", "cancelled", "failed", "transport-unavailable", "protocol-mismatch", "provider-failed"})
FEATURE_FAMILY_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{0,95}$")
LOG_LEVELS = frozenset({"debug", "info", "warn", "error"})
APP_LIBRARY_STATES = frozenset({"installed", "disabled", "failed"})
RECOVERY_REASONS = frozenset({"none", "registry-invalid", "staging-discarded", "app-load-failure", "app-runtime-failure", "app-budget-exceeded", "launcher-fallback"})


class ProviderContractError(ValueError):
    pass


def _no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProviderContractError(f"duplicate JSON member: {key}")
        result[key] = value
    return result


def _string(value: Any, name: str, maximum: int = 256) -> str:
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > maximum:
        raise ProviderContractError(f"{name} must be a non-empty UTF-8 string no longer than {maximum} bytes")
    return value


def _object(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProviderContractError(f"{name} must be an object")
    return value


def _positive_uint32(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 < value <= 0xFFFFFFFF:
        raise ProviderContractError(f"{name} must be a positive uint32")
    return value


def _uint32(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFFFFFF:
        raise ProviderContractError(f"{name} must be a uint32")
    return value


def _optional_string(value: Any, name: str, maximum: int = 256) -> str:
    if not isinstance(value, str) or len(value.encode("utf-8")) > maximum:
        raise ProviderContractError(f"{name} must be a UTF-8 string no longer than {maximum} bytes")
    return value


def _provider(value: Any) -> dict[str, Any]:
    provider = _object(value, "provider")
    if set(provider) != {"id", "version"}:
        raise ProviderContractError("provider has unknown or missing fields")
    _string(provider["id"], "provider.id")
    _string(provider["version"], "provider.version")
    return provider


def _envelope(value: Any, name: str, kind: str, require_sequence: bool) -> dict[str, Any]:
    event = _object(value, name)
    required = {"format", "formatVersion", "kind", "operation", "requestId", "provider"}
    if require_sequence:
        required.add("sequence")
    if not required.issubset(event):
        raise ProviderContractError(f"{name} has missing fields")
    if event["format"] != FORMAT or event["formatVersion"] != FORMAT_VERSION or event["kind"] != kind:
        raise ProviderContractError(f"unsupported {name} format")
    if event["operation"] not in OPERATIONS:
        raise ProviderContractError(f"unsupported {name} operation")
    request_id = _string(event["requestId"], "requestId", MAX_REQUEST_ID_BYTES)
    if not request_id.isascii():
        raise ProviderContractError("requestId must be ASCII")
    _provider(event["provider"])
    if require_sequence:
        _positive_uint32(event["sequence"], "sequence")
    return event


def _validate_device(value: Any, name: str) -> None:
    device = _object(value, name)
    required = ("endpointId", "boardId", "profileId", "imageVersion", "runtimeVersion", "protocol", "connected", "capabilities")
    if set(device) != set(required):
        raise ProviderContractError(f"{name} has unknown or missing fields")
    for field in required[:6]:
        _string(device[field], f"{name}.{field}")
    if device["protocol"] != "JFDP/1" or not isinstance(device["connected"], bool):
        raise ProviderContractError(f"{name} has invalid protocol or connection state")
    capabilities = _object(device["capabilities"], f"{name}.capabilities")
    expected = {"display", "featureFamilies", "maxBundleBytes", "availableStorageBytes"}
    if set(capabilities) != expected:
        raise ProviderContractError(f"{name}.capabilities has unknown or missing fields")
    display = _object(capabilities["display"], f"{name}.capabilities.display")
    if set(display) != {"width", "height", "shape"} or not all(isinstance(display[key], int) and display[key] > 0 for key in ("width", "height")):
        raise ProviderContractError(f"{name}.capabilities.display is invalid")
    _string(display["shape"], f"{name}.capabilities.display.shape", 32)
    feature_families = capabilities["featureFamilies"]
    if (not isinstance(feature_families, list) or len(feature_families) > MAX_FEATURE_FAMILIES or
            len(set(feature_families)) != len(feature_families)):
        raise ProviderContractError(f"{name}.capabilities.featureFamilies is invalid")
    for index, family in enumerate(feature_families):
        _string(family, f"{name}.capabilities.featureFamilies[{index}]", 96)
        if FEATURE_FAMILY_RE.fullmatch(family) is None:
            raise ProviderContractError(f"{name}.capabilities.featureFamilies[{index}] is invalid")
    for field in ("maxBundleBytes", "availableStorageBytes"):
        _uint32(capabilities[field], f"{name}.capabilities.{field}")


def _validate_app_library_entry(value: Any, name: str) -> None:
    entry = _object(value, name)
    required = {"appId", "versionName", "versionCode", "bundleBytes", "state", "rollbackAvailable"}
    if set(entry) != required:
        raise ProviderContractError(f"{name} has unknown or missing fields")
    _string(entry["appId"], f"{name}.appId", 96)
    _string(entry["versionName"], f"{name}.versionName", 64)
    _positive_uint32(entry["versionCode"], f"{name}.versionCode")
    _uint32(entry["bundleBytes"], f"{name}.bundleBytes")
    if entry["state"] not in APP_LIBRARY_STATES or not isinstance(entry["rollbackAvailable"], bool):
        raise ProviderContractError(f"{name} has invalid state or rollback availability")


def _validate_logs(value: Any, name: str) -> None:
    if not isinstance(value, list) or len(value) > MAX_LOG_RECORDS:
        raise ProviderContractError(f"{name} is invalid")
    for index, record in enumerate(value):
        _validate_stream_log({
            "format": FORMAT,
            "formatVersion": FORMAT_VERSION,
            "kind": "log",
            "operation": "logs",
            "requestId": "provider-result",
            "sequence": index + 1,
            "provider": {"id": "provider-result", "version": "0"},
            "log": record,
        })


def _validate_transaction(value: Any) -> None:
    transaction = _object(value, "transaction")
    if set(transaction) != {"id", "receivedBytes", "expectedBytes", "complete", "active"}:
        raise ProviderContractError("transaction has unknown or missing fields")
    _positive_uint32(transaction["id"], "transaction.id")
    received = _uint32(transaction["receivedBytes"], "transaction.receivedBytes")
    expected = _uint32(transaction["expectedBytes"], "transaction.expectedBytes")
    if received > expected or not isinstance(transaction["complete"], bool) or not isinstance(transaction["active"], bool):
        raise ProviderContractError("transaction is invalid")


def _validate_progress(value: Any) -> None:
    progress = _object(value, "progress")
    if set(progress) != {"completedBytes", "totalBytes"}:
        raise ProviderContractError("progress has unknown or missing fields")
    completed = _uint32(progress["completedBytes"], "progress.completedBytes")
    total = _positive_uint32(progress["totalBytes"], "progress.totalBytes")
    if completed > total:
        raise ProviderContractError("progress.completedBytes is invalid")


def _validate_recovery(value: Any) -> None:
    recovery = _object(value, "recovery")
    required = {"appId", "registryGeneration", "recoverySequence", "reason", "launcherActive", "appDisabled", "rollbackAvailable"}
    if set(recovery) != required:
        raise ProviderContractError("recovery has unknown or missing fields")
    _optional_string(recovery["appId"], "recovery.appId", 96)
    _uint32(recovery["registryGeneration"], "recovery.registryGeneration")
    _uint32(recovery["recoverySequence"], "recovery.recoverySequence")
    if (recovery["reason"] not in RECOVERY_REASONS or
            not all(isinstance(recovery[field], bool) for field in
                    ("launcherActive", "appDisabled", "rollbackAvailable"))):
        raise ProviderContractError("recovery is invalid")


def _validate_result(value: Any, require_sequence: bool = False) -> dict[str, Any]:
    result = _envelope(value, "result", "result", require_sequence)
    allowed = {"format", "formatVersion", "kind", "operation", "requestId", "resultCode", "provider", "devices", "device", "apps", "registryGeneration", "message", "transaction", "progress", "logs", "recovery", "cancellation"}
    if require_sequence:
        allowed.add("sequence")
    required = {"format", "formatVersion", "kind", "operation", "requestId", "resultCode", "provider"}
    if require_sequence:
        required.add("sequence")
    if not required.issubset(result) or not set(result).issubset(allowed):
        raise ProviderContractError("provider result has unknown or missing fields")
    if result["resultCode"] not in RESULT_CODES:
        raise ProviderContractError("unsupported provider operation or result code")
    if "devices" in result:
        if not isinstance(result["devices"], list) or len(result["devices"]) > 32:
            raise ProviderContractError("devices is invalid")
        for index, device in enumerate(result["devices"]):
            _validate_device(device, f"devices[{index}]")
    if "device" in result:
        _validate_device(result["device"], "device")
    if "apps" in result:
        if not isinstance(result["apps"], list) or len(result["apps"]) > MAX_APP_LIST_ENTRIES:
            raise ProviderContractError("apps is invalid")
        app_ids: set[str] = set()
        for index, app in enumerate(result["apps"]):
            _validate_app_library_entry(app, f"apps[{index}]")
            app_id = app["appId"]
            if app_id in app_ids:
                raise ProviderContractError("apps contains duplicate appId")
            app_ids.add(app_id)
    if "registryGeneration" in result:
        _uint32(result["registryGeneration"], "registryGeneration")
    if ("apps" in result) != ("registryGeneration" in result):
        raise ProviderContractError("apps and registryGeneration must appear together")
    if "message" in result:
        _string(result["message"], "message", 1024)
    if "transaction" in result:
        _validate_transaction(result["transaction"])
    if "progress" in result:
        _validate_progress(result["progress"])
    if "logs" in result:
        _validate_logs(result["logs"], "logs")
    if "recovery" in result:
        _validate_recovery(result["recovery"])
        if (not result["recovery"]["appId"] and
                (result["recovery"]["appDisabled"] or result["recovery"]["rollbackAvailable"])):
            raise ProviderContractError("device-wide recovery cannot carry app state")
    if "cancellation" in result:
        cancellation = _object(result["cancellation"], "cancellation")
        if set(cancellation) != {"confirmed"} or not isinstance(cancellation["confirmed"], bool):
            raise ProviderContractError("cancellation is invalid")
    if result["resultCode"] in {"ok", "accepted"}:
        if result["operation"] == "list" and "apps" not in result:
            raise ProviderContractError("successful list result is missing apps")
        if result["operation"] == "recovery" and "recovery" not in result:
            raise ProviderContractError("successful recovery result is missing recovery")
    return result


def _parse_json(raw: bytes, name: str) -> dict[str, Any]:
    try:
        return _object(json.loads(raw.decode("utf-8"), object_pairs_hook=_no_duplicates), name)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProviderContractError(f"invalid {name} JSON: {error}") from error


def parse_provider_result(data: bytes | str) -> dict[str, Any]:
    raw = data.encode("utf-8") if isinstance(data, str) else data
    if not isinstance(raw, bytes) or len(raw) > MAX_DOCUMENT_BYTES:
        raise ProviderContractError("provider result exceeds the document budget")
    return _validate_result(_parse_json(raw, "provider result"))


def _validate_stream_progress(value: Any) -> dict[str, Any]:
    event = _envelope(value, "progress event", "progress", True)
    if set(event) != {"format", "formatVersion", "kind", "operation", "requestId", "sequence", "provider", "progress"}:
        raise ProviderContractError("progress event has unknown or missing fields")
    progress = _object(event["progress"], "progress")
    if set(progress) != {"completedBytes", "totalBytes"}:
        raise ProviderContractError("progress has unknown or missing fields")
    completed = progress["completedBytes"]
    total = _positive_uint32(progress["totalBytes"], "progress.totalBytes")
    if (not isinstance(completed, int) or isinstance(completed, bool) or
            not 0 <= completed <= total):
        raise ProviderContractError("progress.completedBytes is invalid")
    return event


def _validate_stream_log(value: Any) -> dict[str, Any]:
    event = _envelope(value, "log event", "log", True)
    if set(event) != {"format", "formatVersion", "kind", "operation", "requestId", "sequence", "provider", "log"}:
        raise ProviderContractError("log event has unknown or missing fields")
    log = _object(event["log"], "log")
    if set(log) != {"level", "appId", "message"} or log["level"] not in LOG_LEVELS:
        raise ProviderContractError("log has unknown or missing fields")
    _string(log["appId"], "log.appId", 96)
    _string(log["message"], "log.message", 1024)
    return event


def parse_provider_jsonl(data: bytes | str) -> list[dict[str, Any]]:
    """Parse one bounded provider operation stream with one ordered terminal result."""
    raw = data.encode("utf-8") if isinstance(data, str) else data
    if not isinstance(raw, bytes) or not raw or len(raw) > MAX_JSONL_BYTES:
        raise ProviderContractError("provider JSONL exceeds the stream budget")
    lines = raw.splitlines()
    if not lines or len(lines) > MAX_JSONL_EVENTS or any(not line.strip() for line in lines):
        raise ProviderContractError("provider JSONL has invalid line boundaries")
    events: list[dict[str, Any]] = []
    previous_sequence = 0
    stream_identity: tuple[str, str, str, str] | None = None
    for index, line in enumerate(lines):
        event = _parse_json(line, f"provider JSONL line {index + 1}")
        kind = event.get("kind")
        if kind == "progress":
            event = _validate_stream_progress(event)
        elif kind == "log":
            event = _validate_stream_log(event)
        elif kind == "result":
            event = _validate_result(event, require_sequence=True)
        else:
            raise ProviderContractError("provider JSONL has an unsupported event kind")
        sequence = event["sequence"]
        if sequence <= previous_sequence:
            raise ProviderContractError("provider JSONL sequence is not strictly increasing")
        previous_sequence = sequence
        provider = event["provider"]
        identity = (event["operation"], event["requestId"], provider["id"], provider["version"])
        if stream_identity is None:
            stream_identity = identity
        elif identity != stream_identity:
            raise ProviderContractError("provider JSONL stream identity changed")
        if kind == "result" and index != len(lines) - 1:
            raise ProviderContractError("provider JSONL terminal result is not final")
        events.append(event)
    if events[-1]["kind"] != "result":
        raise ProviderContractError("provider JSONL is missing a terminal result")
    return events
