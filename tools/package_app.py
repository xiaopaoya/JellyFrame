#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import re
import struct
import zlib
from pathlib import Path, PurePosixPath


JFAPP_MAGIC = b"JFAPPV0\0"
JFAPP_HEADER_FORMAT = "<8sHHIIIIIIIIIII"
JFAPP_HEADER_SIZE = struct.calcsize(JFAPP_HEADER_FORMAT)
JFAPP_ENTRY_FORMAT = "<IIHHIIII"
JFAPP_ENTRY_SIZE = struct.calcsize(JFAPP_ENTRY_FORMAT)
JFAPP_ALIGNMENT = 4
JFFONT_MAGIC = b"JFFONT0\0"
JFFONT_HEADER_SIZE = 32
JFFONT_GLYPH_ENTRY_SIZE = 16

KIND_BY_SUFFIX = {
    ".css": "jellyframe::HostResourceKind::Stylesheet",
    ".js": "jellyframe::HostResourceKind::ClassicScript",
    ".png": "jellyframe::HostResourceKind::Image",
    ".jpg": "jellyframe::HostResourceKind::Image",
    ".jpeg": "jellyframe::HostResourceKind::Image",
    ".gif": "jellyframe::HostResourceKind::Image",
    ".webp": "jellyframe::HostResourceKind::Image",
    ".bmp": "jellyframe::HostResourceKind::Image",
    ".bdf": "jellyframe::HostResourceKind::Font",
    ".fnt": "jellyframe::HostResourceKind::Font",
    ".jffont": "jellyframe::HostResourceKind::Font",
    ".ttf": "jellyframe::HostResourceKind::Font",
    ".otf": "jellyframe::HostResourceKind::Font",
    ".woff": "jellyframe::HostResourceKind::Font",
    ".woff2": "jellyframe::HostResourceKind::Font",
}

BUNDLE_KIND_BY_RESOURCE_KIND = {
    "jellyframe::HostResourceKind::Other": 0,
    "jellyframe::HostResourceKind::Stylesheet": 1,
    "jellyframe::HostResourceKind::ClassicScript": 2,
    "jellyframe::HostResourceKind::Image": 3,
    "jellyframe::HostResourceKind::Font": 4,
}


def fail(message: str) -> None:
    raise SystemExit(f"jellyframe_package_app: {message}")


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def presets_dir() -> Path:
    return repo_root() / "tools" / "presets" / "targets"


def normalize_app_path(value: str) -> str:
    if not value or "://" in value or value.startswith("//"):
        fail(f"resource path must be local: {value!r}")
    raw = value.replace("\\", "/")
    if not raw.startswith("/"):
        raw = "/" + raw
    parts = []
    for part in raw.split("/"):
        if not part or part == ".":
            continue
        if part == "..":
            if not parts:
                fail(f"resource path escapes app root: {value!r}")
            parts.pop()
            continue
        parts.append(part)
    return "/" + "/".join(parts)


def local_path_for(root: Path, app_path: str) -> Path:
    relative = PurePosixPath(app_path.lstrip("/"))
    return root.joinpath(*relative.parts)


def resource_kind(path: Path) -> str:
    return KIND_BY_SUFFIX.get(path.suffix.lower(), "jellyframe::HostResourceKind::Other")


def cpp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cpp_symbol(index: int) -> str:
    return f"kJellyFrameResource{index}"


def emit_byte_array(name: str, data: bytes) -> str:
    lines = [f"constexpr std::uint8_t {name}[] = {{"]
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def resource_kind_name(kind: str) -> str:
    return kind.split("::")[-1]


GENERIC_FONT_FAMILIES = {
    "serif",
    "sans-serif",
    "monospace",
    "cursive",
    "fantasy",
    "system-ui",
    "ui-serif",
    "ui-sans-serif",
    "ui-monospace",
    "ui-rounded",
    "emoji",
    "math",
    "fangsong",
}


def fnv1a_32(value: str) -> int:
    result = 0x811c9dc5
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 0x01000193) & 0xffffffff
    return result


def align_up(value: int, alignment: int = JFAPP_ALIGNMENT) -> int:
    return (value + alignment - 1) // alignment * alignment


def append_padding(data: bytearray, alignment: int = JFAPP_ALIGNMENT) -> None:
    data.extend(b"\0" * (align_up(len(data), alignment) - len(data)))


def read_manifest(root: Path) -> dict:
    manifest_path = root / "jellyframe.app.json"
    if not manifest_path.is_file():
        fail("missing jellyframe.app.json")
    try:
        return json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        fail(f"invalid manifest JSON: {error}")


def int_field(mapping: dict, key: str, default: int = 0) -> int:
    value = mapping.get(key, default)
    return value if isinstance(value, int) else default


def normalized_int_list(value, minimum: int, maximum: int | None = None) -> list[int]:
    if not isinstance(value, list):
        return []
    result = []
    for item in value:
        if isinstance(item, bool) or not isinstance(item, int):
            continue
        if item < minimum:
            continue
        if maximum is not None and item > maximum:
            continue
        result.append(item)
    return result


def bool_field(mapping: dict, key: str, default: bool = False) -> bool:
    value = mapping.get(key, default)
    return value if isinstance(value, bool) else default


def strip_css_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def split_css_top_level(value: str, separator: str) -> list[str]:
    parts = []
    begin = 0
    depth = 0
    quote = ""
    index = 0
    while index < len(value):
        ch = value[index]
        if quote:
            if ch == "\\":
                index += 2
                continue
            if ch == quote:
                quote = ""
        elif ch in {"'", '"'}:
            quote = ch
        elif ch == "(":
            depth += 1
        elif ch == ")" and depth > 0:
            depth -= 1
        elif ch == separator and depth == 0:
            parts.append(value[begin:index].strip())
            begin = index + 1
        index += 1
    tail = value[begin:].strip()
    if tail:
        parts.append(tail)
    return parts


def normalize_font_family_name(value: str) -> str:
    family = value.strip()
    if len(family) >= 2 and family[0] == family[-1] and family[0] in {"'", '"'}:
        family = family[1:-1]
    family = re.sub(r"\s+", " ", family.strip())
    return family


def collect_font_family_usage(resources: list[dict], manifest_fonts: list[dict]) -> dict:
    manifest_by_family = {}
    for font in manifest_fonts:
        family = font.get("family", "") if isinstance(font, dict) else ""
        normalized = normalize_font_family_name(family).lower()
        if normalized:
            manifest_by_family[normalized] = {
                "id": font.get("id", ""),
                "source": font.get("source", ""),
                "family": normalize_font_family_name(family),
            }

    entries = []
    seen = set()
    for resource in resources:
        kind = resource_kind_name(resource["kind"])
        suffix = resource["file"].suffix.lower()
        if kind != "Stylesheet" and suffix not in {".html", ".htm"}:
            continue
        try:
            text = resource["file"].read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            continue
        css_sources = []
        if kind == "Stylesheet":
            css_sources.append(text)
        else:
            css_sources.extend(match.group(1) for match in re.finditer(r"<style[^>]*>(.*?)</style>", text, flags=re.I | re.S))
            css_sources.extend(match.group(1) for match in re.finditer(r"style\s*=\s*\"([^\"]*)\"", text, flags=re.I | re.S))
            css_sources.extend(match.group(1) for match in re.finditer(r"style\s*=\s*'([^']*)'", text, flags=re.I | re.S))

        for css_text in css_sources:
            for match in re.finditer(r"font-family\s*:\s*([^;{}]+)", strip_css_comments(css_text), flags=re.I):
                for index, family in enumerate(split_css_top_level(match.group(1), ",")):
                    normalized = normalize_font_family_name(family)
                    if not normalized:
                        continue
                    lowered = normalized.lower()
                    key = (resource["path"], lowered)
                    if key in seen:
                        continue
                    seen.add(key)
                    manifest_font = manifest_by_family.get(lowered)
                    if lowered in GENERIC_FONT_FAMILIES:
                        status = "generic"
                    elif manifest_font is not None:
                        status = "manifest-runtime-font"
                    elif index == 0:
                        status = "unmatched-primary"
                    else:
                        status = "unmatched-fallback"
                    entries.append({
                        "family": normalized,
                        "status": status,
                        "source": resource["path"],
                        "manifestFont": manifest_font if manifest_font is not None else {},
                    })
    return {
        "model": "css-font-family-declarations-plus-manifest-fonts",
        "entries": entries,
        "entryCount": len(entries),
        "unmatchedPrimaryCount": len([entry for entry in entries if entry["status"] == "unmatched-primary"]),
    }


def parse_background_service_policy(manifest: dict) -> dict:
    services = manifest.get("backgroundServices", {})
    if services and not isinstance(services, dict):
        fail("manifest backgroundServices must be an object")
    if not isinstance(services, dict):
        services = {}

    def service(name: str, sensor: bool = False) -> dict:
        raw = services.get(name, {})
        if raw and not isinstance(raw, dict):
            fail(f"manifest backgroundServices.{name} must be an object")
        if not isinstance(raw, dict):
            raw = {}
        parsed = {
            "whileSuspended": bool_field(raw, "whileSuspended"),
            "whileScreenOff": bool_field(raw, "whileScreenOff"),
        }
        if sensor:
            parsed["inLowPower"] = bool_field(raw, "inLowPower")
        return parsed

    return {
        "network": service("network"),
        "audio": service("audio"),
        "sensors": service("sensors", sensor=True),
        "location": service("location", sensor=True),
    }


def validate_manifest(manifest: dict) -> dict:
    if manifest.get("format") != "jellyframe.app":
        fail('manifest format must be "jellyframe.app"')
    if int_field(manifest, "formatVersion", -1) != 0:
        fail("only manifest formatVersion 0 is supported")
    app_id = manifest.get("id")
    if not isinstance(app_id, str) or not app_id:
        fail("manifest id is required")
    entry = normalize_app_path(str(manifest.get("entry", "/index.html")))
    version = manifest.get("version", {})
    if not isinstance(version, dict):
        version = {}
    runtime = manifest.get("runtime", {})
    if not isinstance(runtime, dict):
        runtime = {}
    viewport = manifest.get("viewport", {})
    if not isinstance(viewport, dict):
        viewport = {}
    budgets = manifest.get("budgets", {})
    if not isinstance(budgets, dict):
        budgets = {}
    permissions = manifest.get("permissions", [])
    if not isinstance(permissions, list):
        permissions = []
    capabilities = manifest.get("capabilities", [])
    if not isinstance(capabilities, list):
        capabilities = []
    fonts = []
    raw_fonts = manifest.get("fonts", [])
    if raw_fonts and not isinstance(raw_fonts, list):
        fail("manifest fonts must be an array")
    if isinstance(raw_fonts, list):
        for index, font in enumerate(raw_fonts):
            if not isinstance(font, dict):
                fail(f"manifest fonts[{index}] must be an object")
            font_id = font.get("id", "")
            source = font.get("source", "")
            profile = font.get("profile", "")
            if not isinstance(font_id, str) or not font_id:
                fail(f"manifest fonts[{index}].id is required")
            if not isinstance(source, str) or not source:
                fail(f"manifest fonts[{index}].source is required")
            if not isinstance(profile, str) or not profile:
                fail(f"manifest fonts[{index}].profile is required")
            family = font.get("family", "")
            license_info = font.get("license", {})
            fonts.append({
                "id": font_id,
                "source": normalize_app_path(source),
                "profile": profile,
                "family": family if isinstance(family, str) else "",
                "license": license_info if isinstance(license_info, dict) else {},
                "sizes": font.get("sizes", []),
                "weights": font.get("weights", []),
            })
    targets = manifest.get("targets", {})
    if not isinstance(targets, dict):
        targets = {}
    role = manifest.get("role", "app")
    if not isinstance(role, str) or role not in {"app", "launcher", "watchface", "settings"}:
        fail("manifest role must be one of: app, launcher, watchface, settings")
    network_allowed = "network" in permissions or "network.fetch" in capabilities
    storage_kv_allowed = "storage.kv" in capabilities
    audio_playback_allowed = "media.audio.mp3" in capabilities
    sensor_accelerometer_allowed = "sensor.accelerometer" in capabilities
    sensor_gyroscope_allowed = "sensor.gyroscope" in capabilities
    sensor_heart_rate_allowed = "sensor.heart-rate" in capabilities
    sensor_ambient_light_allowed = "sensor.ambient-light" in capabilities
    location_position_allowed = "location.position" in capabilities
    background_services = parse_background_service_policy(manifest)
    return {
        "id": app_id,
        "name": manifest.get("name", app_id),
        "role": role,
        "versionName": version.get("name", "0.0.0"),
        "versionCode": int_field(version, "code", 0),
        "entry": entry,
        "minJellyFrame": runtime.get("minJellyFrame", ""),
        "script": runtime.get("script", "classic"),
        "viewport": viewport,
        "budgets": budgets,
        "fonts": fonts,
        "targets": targets,
        "permissions": permissions,
        "capabilities": capabilities,
        "networkAllowed": network_allowed,
        "storageKvAllowed": storage_kv_allowed,
        "audioPlaybackAllowed": audio_playback_allowed,
        "sensorAccelerometerAllowed": sensor_accelerometer_allowed,
        "sensorGyroscopeAllowed": sensor_gyroscope_allowed,
        "sensorHeartRateAllowed": sensor_heart_rate_allowed,
        "sensorAmbientLightAllowed": sensor_ambient_light_allowed,
        "locationPositionAllowed": location_position_allowed,
        "backgroundServices": background_services,
    }


def service_intent_report(manifest: dict, target_config: dict) -> dict:
    capabilities = manifest.get("capabilities", [])
    if not isinstance(capabilities, list):
        capabilities = []
    permissions = manifest.get("permissions", [])
    if not isinstance(permissions, list):
        permissions = []
    background_services = manifest.get("backgroundServices", {})
    if not isinstance(background_services, dict):
        background_services = parse_background_service_policy({})

    target_id = target_config.get("id", "")
    host_services = target_config.get("hostServices", {})
    if not isinstance(host_services, dict):
        host_services = {}

    def support_state(key: str) -> str:
        if key not in host_services:
            return "unknown"
        return "supported" if bool(host_services.get(key)) else "unsupported"

    return {
        "target": target_id if isinstance(target_id, str) else "",
        "requested": {
            "networkFetch": bool(manifest.get("networkAllowed")),
            "storageKv": bool(manifest.get("storageKvAllowed")),
            "audioPlayback": bool(manifest.get("audioPlaybackAllowed")),
            "sensorAccelerometer": bool(manifest.get("sensorAccelerometerAllowed")),
            "sensorGyroscope": bool(manifest.get("sensorGyroscopeAllowed")),
            "sensorHeartRate": bool(manifest.get("sensorHeartRateAllowed")),
            "sensorAmbientLight": bool(manifest.get("sensorAmbientLightAllowed")),
            "locationPosition": bool(manifest.get("locationPositionAllowed")),
        },
        "targetSupport": {
            "networkFetch": support_state("networkFetch"),
            "storageKv": support_state("storageKv"),
            "audioPlayback": support_state("audioPlayback"),
            "sensorAccelerometer": support_state("sensorAccelerometer"),
            "sensorGyroscope": support_state("sensorGyroscope"),
            "sensorHeartRate": support_state("sensorHeartRate"),
            "sensorAmbientLight": support_state("sensorAmbientLight"),
            "locationPosition": support_state("locationPosition"),
        },
        "permissions": list(permissions),
        "capabilities": list(capabilities),
        "backgroundServices": background_services,
        "policyNotes": [
            "Manifest capabilities describe app intent only; host profile and product policy remain authoritative.",
            "Network fetch is runtime data only; remote HTML, CSS, script and image loaders remain disabled.",
            "Storage is app-private KV only; cookies, IndexedDB, Cache API and general filesystem access are absent.",
            "Audio playback is host-owned; Audio() V0 is available only when the host binds an audio adapter.",
            "Sensor and location data are semantic host services; apps never receive raw hardware handles.",
        ],
    }


def collect_service_target_warnings(manifest: dict, target_config: dict) -> list[dict]:
    host_services = target_config.get("hostServices", {})
    if not isinstance(host_services, dict):
        return []
    target_id = target_config.get("id", "")
    source = f"target:{target_id}" if isinstance(target_id, str) and target_id else "target"
    requests = [
        ("networkFetch", bool(manifest.get("networkAllowed")), "network.fetch"),
        ("storageKv", bool(manifest.get("storageKvAllowed")), "storage.kv"),
        ("audioPlayback", bool(manifest.get("audioPlaybackAllowed")), "media.audio.mp3"),
        ("sensorAccelerometer", bool(manifest.get("sensorAccelerometerAllowed")), "sensor.accelerometer"),
        ("sensorGyroscope", bool(manifest.get("sensorGyroscopeAllowed")), "sensor.gyroscope"),
        ("sensorHeartRate", bool(manifest.get("sensorHeartRateAllowed")), "sensor.heart-rate"),
        ("sensorAmbientLight", bool(manifest.get("sensorAmbientLightAllowed")), "sensor.ambient-light"),
        ("locationPosition", bool(manifest.get("locationPositionAllowed")), "location.position"),
    ]
    warnings = []
    for key, requested, capability in requests:
        if not requested or key not in host_services or bool(host_services.get(key)):
            continue
        warnings.append({
            "level": "warning",
            "code": "service-target-unsupported",
            "message": f"manifest requests {capability}, but target {target_id or '<custom>'} marks {key} unsupported",
            "source": source,
            "service": key,
            "capability": capability,
            "target": target_id if isinstance(target_id, str) else "",
        })
    return warnings


def collect_manifest_warnings(manifest: dict) -> list[dict]:
    warnings = []
    allowed_top_level = {
        "$schema",
        "format",
        "formatVersion",
        "id",
        "name",
        "role",
        "version",
        "entry",
        "runtime",
        "viewport",
        "budgets",
        "fonts",
        "permissions",
        "capabilities",
        "backgroundServices",
        "targets",
    }
    for key in sorted(manifest.keys()):
        if key not in allowed_top_level:
            warnings.append({
                "level": "warning",
                "code": "manifest-field-unknown",
                "message": f"manifest field is not recognized by this JellyFrame toolchain: {key}",
                "source": "jellyframe.app.json",
            })
    known_capabilities = {
        "network.fetch",
        "storage.kv",
        "image.decode",
        "media.audio.mp3",
        "media.microphone",
        "media.camera",
        "media.video.input",
        "sensor.accelerometer",
        "sensor.gyroscope",
        "sensor.heart-rate",
        "sensor.ambient-light",
        "location.position",
        "connectivity.status",
        "connectivity.companion",
        "system.launcher",
        "system.appManager",
    }
    capabilities = manifest.get("capabilities", [])
    if isinstance(capabilities, list):
        for capability in capabilities:
            if isinstance(capability, str) and capability not in known_capabilities:
                warnings.append({
                    "level": "warning",
                    "code": "manifest-capability-unknown",
                    "message": f"manifest capability is not recognized by this JellyFrame toolchain: {capability}",
                    "source": "jellyframe.app.json",
                })
    nested_allowed = {
        "version": {"name", "code"},
        "runtime": {"minJellyFrame", "script"},
        "viewport": {"designWidth", "designHeight", "shape"},
        "budgets": {
            "maxResourceBytes",
            "maxDomNodes",
            "maxDomDepth",
            "maxAttributesPerElement",
            "maxCssRules",
            "maxCssDeclarationsPerRule",
            "maxRenderObjects",
            "maxLayoutBoxes",
            "maxLayers",
            "maxDisplayCommands",
            "maxDirtyRects",
            "maxTimers",
            "maxDetachedDomNodes",
            "maxInputEventsPerFrame",
            "maxTimerCallbacksPerFrame",
            "maxEventListeners",
            "maxFramebufferPixels",
            "maxAppFonts",
            "maxAppFontBytes",
            "maxAppFontGlyphs",
        },
        "backgroundServices": {"network", "audio", "sensors", "location"},
    }
    raw_fonts = manifest.get("fonts", [])
    if isinstance(raw_fonts, list):
        allowed_font_fields = {"id", "source", "profile", "family", "license", "sizes", "weights"}
        allowed_license_fields = {"name", "url", "source"}
        for index, font in enumerate(raw_fonts):
            if not isinstance(font, dict):
                continue
            for key in sorted(font.keys()):
                if key not in allowed_font_fields:
                    warnings.append({
                        "level": "warning",
                        "code": "manifest-field-unknown",
                        "message": f"manifest field is not recognized by this JellyFrame toolchain: fonts[{index}].{key}",
                        "source": "jellyframe.app.json",
                    })
            for field, minimum, maximum in (("sizes", 1, None), ("weights", 1, 1000)):
                value = font.get(field)
                if value is None:
                    warnings.append({
                        "level": "warning",
                        "code": "font-axis-metadata-missing",
                        "message": f"manifest fonts[{index}].{field} is recommended for product font policy",
                        "source": "jellyframe.app.json",
                    })
                    continue
                normalized = normalized_int_list(value, minimum, maximum)
                if not isinstance(value, list) or len(normalized) != len(value) or not normalized:
                    warnings.append({
                        "level": "warning",
                        "code": "font-axis-metadata-invalid",
                        "message": f"manifest fonts[{index}].{field} should be a non-empty integer array",
                        "source": "jellyframe.app.json",
                    })
            license_info = font.get("license")
            if license_info is None:
                continue
            if not isinstance(license_info, dict):
                warnings.append({
                    "level": "warning",
                    "code": "font-license-incomplete",
                    "message": f"manifest fonts[{index}].license should be an object with name/source metadata",
                    "source": "jellyframe.app.json",
                })
                continue
            for key in sorted(license_info.keys()):
                if key not in allowed_license_fields:
                    warnings.append({
                        "level": "warning",
                        "code": "manifest-field-unknown",
                        "message": f"manifest field is not recognized by this JellyFrame toolchain: fonts[{index}].license.{key}",
                        "source": "jellyframe.app.json",
                    })
            if not isinstance(license_info.get("name"), str) or not license_info.get("name"):
                warnings.append({
                    "level": "warning",
                    "code": "font-license-incomplete",
                    "message": f"manifest fonts[{index}].license.name is recommended for redistributed font supplements",
                    "source": "jellyframe.app.json",
                })
            if not isinstance(license_info.get("source"), str) or not license_info.get("source"):
                warnings.append({
                    "level": "warning",
                    "code": "font-license-incomplete",
                    "message": f"manifest fonts[{index}].license.source is recommended for redistributed font supplements",
                    "source": "jellyframe.app.json",
                })
    background_services = manifest.get("backgroundServices")
    if isinstance(background_services, dict):
        background_allowed = {
            "network": {"whileSuspended", "whileScreenOff"},
            "audio": {"whileSuspended", "whileScreenOff"},
            "sensors": {"whileSuspended", "whileScreenOff", "inLowPower"},
            "location": {"whileSuspended", "whileScreenOff", "inLowPower"},
        }
        for service, allowed in background_allowed.items():
            value = background_services.get(service)
            if not isinstance(value, dict):
                continue
            for key in sorted(value.keys()):
                if key not in allowed:
                    warnings.append({
                        "level": "warning",
                        "code": "manifest-field-unknown",
                        "message": f"manifest field is not recognized by this JellyFrame toolchain: backgroundServices.{service}.{key}",
                        "source": "jellyframe.app.json",
                    })
    for parent, allowed in nested_allowed.items():
        value = manifest.get(parent)
        if not isinstance(value, dict):
            continue
        for key in sorted(value.keys()):
            if key not in allowed:
                warnings.append({
                    "level": "warning",
                    "code": "manifest-field-unknown",
                    "message": f"manifest field is not recognized by this JellyFrame toolchain: {parent}.{key}",
                    "source": "jellyframe.app.json",
                })
    return warnings


def collect_audio_resource_warnings(manifest: dict, resources: list[dict]) -> list[dict]:
    capabilities = manifest.get("capabilities", [])
    if not isinstance(capabilities, list) or "media.audio.mp3" not in capabilities:
        return []
    audio_suffixes = {".mp3", ".wav", ".ogg", ".m4a", ".aac"}
    audio_resources = [
        resource for resource in resources
        if Path(resource.get("path", "")).suffix.lower() in audio_suffixes
    ]
    if not audio_resources:
        return []
    if any(Path(resource.get("path", "")).suffix.lower() == ".mp3" for resource in audio_resources):
        return []
    sources = ", ".join(resource["path"] for resource in audio_resources)
    return [{
        "level": "warning",
        "code": "audio-capability-resource-mismatch",
        "message": "manifest declares media.audio.mp3, but packaged audio resources are not MP3: "
                   f"{sources}. A real MCU host MP3 pipeline will not play these resources.",
        "source": "jellyframe.app.json",
    }]


def load_target_preset(target: str) -> dict:
    if not target:
        return {}
    path = presets_dir() / f"{target}.json"
    if not path.is_file():
        return {}
    try:
        preset = json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        fail(f"invalid target preset {target}: {error}")
    if preset.get("id") != target:
        fail(f"target preset id mismatch: {target}")
    return preset


def merge_dict(base: dict, overlay: dict) -> dict:
    merged = dict(base)
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = merge_dict(merged[key], value)
        else:
            merged[key] = value
    return merged


def effective_target_config(manifest: dict, target: str) -> dict:
    manifest_targets = manifest.get("targets", {})
    manifest_target = manifest_targets.get(target, {}) if target else {}
    preset = load_target_preset(target) if target else {}
    has_manifest_target = isinstance(manifest_target, dict) and bool(manifest_target)
    if target and not has_manifest_target and not preset:
        fail(f"target is not declared by manifest and no preset exists: {target}")
    if target and not has_manifest_target:
        print(f"warning: target {target} comes from preset only; manifest does not declare it")
    config = merge_dict(preset, manifest_target if isinstance(manifest_target, dict) else {})
    if target:
        config["id"] = target
    return config


def effective_budgets(manifest: dict, target_config: dict) -> dict:
    budgets = dict(manifest.get("budgets", {}))
    target_budgets = target_config.get("budgets", {})
    if isinstance(target_budgets, dict):
        budgets.update(target_budgets)
    return budgets


def build_resource_entry(root: Path, path: Path, app_path: str, max_resource_bytes: int) -> dict:
    data = path.read_bytes()
    if max_resource_bytes > 0 and len(data) > max_resource_bytes:
        fail(f"resource exceeds maxResourceBytes: {app_path} ({len(data)} bytes)")
    return {
        "path": app_path,
        "file": path,
        "kind": resource_kind(path),
        "size": len(data),
        "crc32": f"{zlib.crc32(data) & 0xffffffff:08x}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "relativeFile": path.relative_to(root).as_posix(),
    }


def is_development_only_file(path: Path) -> bool:
    lowered = path.name.lower()
    return lowered in {
        ".ds_store",
        "readme",
        "readme.md",
        "readme_zh.md",
        "thumbs.db",
    }


def is_development_only_path(relative: Path) -> bool:
    return any(part.startswith(".") or part == "__pycache__" for part in relative.parts) or \
        is_development_only_file(relative)


def discover_resources(root: Path, max_resource_bytes: int) -> list[dict]:
    resources = []
    seen = set()
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "jellyframe.app.json":
            continue
        relative_path = path.relative_to(root)
        if is_development_only_path(relative_path):
            continue
        app_path = normalize_app_path(relative_path.as_posix())
        if app_path in seen:
            fail(f"duplicate normalized resource path: {app_path}")
        seen.add(app_path)
        resources.append(build_resource_entry(root, path, app_path, max_resource_bytes))
    return resources


def classify_reference(value: str) -> str:
    lowered = value.lower()
    if lowered.startswith("data:"):
        return "data"
    if "://" in value or value.startswith("//"):
        return "remote"
    if not value or value.startswith("#"):
        return "fragment"
    return "local"


def strip_url_fragment(value: str) -> str:
    return value.split("#", 1)[0].split("?", 1)[0]


def extract_references(text: str) -> list[dict]:
    refs = []
    patterns = [
        r"""<(?:link|script|img)\b[^>]*(?:href|src)\s*=\s*["']([^"']+)["']""",
        r"""url\(\s*["']?([^"')]+)["']?\s*\)""",
    ]
    for pattern in patterns:
        for match in re.finditer(pattern, text, re.IGNORECASE):
            value = match.group(1).strip()
            if value:
                refs.append({
                    "value": value,
                    "kind": classify_reference(value),
                })
    return refs


def resolve_reference(ref: str, base_path: str) -> str:
    cleaned = strip_url_fragment(ref)
    if not cleaned:
        return ""
    if cleaned.startswith("/"):
        return normalize_app_path(cleaned)
    return normalize_app_path(str(PurePosixPath(base_path).parent / cleaned))


def collect_reference_diagnostics(root: Path, resources: list[dict], entry: str) -> tuple[list[dict], list[dict]]:
    warnings = []
    references = []
    entry_file = local_path_for(root, entry)
    if not entry_file.is_file():
        fail(f"entry resource does not exist: {entry}")
    resources_by_path = {resource["path"]: resource for resource in resources}
    text_resources = [resource for resource in resources if resource["kind"] in {
        "jellyframe::HostResourceKind::Stylesheet",
        "jellyframe::HostResourceKind::ClassicScript",
        "jellyframe::HostResourceKind::Other",
    }]
    if entry not in resources_by_path:
        text_resources.append(build_resource_entry(root, entry_file, entry, 0))

    seen_edges = set()
    for resource in text_resources:
        try:
            text = resource["file"].read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for raw_ref in extract_references(text):
            reference = {
                "from": resource["path"],
                "value": raw_ref["value"],
                "kind": raw_ref["kind"],
                "resolved": "",
                "packaged": False,
            }
            if raw_ref["kind"] == "remote":
                warnings.append({
                    "level": "warning",
                    "code": "remote-package-resource",
                    "message": f"remote package resource is ignored: {raw_ref['value']}",
                    "source": resource["path"],
                })
            elif raw_ref["kind"] == "local":
                resolved = resolve_reference(raw_ref["value"], resource["path"])
                reference["resolved"] = resolved
                reference["packaged"] = resolved in resources_by_path or resolved == entry
                if not reference["packaged"]:
                    warnings.append({
                        "level": "warning",
                        "code": "missing-local-resource",
                        "message": f"referenced resource is not packaged: {resolved}",
                        "source": resource["path"],
                    })
            edge_key = (reference["from"], reference["value"], reference["resolved"])
            if edge_key not in seen_edges:
                references.append(reference)
                seen_edges.add(edge_key)
    return warnings, references


def is_text_resource(resource: dict) -> bool:
    return resource["kind"] in {
        "jellyframe::HostResourceKind::Stylesheet",
        "jellyframe::HostResourceKind::ClassicScript",
        "jellyframe::HostResourceKind::Other",
    }


def collect_source_codepoints(resources: list[dict]) -> set[int]:
    codepoints = set()
    for resource in resources:
        if not is_text_resource(resource):
            continue
        try:
            text = resource["file"].read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for char in text:
            codepoint = ord(char)
            if codepoint >= 0x20 and codepoint not in {0x7f, 0xfeff}:
                codepoints.add(codepoint)
    return codepoints


def codepoint_record(codepoint: int) -> dict:
    char = chr(codepoint)
    return {
        "codepoint": f"U+{codepoint:04X}",
        "char": char if char.strip() else "",
    }


def codepoint_sample(codepoints: set[int], limit: int = 48) -> list[dict]:
    return [codepoint_record(codepoint) for codepoint in sorted(codepoints)[:limit]]


def is_common_symbol_codepoint(codepoint: int) -> bool:
    return (
        0x00a0 <= codepoint <= 0x00bf or
        0x2010 <= codepoint <= 0x203a or
        0x2190 <= codepoint <= 0x21ff or
        0x25a0 <= codepoint <= 0x27bf
    )


def profile_covers_codepoint(profile: str, codepoint: int) -> bool:
    if 0x20 <= codepoint <= 0x7e:
        return True
    if profile in {"tiny-plus-symbols", "app-subset-cn", "cn-standard", "global-product"} and \
            is_common_symbol_codepoint(codepoint):
        return True
    if profile == "cn-standard" and 0x4e00 <= codepoint <= 0x9fff:
        return True
    if profile == "global-product" and codepoint >= 0x80:
        return True
    return False


def parse_jffont(data: bytes) -> dict:
    if len(data) < JFFONT_HEADER_SIZE:
        return {"ok": False, "error": "short-header"}
    if data[:8] != JFFONT_MAGIC:
        return {"ok": False, "error": "bad-magic"}
    header_size, version = struct.unpack_from("<HH", data, 8)
    glyph_count = struct.unpack_from("<I", data, 12)[0]
    line_height = data[16]
    fallback_advance = data[17]
    flags = struct.unpack_from("<H", data, 18)[0]
    glyph_table_offset, row_data_offset, row_data_size = struct.unpack_from("<III", data, 20)
    if header_size != JFFONT_HEADER_SIZE or version not in {0, 1}:
        return {"ok": False, "error": "unsupported-header"}
    if line_height == 0 or fallback_advance == 0:
        return {"ok": False, "error": "invalid-metrics"}
    if (version == 0 and flags != 0) or (version == 1 and (flags & 0xff00) != 0):
        return {"ok": False, "error": "unsupported-header"}
    coverage_bits = 1 if version == 0 else flags & 0xff
    if coverage_bits not in {1, 2, 4}:
        return {"ok": False, "error": "unsupported-coverage-bits"}
    glyph_table_size = glyph_count * JFFONT_GLYPH_ENTRY_SIZE
    if glyph_table_offset > len(data) or glyph_table_size > len(data) - glyph_table_offset:
        return {"ok": False, "error": "glyph-table-out-of-range"}
    if row_data_offset > len(data) or row_data_size > len(data) - row_data_offset:
        return {"ok": False, "error": "row-data-out-of-range"}

    glyphs = set()
    previous = 0
    for index in range(glyph_count):
        offset = glyph_table_offset + index * JFFONT_GLYPH_ENTRY_SIZE
        codepoint, row_offset, row_size = struct.unpack_from("<III", data, offset)
        width = data[offset + 12]
        height = data[offset + 13]
        advance = data[offset + 14]
        bytes_per_row = data[offset + 15]
        minimum_bytes_per_row = (width * coverage_bits + 7) // 8
        minimum_row_size = height * bytes_per_row
        if (
            codepoint == 0 or
            (index > 0 and codepoint <= previous) or
            width == 0 or
            height == 0 or
            advance == 0 or
            bytes_per_row < minimum_bytes_per_row or
            row_size < minimum_row_size or
            row_offset > row_data_size or
            row_size > row_data_size - row_offset
        ):
            return {"ok": False, "error": "invalid-glyph-entry", "glyphIndex": index}
        glyphs.add(codepoint)
        previous = codepoint

    return {
        "ok": True,
        "format": f"jffont-v{version}",
        "coverageBits": coverage_bits,
        "glyphCount": glyph_count,
        "lineHeight": line_height,
        "fallbackAdvance": fallback_advance,
        "glyphs": glyphs,
    }


def collect_font_diagnostics(manifest: dict,
                             resources: list[dict],
                             target_config: dict,
                             budgets: dict) -> tuple[dict, list[dict]]:
    resources_by_path = {resource["path"]: resource for resource in resources}
    source_codepoints = collect_source_codepoints(resources)
    target_profile = target_config.get("fontProfile", "")
    if not isinstance(target_profile, str) or not target_profile:
        target_profile = "tiny"
    system_covered = {
        codepoint for codepoint in source_codepoints
        if profile_covers_codepoint(target_profile, codepoint)
    }
    app_covered = set()
    manifest_fonts = []
    warnings = []
    total_runtime_font_bytes = 0
    total_runtime_font_glyphs = 0
    usable_runtime_fonts = 0
    for font in manifest.get("fonts", []):
        source = font.get("source", "") if isinstance(font, dict) else ""
        license_info = font.get("license", {}) if isinstance(font, dict) else {}
        font_entry = {
            "id": font.get("id", "") if isinstance(font, dict) else "",
            "source": source,
            "profile": font.get("profile", "") if isinstance(font, dict) else "",
            "family": font.get("family", "") if isinstance(font, dict) else "",
            "license": license_info if isinstance(license_info, dict) else {},
            "sizes": normalized_int_list(font.get("sizes", []) if isinstance(font, dict) else [], 1),
            "weights": normalized_int_list(font.get("weights", []) if isinstance(font, dict) else [], 1, 1000),
            "packaged": source in resources_by_path,
            "status": "missing",
        }
        if not isinstance(license_info, dict) or not license_info.get("name") or not license_info.get("source"):
            warnings.append({
                "level": "warning",
                "code": "font-license-missing",
                "message": f"manifest font should declare license.name and license.source before redistribution: {source or font_entry['id']}",
                "source": "jellyframe.app.json",
            })
        resource = resources_by_path.get(source)
        if source and resource is None:
            warnings.append({
                "level": "warning",
                "code": "missing-font-resource",
                "message": f"manifest font source is not packaged: {source}",
                "source": "jellyframe.app.json",
            })
            manifest_fonts.append(font_entry)
            continue

        suffix = resource["file"].suffix.lower() if resource is not None else ""
        if suffix != ".jffont":
            font_entry["status"] = "packaged-unsupported-runtime-format"
            font_entry["format"] = suffix[1:] if suffix else "unknown"
            warnings.append({
                "level": "warning",
                "code": "unsupported-font-resource-format",
                "message": f"manifest font source is packaged but not runtime-loadable yet: {source}",
                "source": "jellyframe.app.json",
            })
            manifest_fonts.append(font_entry)
            continue

        total_runtime_font_bytes += resource["size"]
        parsed = parse_jffont(resource["file"].read_bytes())
        if not parsed.get("ok"):
            font_entry["status"] = "invalid"
            font_entry["format"] = "jffont-v0/v1"
            font_entry["error"] = parsed.get("error", "invalid")
            warnings.append({
                "level": "warning",
                "code": "invalid-jffont-resource",
                "message": f"manifest font source is not a valid .jffont resource: {source}",
                "source": "jellyframe.app.json",
            })
            manifest_fonts.append(font_entry)
            continue

        glyphs = parsed["glyphs"]
        usable_runtime_fonts += 1
        total_runtime_font_glyphs += parsed["glyphCount"]
        app_covered.update(source_codepoints & glyphs)
        font_entry.update({
            "status": "usable",
            "format": parsed["format"],
            "coverageBits": parsed["coverageBits"],
            "glyphCount": parsed["glyphCount"],
            "lineHeight": parsed["lineHeight"],
            "fallbackAdvance": parsed["fallbackAdvance"],
            "usedGlyphCount": len(source_codepoints & glyphs),
            "usedGlyphSample": codepoint_sample(source_codepoints & glyphs, 24),
            })
        manifest_fonts.append(font_entry)

    font_family_usage = collect_font_family_usage(resources, manifest_fonts)
    for entry in font_family_usage["entries"]:
        if entry["status"] != "unmatched-primary":
            continue
        warnings.append({
            "level": "warning",
            "code": "font-family-unmatched",
            "message": f"CSS primary font-family is not declared as a manifest runtime font: {entry['family']}",
            "source": entry["source"],
        })

    missing = source_codepoints - system_covered - app_covered
    missing_non_ascii = {codepoint for codepoint in missing if codepoint >= 0x80}
    if missing_non_ascii:
        sample = ", ".join(item["codepoint"] for item in codepoint_sample(missing_non_ascii, 8))
        warnings.append({
            "level": "warning",
            "code": "font-missing-glyphs",
            "message": f"source text uses codepoints not covered by target profile or app .jffont supplements: {sample}",
            "source": "jellyframe.app.json",
        })

    max_app_fonts = int_field(budgets, "maxAppFonts", 0)
    max_app_font_bytes = int_field(budgets, "maxAppFontBytes", 0)
    max_app_font_glyphs = int_field(budgets, "maxAppFontGlyphs", 0)
    if max_app_fonts > 0 and usable_runtime_fonts > max_app_fonts:
        warnings.append({
            "level": "warning",
            "code": "font-budget-exceeded",
            "message": f"manifest declares {usable_runtime_fonts} usable runtime fonts, over maxAppFonts={max_app_fonts}",
            "source": "jellyframe.app.json",
        })
    if max_app_font_bytes > 0 and total_runtime_font_bytes > max_app_font_bytes:
        warnings.append({
            "level": "warning",
            "code": "font-budget-exceeded",
            "message": f"runtime font payload uses {total_runtime_font_bytes} bytes, over maxAppFontBytes={max_app_font_bytes}",
            "source": "jellyframe.app.json",
        })
    if max_app_font_glyphs > 0 and total_runtime_font_glyphs > max_app_font_glyphs:
        warnings.append({
            "level": "warning",
            "code": "font-budget-exceeded",
            "message": f"runtime font payload has {total_runtime_font_glyphs} glyphs, over maxAppFontGlyphs={max_app_font_glyphs}",
            "source": "jellyframe.app.json",
        })

    diagnostics = {
        "targetFontProfile": target_profile,
        "coverageModel": "target-profile-estimate-plus-jffont-glyph-table",
        "runtimeFontBudget": {
            "maxAppFonts": max_app_fonts,
            "maxAppFontBytes": max_app_font_bytes,
            "maxAppFontGlyphs": max_app_font_glyphs,
        },
        "usableRuntimeFontCount": usable_runtime_fonts,
        "runtimeFontBytes": total_runtime_font_bytes,
        "runtimeFontGlyphs": total_runtime_font_glyphs,
        "sourceCodepointCount": len(source_codepoints),
        "sourceNonAsciiCodepointCount": len({codepoint for codepoint in source_codepoints if codepoint >= 0x80}),
        "sourceNonAsciiSample": codepoint_sample({codepoint for codepoint in source_codepoints if codepoint >= 0x80}),
        "systemProfileCoveredCount": len(system_covered),
        "appFontCoveredCount": len(app_covered),
        "missingCodepointCount": len(missing),
        "missingNonAsciiCodepointCount": len(missing_non_ascii),
        "missingNonAsciiSample": codepoint_sample(missing_non_ascii),
        "manifestFonts": manifest_fonts,
        "fontFamilyUsage": font_family_usage,
    }
    return diagnostics, warnings


def write_cpp(resources: list[dict], output: Path, namespace: str, include: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as generated:
        generated.write("// Generated by tools/package_app.py. Do not edit by hand.\n\n")
        generated.write(f'#include "{include}"\n\n')
        generated.write("#include <cstdint>\n\n")
        generated.write(f"namespace {namespace} {{\n")
        generated.write("namespace {\n\n")
        for index, resource in enumerate(resources):
            generated.write(emit_byte_array(cpp_symbol(index), resource["file"].read_bytes()))
            generated.write("\n\n")
        generated.write("constexpr ResourceEntry kGeneratedEntries[] = {\n")
        for index, resource in enumerate(resources):
            generated.write("    ResourceEntry{\n")
            generated.write(f"        {cpp_string(resource['path'])},\n")
            generated.write(f"        {resource['kind']},\n")
            generated.write(f"        {cpp_symbol(index)},\n")
            generated.write(f"        {resource['size']},\n")
            generated.write("    },\n")
        generated.write("};\n\n")
        generated.write("} // namespace\n\n")
        generated.write("const ResourceBundle& generated_resource_bundle() {\n")
        generated.write(f"    static constexpr ResourceBundle bundle{{kGeneratedEntries, {len(resources)}}};\n")
        generated.write("    return bundle;\n")
        generated.write("}\n\n")
        generated.write(f"}} // namespace {namespace}\n")


def write_debug_dir(root: Path, output_dir: Path, manifest: dict, resources: list[dict], report: dict) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for resource in resources:
        target = output_dir / resource["relativeFile"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(resource["file"], target)
    (output_dir / "jellyframe.package.json").write_text(
        json.dumps({
            "format": "jellyframe.package.debug",
            "app": manifest,
            "sourceRoot": str(root),
            "report": "jellyframe.package.report.json",
        }, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (output_dir / "jellyframe.package.report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def bundle_summary_bytes(manifest: dict) -> bytes:
    return (json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def write_jfapp_bundle(output: Path, manifest: dict, resources: list[dict]) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    summary = bundle_summary_bytes(manifest)
    strings = bytearray()
    payload = bytearray()
    entries = []
    for resource in sorted(resources, key=lambda item: item["path"]):
        path_bytes = resource["path"].encode("utf-8")
        path_offset = len(strings)
        strings.extend(path_bytes)
        payload_offset = len(payload)
        data = resource["file"].read_bytes()
        payload.extend(data)
        append_padding(payload)
        entries.append({
            "path": resource["path"],
            "pathHash": fnv1a_32(resource["path"]),
            "pathOffset": path_offset,
            "pathSize": len(path_bytes),
            "kind": BUNDLE_KIND_BY_RESOURCE_KIND.get(resource["kind"], 0),
            "payloadOffset": payload_offset,
            "payloadSize": len(data),
            "crc32": zlib.crc32(data) & 0xffffffff,
            "flags": 0,
        })

    index = bytearray()
    for entry in entries:
        index.extend(struct.pack(
            JFAPP_ENTRY_FORMAT,
            entry["pathHash"],
            entry["pathOffset"],
            entry["pathSize"],
            entry["kind"],
            entry["payloadOffset"],
            entry["payloadSize"],
            entry["crc32"],
            entry["flags"],
        ))

    summary_offset = JFAPP_HEADER_SIZE
    summary_size = len(summary)
    index_offset = align_up(summary_offset + summary_size)
    strings_offset = align_up(index_offset + len(index))
    payload_offset = align_up(strings_offset + len(strings))
    payload_size = len(payload)

    bundle = bytearray()
    bundle.extend(b"\0" * JFAPP_HEADER_SIZE)
    bundle.extend(summary)
    append_padding(bundle)
    if len(bundle) != index_offset:
        fail("internal jfapp index alignment mismatch")
    bundle.extend(index)
    append_padding(bundle)
    if len(bundle) != strings_offset:
        fail("internal jfapp string alignment mismatch")
    bundle.extend(strings)
    append_padding(bundle)
    if len(bundle) != payload_offset:
        fail("internal jfapp payload alignment mismatch")
    bundle.extend(payload)

    header_without_crc = struct.pack(
        JFAPP_HEADER_FORMAT,
        JFAPP_MAGIC,
        JFAPP_HEADER_SIZE,
        0,
        0,
        summary_offset,
        summary_size,
        index_offset,
        len(entries),
        strings_offset,
        len(strings),
        payload_offset,
        payload_size,
        0,
        0,
    )
    bundle[:JFAPP_HEADER_SIZE] = header_without_crc
    bundle_crc32 = zlib.crc32(bundle) & 0xffffffff
    bundle[:JFAPP_HEADER_SIZE] = struct.pack(
        JFAPP_HEADER_FORMAT,
        JFAPP_MAGIC,
        JFAPP_HEADER_SIZE,
        0,
        0,
        summary_offset,
        summary_size,
        index_offset,
        len(entries),
        strings_offset,
        len(strings),
        payload_offset,
        payload_size,
        bundle_crc32,
        0,
    )
    output.write_bytes(bundle)
    return {
        "format": "jfapp",
        "formatVersion": 0,
        "path": str(output),
        "size": len(bundle),
        "crc32": f"{bundle_crc32:08x}",
        "sha256": hashlib.sha256(bundle).hexdigest(),
        "summaryBytes": summary_size,
        "resourceIndexBytes": len(index),
        "stringTableBytes": len(strings),
        "payloadBytes": payload_size,
        "resourceCount": len(entries),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate and package a JellyFrame app directory.")
    parser.add_argument("--root", required=True, help="App package source directory.")
    parser.add_argument("--output-cpp", help="Generated C++ resource table.")
    parser.add_argument("--output-bundle", help="Generated installable .jfapp bundle.")
    parser.add_argument("--report", required=True, help="Generated JSON report.")
    parser.add_argument("--namespace", default="jellyframe_esp32s3", help="C++ namespace for generated resources.")
    parser.add_argument("--include", default="jellyframe_esp32s3_resources.h", help="C++ include used by generated table.")
    parser.add_argument("--debug-dir", help="Optional copied debug package directory.")
    parser.add_argument("--validate-only", action="store_true", help="Validate and report without emitting C++.")
    parser.add_argument("--target", help="Optional target id. Loads tools/presets/targets/<id>.json and overlays manifest target settings.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    raw_manifest = read_manifest(root)
    warnings = collect_manifest_warnings(raw_manifest)
    manifest = validate_manifest(raw_manifest)
    target_config = effective_target_config(manifest, args.target)
    budgets = effective_budgets(manifest, target_config)
    max_resource_bytes = int_field(budgets, "maxResourceBytes", 0)
    resources = discover_resources(root, max_resource_bytes)
    warnings.extend(collect_audio_resource_warnings(manifest, resources))
    warnings.extend(collect_service_target_warnings(manifest, target_config))
    reference_warnings, references = collect_reference_diagnostics(root, resources, manifest["entry"])
    warnings.extend(reference_warnings)
    font_diagnostics, font_warnings = collect_font_diagnostics(manifest, resources, target_config, budgets)
    warnings.extend(font_warnings)

    if not args.validate_only:
        if not args.output_cpp and not args.output_bundle and not args.debug_dir:
            fail("at least one output is required unless --validate-only is used")
        if args.output_cpp:
            write_cpp(resources, Path(args.output_cpp).resolve(), args.namespace, args.include)
    bundle_report = None
    if not args.validate_only and args.output_bundle:
        bundle_report = write_jfapp_bundle(Path(args.output_bundle).resolve(), manifest, resources)

    report = {
        "format": "jellyframe.package.report",
        "app": manifest,
        "target": target_config,
        "effectiveBudgets": budgets,
        "resourceCount": len(resources),
        "totalResourceBytes": sum(resource["size"] for resource in resources),
        "resources": [
            {
                "path": resource["path"],
                "kind": resource_kind_name(resource["kind"]),
                "size": resource["size"],
                "crc32": resource["crc32"],
                "sha256": resource["sha256"],
            }
            for resource in resources
        ],
        "references": references,
        "serviceIntent": service_intent_report(manifest, target_config),
        "fontDiagnostics": font_diagnostics,
        "warnings": warnings,
    }
    if bundle_report is not None:
        report["bundle"] = bundle_report
    report_path = Path(args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.debug_dir:
        write_debug_dir(root, Path(args.debug_dir).resolve(), manifest, resources, report)

    print(
        f"packaged {manifest['id']} resources={len(resources)} "
        f"bytes={report['totalResourceBytes']} network_allowed={manifest['networkAllowed']} "
        f"storage_kv_allowed={manifest['storageKvAllowed']} "
        f"audio_playback_allowed={manifest['audioPlaybackAllowed']} "
        f"background_services={json.dumps(manifest['backgroundServices'], separators=(',', ':'))} "
        f"warnings={len(warnings)}"
    )
    for warning in warnings:
        print(f"{warning['level']}: {warning['message']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
