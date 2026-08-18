"""Strict Developer Image manifest contract shared by Device OS and author tools.

The manifest describes one immutable, flashable Device OS image. It deliberately
does not open a transport, discover a board, or replace the JFDP provider.
"""

from __future__ import annotations

import json
import re
from typing import Any


FORMAT = "jellyframe.device-image"
FORMAT_VERSION = 0
MAX_DOCUMENT_BYTES = 16 * 1024
MAX_FEATURE_FAMILIES = 64

_ID_RE = re.compile(r"^[a-z0-9][a-z0-9.-]{0,95}$")
_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
_HEX40_RE = re.compile(r"^[0-9a-f]{40}$")
_HEX64_RE = re.compile(r"^[0-9a-f]{64}$")


class DeviceImageManifestError(ValueError):
    pass


def _no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DeviceImageManifestError(f"duplicate JSON member: {key}")
        result[key] = value
    return result


def _object(value: Any, name: str, required: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != required:
        raise DeviceImageManifestError(f"{name} has unknown or missing fields")
    return value


def _string(value: Any, name: str, pattern: re.Pattern[str] | None = None) -> str:
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > 256:
        raise DeviceImageManifestError(f"{name} must be a bounded non-empty UTF-8 string")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise DeviceImageManifestError(f"{name} has an invalid format")
    return value


def _positive_int(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0 or value > 0xFFFFFFFF:
        raise DeviceImageManifestError(f"{name} must be a positive uint32")
    return value


def parse_device_image_manifest(data: bytes | str) -> dict[str, Any]:
    raw = data.encode("utf-8") if isinstance(data, str) else data
    if not isinstance(raw, bytes) or len(raw) > MAX_DOCUMENT_BYTES:
        raise DeviceImageManifestError("manifest exceeds the document budget")
    try:
        manifest = json.loads(raw.decode("utf-8"), object_pairs_hook=_no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DeviceImageManifestError(f"invalid manifest JSON: {error}") from error

    manifest = _object(manifest, "manifest", {
        "format", "formatVersion", "imageId", "imageVersion", "runtimeVersion",
        "renderCore", "source", "board", "profile", "transport", "storage", "recovery",
    })
    if manifest["format"] != FORMAT or manifest["formatVersion"] != FORMAT_VERSION:
        raise DeviceImageManifestError("unsupported manifest format")
    for field in ("imageId",):
        _string(manifest[field], field, _ID_RE)
    _string(manifest["imageVersion"], "imageVersion", _VERSION_RE)
    _string(manifest["runtimeVersion"], "runtimeVersion", _VERSION_RE)

    render_core = _object(manifest["renderCore"], "renderCore", {"version", "abi"})
    _string(render_core["version"], "renderCore.version", _VERSION_RE)
    _positive_int(render_core["abi"], "renderCore.abi")

    source = _object(manifest["source"], "source", {"revision", "firmwareSha256"})
    _string(source["revision"], "source.revision", _HEX40_RE)
    _string(source["firmwareSha256"], "source.firmwareSha256", _HEX64_RE)

    board = _object(manifest["board"], "board", {"id", "display"})
    _string(board["id"], "board.id", _ID_RE)
    display = _object(board["display"], "board.display", {"width", "height", "shape"})
    _positive_int(display["width"], "board.display.width")
    _positive_int(display["height"], "board.display.height")
    _string(display["shape"], "board.display.shape", _ID_RE)

    profile = _object(manifest["profile"], "profile", {"id", "featureFamilies"})
    _string(profile["id"], "profile.id", _ID_RE)
    families = profile["featureFamilies"]
    if (not isinstance(families, list) or not families or len(families) > MAX_FEATURE_FAMILIES or
            len(set(families)) != len(families)):
        raise DeviceImageManifestError("profile.featureFamilies is invalid")
    for index, family in enumerate(families):
        _string(family, f"profile.featureFamilies[{index}]", _ID_RE)

    transport = _object(manifest["transport"], "transport", {"protocol", "kind"})
    if transport["protocol"] != "JFDP/1":
        raise DeviceImageManifestError("transport.protocol must be JFDP/1")
    _string(transport["kind"], "transport.kind", _ID_RE)

    storage = _object(manifest["storage"], "storage", {"maxBundleBytes"})
    _positive_int(storage["maxBundleBytes"], "storage.maxBundleBytes")

    recovery = _object(manifest["recovery"], "recovery", {"procedureId", "factoryImageSha256"})
    _string(recovery["procedureId"], "recovery.procedureId", _ID_RE)
    _string(recovery["factoryImageSha256"], "recovery.factoryImageSha256", _HEX64_RE)
    return manifest


def validate_provider_device(manifest: dict[str, Any], device: dict[str, Any]) -> None:
    """Reject a discovered physical endpoint that cannot run this exact image."""
    try:
        board = manifest["board"]
        display = board["display"]
        profile = manifest["profile"]
        capabilities = device["capabilities"]
        provider_display = capabilities["display"]
    except (KeyError, TypeError) as error:
        raise DeviceImageManifestError("manifest or provider device has not been validated") from error
    expected = {
        "boardId": board["id"],
        "profileId": profile["id"],
        "imageVersion": manifest["imageVersion"],
        "runtimeVersion": manifest["runtimeVersion"],
        "protocol": manifest["transport"]["protocol"],
    }
    for field, value in expected.items():
        if device.get(field) != value:
            raise DeviceImageManifestError(f"provider device {field} does not match image manifest")
    if provider_display != display:
        raise DeviceImageManifestError("provider device display does not match image manifest")
    if capabilities.get("maxBundleBytes") != manifest["storage"]["maxBundleBytes"]:
        raise DeviceImageManifestError("provider device maxBundleBytes does not match image manifest")
    if set(capabilities.get("featureFamilies", ())) != set(profile["featureFamilies"]):
        raise DeviceImageManifestError("provider device feature families do not match image manifest")
