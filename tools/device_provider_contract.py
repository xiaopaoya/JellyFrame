"""Strict parser for the future Device OS tool-provider boundary.

This module validates host-process results only. It neither opens a transport
nor discovers a device, so Runtime tooling can consume a provider without
pulling board dependencies into this repository.
"""

from __future__ import annotations

import json
from typing import Any


FORMAT = "jellyframe.device-provider"
FORMAT_VERSION = 0
MAX_DOCUMENT_BYTES = 64 * 1024
MAX_LOG_RECORDS = 256
MAX_REQUEST_ID_BYTES = 64
OPERATIONS = frozenset({"discover", "info", "install", "launch", "stop", "remove", "rollback", "logs", "recovery"})
RESULT_CODES = frozenset({"ok", "accepted", "queued", "invalid-request", "busy", "unsupported", "denied", "not-found", "stale-session", "stale-request", "payload-too-large", "integrity-failed", "storage-full", "cancelled", "failed", "transport-unavailable", "protocol-mismatch", "provider-failed"})


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
    if (not isinstance(capabilities["featureFamilies"], list) or
            any(not isinstance(item, str) or not item for item in capabilities["featureFamilies"])):
        raise ProviderContractError(f"{name}.capabilities.featureFamilies is invalid")
    for field in ("maxBundleBytes", "availableStorageBytes"):
        if not isinstance(capabilities[field], int) or capabilities[field] < 0:
            raise ProviderContractError(f"{name}.capabilities.{field} is invalid")


def parse_provider_result(data: bytes | str) -> dict[str, Any]:
    raw = data.encode("utf-8") if isinstance(data, str) else data
    if not isinstance(raw, bytes) or len(raw) > MAX_DOCUMENT_BYTES:
        raise ProviderContractError("provider result exceeds the document budget")
    try:
        result = json.loads(raw.decode("utf-8"), object_pairs_hook=_no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ProviderContractError(f"invalid provider JSON: {error}") from error
    result = _object(result, "result")
    allowed = {"format", "formatVersion", "kind", "operation", "requestId", "resultCode", "provider", "devices", "device", "message", "transaction", "progress", "logs", "recovery"}
    required = {"format", "formatVersion", "kind", "operation", "requestId", "resultCode", "provider"}
    if not required.issubset(result) or not set(result).issubset(allowed):
        raise ProviderContractError("provider result has unknown or missing fields")
    if result["format"] != FORMAT or result["formatVersion"] != FORMAT_VERSION or result["kind"] != "result":
        raise ProviderContractError("unsupported provider result format")
    if result["operation"] not in OPERATIONS or result["resultCode"] not in RESULT_CODES:
        raise ProviderContractError("unsupported provider operation or result code")
    request_id = _string(result["requestId"], "requestId", MAX_REQUEST_ID_BYTES)
    if not request_id.isascii():
        raise ProviderContractError("requestId must be ASCII")
    provider = _object(result["provider"], "provider")
    if set(provider) != {"id", "version"}:
        raise ProviderContractError("provider has unknown or missing fields")
    _string(provider["id"], "provider.id")
    _string(provider["version"], "provider.version")
    if "devices" in result:
        if not isinstance(result["devices"], list) or len(result["devices"]) > 32:
            raise ProviderContractError("devices is invalid")
        for index, device in enumerate(result["devices"]):
            _validate_device(device, f"devices[{index}]")
    if "device" in result:
        _validate_device(result["device"], "device")
    if "logs" in result and (not isinstance(result["logs"], list) or len(result["logs"]) > MAX_LOG_RECORDS):
        raise ProviderContractError("logs is invalid")
    return result
