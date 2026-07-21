#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import re
import struct
import tempfile
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

IMAGE_CODEC_BY_SUFFIX = {
    ".bmp": "bmp",
    ".png": "png",
    ".jpg": "jpeg",
    ".jpeg": "jpeg",
    ".webp": "webp",
    ".gif": "gif",
}

STANDARD_IMAGE_CODECS = {"bmp", "png", "jpeg", "webp"}


def fail(message: str) -> None:
    raise SystemExit(f"jellyframe_package_app: {message}")


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def presets_dir() -> Path:
    return repo_root() / "tools" / "presets" / "targets"


def normalize_app_path(value: str) -> str:
    if not value or ":" in value or value.startswith("//"):
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


def required_object(parent: dict, key: str) -> dict:
    if key not in parent:
        fail(f"manifest {key} is required")
    value = parent.get(key)
    if not isinstance(value, dict):
        fail(f"manifest {key} must be an object")
    return value


def required_string(parent: dict, key: str, source: str) -> str:
    if key not in parent:
        fail(f"{source}.{key} is required")
    value = parent.get(key)
    if not isinstance(value, str) or not value:
        fail(f"{source}.{key} must be a non-empty string")
    return value


def required_int(parent: dict, key: str, source: str, minimum: int | None = None) -> int:
    if key not in parent:
        fail(f"{source}.{key} is required")
    value = parent.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        fail(f"{source}.{key} must be an integer")
    if minimum is not None and value < minimum:
        fail(f"{source}.{key} must be >= {minimum}")
    return value


def validate_manifest(manifest: dict) -> dict:
    if not isinstance(manifest, dict):
        fail("manifest root must be an object")
    if manifest.get("format") != "jellyframe.app":
        fail('manifest format must be "jellyframe.app"')
    if int_field(manifest, "formatVersion", -1) != 0:
        fail("only manifest formatVersion 0 is supported")
    app_id = manifest.get("id")
    if not isinstance(app_id, str) or not app_id:
        fail("manifest id is required")
    if "entry" not in manifest:
        fail("manifest entry is required")
    entry = normalize_app_path(required_string(manifest, "entry", "manifest"))
    version = required_object(manifest, "version")
    version_name = required_string(version, "name", "manifest version")
    version_code = required_int(version, "code", "manifest version", 0)
    runtime = required_object(manifest, "runtime")
    min_jellyframe = required_string(runtime, "minJellyFrame", "manifest runtime")
    script_mode = required_string(runtime, "script", "manifest runtime")
    if script_mode not in {"none", "classic"}:
        fail("manifest runtime.script must be one of: none, classic")
    viewport = required_object(manifest, "viewport")
    required_int(viewport, "designWidth", "manifest viewport", 1)
    required_int(viewport, "designHeight", "manifest viewport", 1)
    budgets = required_object(manifest, "budgets")
    required_int(budgets, "maxResourceBytes", "manifest budgets", 1)
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
    targets = required_object(manifest, "targets")
    if not targets:
        fail("manifest targets must contain at least one target")
    for target_name, target_config in targets.items():
        if not isinstance(target_name, str) or not target_name:
            fail("manifest targets keys must be non-empty strings")
        if not isinstance(target_config, dict):
            fail(f"manifest targets.{target_name} must be an object")
        if "viewport" not in target_config or not isinstance(target_config.get("viewport"), dict):
            fail(f"manifest targets.{target_name}.viewport must be an object")
        target_viewport = target_config["viewport"]
        required_int(target_viewport, "width", f"manifest targets.{target_name}.viewport", 1)
        required_int(target_viewport, "height", f"manifest targets.{target_name}.viewport", 1)
        output = required_string(target_config, "output", f"manifest targets.{target_name}")
        if output not in {"cpp", "debug-dir", "jfapp"}:
            fail(f"manifest targets.{target_name}.output must be one of: cpp, debug-dir, jfapp")
    role = manifest.get("role", "app")
    if not isinstance(role, str) or role not in {"app", "launcher", "watchface", "settings"}:
        fail("manifest role must be one of: app, launcher, watchface, settings")
    compute_jobs_allowed = "compute.jobs" in capabilities
    video_frame_allowed = "media.video.frame" in capabilities
    network_allowed = "network" in permissions or "network.fetch" in capabilities
    storage_kv_allowed = "storage.kv" in capabilities
    canvas2d_allowed = "graphics.canvas2d" in capabilities
    audio_playback_allowed = "media.audio.playback" in capabilities
    sensor_accelerometer_allowed = "sensor.accelerometer" in capabilities
    sensor_gyroscope_allowed = "sensor.gyroscope" in capabilities
    sensor_heart_rate_allowed = "sensor.heart-rate" in capabilities
    sensor_ambient_light_allowed = "sensor.ambient-light" in capabilities
    location_position_allowed = "location.position" in capabilities
    system_battery_allowed = "system.battery" in capabilities
    system_weather_allowed = "system.weather" in capabilities
    system_activity_allowed = "system.activity" in capabilities
    background_services = parse_background_service_policy(manifest)
    return {
        "id": app_id,
        "name": manifest.get("name", app_id),
        "role": role,
        "versionName": version_name,
        "versionCode": version_code,
        "entry": entry,
        "minJellyFrame": min_jellyframe,
        "script": script_mode,
        "viewport": viewport,
        "budgets": budgets,
        "fonts": fonts,
        "targets": targets,
        "permissions": permissions,
        "capabilities": capabilities,
        "computeJobsAllowed": compute_jobs_allowed,
        "videoFrameAllowed": video_frame_allowed,
        "networkAllowed": network_allowed,
        "storageKvAllowed": storage_kv_allowed,
        "canvas2dAllowed": canvas2d_allowed,
        "audioPlaybackAllowed": audio_playback_allowed,
        "sensorAccelerometerAllowed": sensor_accelerometer_allowed,
        "sensorGyroscopeAllowed": sensor_gyroscope_allowed,
        "sensorHeartRateAllowed": sensor_heart_rate_allowed,
        "sensorAmbientLightAllowed": sensor_ambient_light_allowed,
        "locationPositionAllowed": location_position_allowed,
        "systemBatteryAllowed": system_battery_allowed,
        "systemWeatherAllowed": system_weather_allowed,
        "systemActivityAllowed": system_activity_allowed,
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
            "computeJobs": bool(manifest.get("computeJobsAllowed")),
            "videoFrame": bool(manifest.get("videoFrameAllowed")),
            "networkFetch": bool(manifest.get("networkAllowed")),
            "storageKv": bool(manifest.get("storageKvAllowed")),
            "canvas2d": bool(manifest.get("canvas2dAllowed")),
            "audioPlayback": bool(manifest.get("audioPlaybackAllowed")),
            "sensorAccelerometer": bool(manifest.get("sensorAccelerometerAllowed")),
            "sensorGyroscope": bool(manifest.get("sensorGyroscopeAllowed")),
            "sensorHeartRate": bool(manifest.get("sensorHeartRateAllowed")),
            "sensorAmbientLight": bool(manifest.get("sensorAmbientLightAllowed")),
            "locationPosition": bool(manifest.get("locationPositionAllowed")),
            "systemBattery": bool(manifest.get("systemBatteryAllowed")),
            "systemWeather": bool(manifest.get("systemWeatherAllowed")),
            "systemActivity": bool(manifest.get("systemActivityAllowed")),
        },
        "targetSupport": {
            "computeJobs": support_state("computeJobs"),
            "videoFrame": support_state("videoFrame"),
            "networkFetch": support_state("networkFetch"),
            "storageKv": support_state("storageKv"),
            "canvas2d": support_state("canvas2d"),
            "audioPlayback": support_state("audioPlayback"),
            "sensorAccelerometer": support_state("sensorAccelerometer"),
            "sensorGyroscope": support_state("sensorGyroscope"),
            "sensorHeartRate": support_state("sensorHeartRate"),
            "sensorAmbientLight": support_state("sensorAmbientLight"),
            "locationPosition": support_state("locationPosition"),
            "systemBattery": support_state("systemBattery"),
            "systemWeather": support_state("systemWeather"),
            "systemActivity": support_state("systemActivity"),
        },
        "permissions": list(permissions),
        "capabilities": list(capabilities),
        "backgroundServices": background_services,
        "policyNotes": [
            "Manifest capabilities describe app intent only; host profile and product policy remain authoritative.",
            "Compute jobs are host-owned bounded requests; no JavaScript Worker, message port or arbitrary background code is exposed by this contract.",
            "Network fetch is runtime data only; remote HTML, CSS, script and image loaders remain disabled.",
            "Storage is app-private KV only; cookies, IndexedDB, Cache API and general filesystem access are absent.",
            "Audio playback is host-owned; Audio() V0 is available only when the host binds an audio adapter.",
            "Canvas 2D is an optional bounded drawing surface; backing storage should be allocated only after getContext(\"2d\"). drawImage() V0.4 accepts allocated canvas sources only; image and video sources remain deferred.",
            "Sensor and location data are semantic host services; apps never receive raw hardware handles.",
            "System data snapshots are explicit, low-frequency host summaries. navigator.jellyframe.getSnapshot() exists only when the app declares one of system.battery, system.weather or system.activity and the host binds that summary.",
        ],
    }


def collect_service_target_warnings(manifest: dict, target_config: dict) -> list[dict]:
    host_services = target_config.get("hostServices", {})
    if not isinstance(host_services, dict):
        return []
    target_id = target_config.get("id", "")
    source = f"target:{target_id}" if isinstance(target_id, str) and target_id else "target"
    requests = [
        ("computeJobs", bool(manifest.get("computeJobsAllowed")), "compute.jobs"),
        ("videoFrame", bool(manifest.get("videoFrameAllowed")), "media.video.frame"),
        ("networkFetch", bool(manifest.get("networkAllowed")), "network.fetch"),
        ("storageKv", bool(manifest.get("storageKvAllowed")), "storage.kv"),
        ("canvas2d", bool(manifest.get("canvas2dAllowed")), "graphics.canvas2d"),
        ("audioPlayback", bool(manifest.get("audioPlaybackAllowed")), "media.audio.playback"),
        ("sensorAccelerometer", bool(manifest.get("sensorAccelerometerAllowed")), "sensor.accelerometer"),
        ("sensorGyroscope", bool(manifest.get("sensorGyroscopeAllowed")), "sensor.gyroscope"),
        ("sensorHeartRate", bool(manifest.get("sensorHeartRateAllowed")), "sensor.heart-rate"),
        ("sensorAmbientLight", bool(manifest.get("sensorAmbientLightAllowed")), "sensor.ambient-light"),
        ("locationPosition", bool(manifest.get("locationPositionAllowed")), "location.position"),
        ("systemBattery", bool(manifest.get("systemBatteryAllowed")), "system.battery"),
        ("systemWeather", bool(manifest.get("systemWeatherAllowed")), "system.weather"),
        ("systemActivity", bool(manifest.get("systemActivityAllowed")), "system.activity"),
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
        "compute.jobs",
        "media.video.frame",
        "network.fetch",
        "storage.kv",
        "file.read",
        "file.write",
        "file.manage",
        "graphics.canvas2d",
        "image.decode",
        "media.audio.playback",
        "media.microphone",
        "media.camera",
        "media.video.input",
        "sensor.accelerometer",
        "sensor.gyroscope",
        "sensor.heart-rate",
        "sensor.ambient-light",
        "location.position",
        "system.battery",
        "system.weather",
        "system.activity",
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
    audio_suffixes = {".mp3", ".wav", ".ogg", ".m4a", ".aac"}
    audio_resources = [
        resource for resource in resources
        if Path(resource.get("path", "")).suffix.lower() in audio_suffixes
    ]
    if not audio_resources:
        return []
    if isinstance(capabilities, list) and "media.audio.playback" in capabilities:
        return []
    sources = ", ".join(resource["path"] for resource in audio_resources)
    return [{
        "level": "warning",
        "code": "audio-capability-resource-mismatch",
        "message": "package includes audio resources but manifest does not declare media.audio.playback: "
                   f"{sources}. Declare the host-owned playback capability or remove unused audio assets.",
        "source": "jellyframe.app.json",
    }]


SCRIPT_API_CAPABILITIES = [
    {
        "api": "XMLHttpRequest",
        "capability": "network.fetch",
        "pattern": re.compile(r"\bXMLHttpRequest\b"),
    },
    {
        "api": "localStorage",
        "capability": "storage.kv",
        "pattern": re.compile(r"\blocalStorage\b"),
    },
    {
        "api": "Audio",
        "capability": "media.audio.playback",
        "pattern": re.compile(r"(?:\bnew\s+Audio\s*\(|\bAudio\s*\()"),
    },
    {
        "api": "navigator.geolocation",
        "capability": "location.position",
        "pattern": re.compile(r"\bnavigator\s*\.\s*geolocation\b|\bgetCurrentPosition\s*\("),
    },
    {
        "api": "CanvasRenderingContext2D",
        "capability": "graphics.canvas2d",
        "pattern": re.compile(r"\bgetContext\s*\(\s*['\"]2d['\"]\s*\)"),
        "preserveStrings": True,
    },
    {
        "api": "navigator.jellyframe.getSnapshot",
        "capabilities": ["system.battery", "system.weather", "system.activity"],
        "pattern": re.compile(r"\bnavigator\s*\.\s*jellyframe\s*\.\s*getSnapshot\s*\("),
    },
]


SCRIPT_API_USAGE_WARNINGS = [
    {
        "api": "Date",
        "code": "script-host-time-ambiguous",
        "pattern": re.compile(r"(?:\bnew\s+Date\s*\(\s*\)|(?<![\w.])Date\s*\(\s*\))"),
        "message": "script uses Date() without an explicit value; JellyFrame host time is exposed through Date.now()",
    },
    {
        "api": "fetch",
        "code": "script-api-deferred",
        "pattern": re.compile(r"(?<![\w.])fetch\s*\("),
        "message": "script uses fetch(), which is deferred until bounded Promise/microtask support is available; use XMLHttpRequest GET V0",
    },
    {
        "api": "Promise",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bPromise\b"),
        "message": "script uses Promise, which is deferred until JellyFrame has bounded microtask support",
    },
    {
        "api": "innerHTML",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\binnerHTML\b"),
        "message": "script uses innerHTML, which is deferred; use textContent or DOM creation APIs",
    },
    {
        "api": "getBoundingClientRect",
        "code": "script-api-subset",
        "pattern": re.compile(r"\bgetBoundingClientRect\s*\("),
        "message": "getBoundingClientRect() returns a numeric snapshot from the last completed host layout frame; it does not force synchronous layout or include transform/nested-scroll geometry",
    },
    {
        "api": "pointer capture",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:setPointerCapture|releasePointerCapture)\s*\("),
        "message": "script uses pointer capture, which is deferred; use pointerdown/pointermove/pointerup state while the pointer remains inside the app viewport",
    },
    {
        "api": "dynamic import",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bimport\s*\("),
        "message": "script uses dynamic import, which is deferred; bundle to classic package-local scripts before packaging",
    },
    {
        "api": "WebSocket",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bWebSocket\s*\("),
        "message": "script uses WebSocket, which is outside the current networking subset; use XMLHttpRequest GET V0 or a host-owned service",
    },
    {
        "api": "EventSource",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bEventSource\s*\("),
        "message": "script uses EventSource/server-sent events, which are outside the current networking subset; use host-owned polling or push services",
    },
    {
        "api": "BroadcastChannel",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bBroadcastChannel\s*\("),
        "message": "script uses BroadcastChannel/web messaging, which is outside the current app isolation model",
    },
    {
        "api": "MessageChannel",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:MessageChannel|MessagePort)\s*\("),
        "message": "script uses MessageChannel/MessagePort, which is outside the current app isolation model",
    },
    {
        "api": "DataTransfer",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:DataTransfer|DataTransferItemList|DataTransferItem|dataTransfer)\b"),
        "message": "script uses drag-and-drop DataTransfer APIs, which are outside the wearable input subset",
    },
    {
        "api": "Worker",
        "code": "script-api-deferred",
        "pattern": re.compile(r"(?<![\w.])(?:Worker|SharedWorker)\s*\("),
        "message": "script uses Web Workers, which are not part of the JellyFrame app runtime; use host services for background work",
    },
    {
        "api": "serviceWorker",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bserviceWorker\b"),
        "message": "script uses serviceWorker, which is not part of the JellyFrame app runtime or install/update model",
    },
    {
        "api": "sessionStorage",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bsessionStorage\b"),
        "message": "script uses sessionStorage, which is not available; JellyFrame only exposes host-optional app-private localStorage",
    },
    {
        "api": "document.cookie",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\bdocument\s*\.\s*cookie\b"),
        "message": "script uses document.cookie, which is outside JellyFrame's app-private storage and networking model",
    },
    {
        "api": "storage event",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:window\s*\.\s*)?addEventListener\s*\(\s*(['\"])storage\1"),
        "preserveStrings": True,
        "message": "script listens for browser storage events, which are not dispatched by JellyFrame's app-private storage subset",
    },
    {
        "api": "Selection/Range",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:window|document)\s*\.\s*(?:getSelection|createRange)\s*\("),
        "message": "script uses Selection/Range APIs, which are outside JellyFrame's bounded text-input model",
    },
    {
        "api": "browser navigation",
        "code": "script-api-deferred",
        "pattern": re.compile(r"\b(?:window\s*\.\s*)?location\s*\.\s*(?:assign|replace|reload)\s*\("),
        "message": "script uses browser navigation, which is outside JellyFrame's app-local fragment-route subset",
    },
    {
        "api": "Canvas ImageData",
        "code": "script-canvas-api-deferred",
        "pattern": re.compile(r"\b(?:getImageData|putImageData|createImageData)\s*\("),
        "message": "script uses Canvas ImageData APIs, which are deferred because pixel readback/allocation is outside the bounded Canvas subset",
    },
    {
        "api": "Canvas pattern/conic gradient",
        "code": "script-canvas-api-deferred",
        "pattern": re.compile(r"\b(?:createPattern|createConicGradient)\s*\("),
        "message": "script uses a Canvas pattern or conic-gradient API, which is outside the bounded Canvas subset; use supported linear/radial gradients or a package-local image",
    },
    {
        "api": "browser Canvas image source",
        "code": "script-canvas-api-deferred",
        "pattern": re.compile(r"\b(?:createImageBitmap|ImageBitmap|new\s+Image)\b"),
        "message": "script uses a browser image source for Canvas, which is deferred; drawImage() currently accepts an already allocated canvas source only",
    },
    {
        "api": "Canvas matrix transform",
        "code": "script-canvas-api-deferred",
        "pattern": re.compile(r"\.\s*(?:scale|rotate|transform|setTransform)\s*\("),
        "message": "script uses a Canvas scale/rotate/matrix transform, which is deferred; use pixel-aligned translate()/resetTransform() or CSS transform for a composited element",
    },
    {
        "api": "Canvas compositing/effects",
        "code": "script-canvas-api-deferred",
        "pattern": re.compile(r"\.\s*(?:clip|globalCompositeOperation|filter|shadowBlur|shadowColor|shadowOffsetX|shadowOffsetY)\b"),
        "message": "script uses Canvas clipping, compositing, filter or shadow state, which is outside the bounded Canvas subset; use DOM/CSS layers or a pre-rendered package asset",
    },
]


def strip_script_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"(^|[^:])//[^\n\r]*", r"\1 ", text)
    return text


def strip_script_comments_and_strings(text: str) -> str:
    text = strip_script_comments(text)
    text = re.sub(r"`(?:\\.|[^`\\])*`", "''", text, flags=re.S)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text, flags=re.S)
    return text


QUERY_SELECTOR_CALL_RE = re.compile(
    r"\bquerySelector(?:All)?\s*\(\s*(?:(['\"])(.*?)\1|([^)]*))\s*\)",
    re.S,
)

HTML_UNSUPPORTED_ELEMENT_MESSAGES = {
    "iframe": "nested browsing contexts and iframe navigation are not implemented",
    "embed": "plugin/embed loading is outside the embedded runtime",
    "object": "object/plugin resource loading is outside the embedded runtime",
    "slot": "Shadow DOM slots are not implemented",
    "map": "image maps are not implemented; use explicit buttons or hit regions",
    "area": "image maps are not implemented; use explicit buttons or hit regions",
}

HTML_SUBSET_MARKUP = {
    "picture": ("html-responsive-image-subset",
                "responsive picture/source selection is not implemented"),
    "source": ("html-responsive-image-subset",
               "responsive image and media source selection is not implemented"),
    "table": ("html-table-layout-subset",
              "browser table layout is not implemented"),
    "caption": ("html-table-layout-subset",
                "browser table layout is not implemented"),
    "colgroup": ("html-table-layout-subset",
                 "browser table layout is not implemented"),
    "col": ("html-table-layout-subset",
            "browser table layout is not implemented"),
    "thead": ("html-table-layout-subset",
              "browser table layout is not implemented"),
    "tbody": ("html-table-layout-subset",
              "browser table layout is not implemented"),
    "tfoot": ("html-table-layout-subset",
              "browser table layout is not implemented"),
    "tr": ("html-table-layout-subset",
           "browser table layout is not implemented"),
    "td": ("html-table-layout-subset",
           "browser table layout is not implemented"),
    "th": ("html-table-layout-subset",
           "browser table layout is not implemented"),
    "ruby": ("html-ruby-bidi-subset",
             "ruby annotation and complex bidi layout are not implemented"),
    "rt": ("html-ruby-bidi-subset",
           "ruby annotation layout is not implemented"),
    "rp": ("html-ruby-bidi-subset",
           "ruby fallback semantics are not implemented"),
    "template": ("html-template-subset",
                 "template content is hidden ordinary DOM, not a detached template document"),
    "video": ("html-media-element-deferred",
              "HTML video playback is not implemented"),
    "track": ("html-media-element-deferred",
              "HTML media text tracks are not implemented"),
    "audio": ("html-audio-element-subset",
              "HTMLMediaElement audio markup is not implemented"),
}


def has_top_level_selector_syntax(selector: str) -> bool:
    depth = 0
    quote = ""
    for ch in selector:
        if quote:
            if ch == quote:
                quote = ""
            continue
        if ch in {"'", '"'}:
            quote = ch
        elif ch == "[":
            depth += 1
        elif ch == "]" and depth > 0:
            depth -= 1
        elif depth == 0 and ch in " >+~,:":
            return True
    return False


def is_jellyframe_simple_selector(selector: str) -> bool:
    selector = selector.strip()
    if not selector or has_top_level_selector_syntax(selector):
        return False
    index = 0
    if selector[index] not in ".#[":
        match = re.match(r"[A-Za-z0-9_-]+", selector)
        if match is None:
            return False
        index = match.end()
    while index < len(selector):
        if selector[index] == ".":
            match = re.match(r"\.[A-Za-z0-9_-]+", selector[index:])
            if match is None:
                return False
            index += match.end()
        elif selector[index] == "#":
            match = re.match(r"#[A-Za-z0-9_-]+", selector[index:])
            if match is None:
                return False
            index += match.end()
        elif selector[index] == "[":
            close = selector.find("]", index + 1)
            if close < 0:
                return False
            content = selector[index + 1:close].strip()
            if not re.match(r"^[A-Za-z0-9_-]+(?:=(?:[A-Za-z0-9_-]+|'[^']*'|\"[^\"]*\"))?$", content):
                return False
            index = close + 1
        else:
            return False
    return True


def collect_query_selector_diagnostics(resource_path: str, source_text: str, seen: set) -> tuple[list[dict], list[dict]]:
    entries = []
    warnings = []
    for match in QUERY_SELECTOR_CALL_RE.finditer(strip_script_comments(source_text)):
        selector = match.group(2)
        dynamic_arg = match.group(3)
        if selector is not None and is_jellyframe_simple_selector(selector):
            continue
        code = "script-api-subset"
        key = (resource_path, code, "querySelector")
        if key in seen:
            continue
        seen.add(key)
        detail = selector if selector is not None else (dynamic_arg or "").strip()
        entries.append({
            "api": "querySelector",
            "source": resource_path,
            "declared": True,
            "warningCode": code,
        })
        if selector is None:
            warnings.append({
                "level": "info",
                "code": code,
                "message": "script uses dynamic querySelector/querySelectorAll; JellyFrame supports only simple tag/.class/#id/[attr] selectors at runtime",
                "source": resource_path,
                "api": "querySelector",
                "detail": detail[:80],
            })
        else:
            warnings.append({
                "level": "warning",
                "code": code,
                "message": "script uses querySelector/querySelectorAll outside JellyFrame's simple selector subset",
                "source": resource_path,
                "api": "querySelector",
                "selector": selector,
            })
    return entries, warnings


def html_sources_for_resource(resource: dict) -> list[str]:
    suffix = resource["file"].suffix.lower()
    if suffix not in {".html", ".htm"}:
        return []
    try:
        return [resource["file"].read_text(encoding="utf-8-sig")]
    except UnicodeDecodeError:
        return []


def css_sources_for_resource(resource: dict) -> list[tuple[str, str]]:
    kind = resource_kind_name(resource["kind"])
    suffix = resource["file"].suffix.lower()
    try:
        text = resource["file"].read_text(encoding="utf-8-sig")
    except UnicodeDecodeError:
        return []
    if kind == "Stylesheet":
        return [(resource["path"], text)]
    if suffix not in {".html", ".htm"}:
        return []
    return [(resource["path"], match.group(1))
            for match in re.finditer(r"<style\b[^>]*>(.*?)</style>", text, flags=re.I | re.S)]


def keyframe_blocks(css_text: str):
    pattern = re.compile(r"@(?:-[a-z]+-)?keyframes\s+[-_a-zA-Z][-_a-zA-Z0-9]*\s*\{", flags=re.I)
    for match in pattern.finditer(css_text):
        depth = 1
        index = match.end()
        begin = index
        while index < len(css_text) and depth:
            if css_text[index] == "{":
                depth += 1
            elif css_text[index] == "}":
                depth -= 1
            index += 1
        if depth == 0:
            yield css_text[begin:index - 1]


def collect_animation_diagnostics(resources: list[dict]) -> tuple[dict, list[dict]]:
    supported_properties = {"opacity", "transform", "color", "background", "background-color"}
    layout_properties = {
        "width", "height", "min-width", "min-height", "max-width", "max-height",
        "top", "right", "bottom", "left", "margin", "margin-top", "margin-right",
        "margin-bottom", "margin-left", "padding", "padding-top", "padding-right",
        "padding-bottom", "padding-left", "display", "position", "flex", "flex-grow",
        "flex-shrink", "flex-basis", "flex-direction", "flex-wrap", "gap", "row-gap",
        "column-gap", "grid", "grid-template-columns", "grid-template-rows",
    }
    unsupported_timing = re.compile(r"\b(?:cubic-bezier|steps|linear)\s*\(|\b(?:step-start|step-end)\b", flags=re.I)
    timing_declaration = re.compile(
        r"\b(?:animation(?:-timing-function)?|transition-timing-function)\s*:\s*([^;{}]+)",
        flags=re.I,
    )
    frame_declaration = re.compile(r"(?:from|to|(?:\d+(?:\.\d+)?)%)\s*\{([^{}]*)\}", flags=re.I | re.S)
    declaration_property = re.compile(r"(?:^|;)\s*([-_a-zA-Z][-_a-zA-Z0-9]*)\s*:")
    entries = []
    warnings = []
    seen = set()

    def add(source: str, code: str, message: str, property_name: str = "") -> None:
        key = (source, code, property_name)
        if key in seen:
            return
        seen.add(key)
        entry = {"source": source, "warningCode": code}
        if property_name:
            entry["property"] = property_name
        entries.append(entry)
        warning = {"level": "warning", "code": code, "message": message, "source": source}
        if property_name:
            warning["property"] = property_name
        warnings.append(warning)

    for resource in resources:
        for source_path, source_text in css_sources_for_resource(resource):
            css_text = strip_css_comments(source_text)
            for match in timing_declaration.finditer(css_text):
                if unsupported_timing.search(match.group(1)):
                    add(source_path,
                        "css-animation-timing-function-unsupported",
                        "animation timing uses a function outside JellyFrame's linear/ease/ease-in/ease-out/ease-in-out subset")
            for block in keyframe_blocks(css_text):
                for frame in frame_declaration.finditer(block):
                    for property_match in declaration_property.finditer(frame.group(1)):
                        property_name = property_match.group(1).lower()
                        if property_name in supported_properties or property_name.startswith("--"):
                            continue
                        if property_name in layout_properties:
                            add(source_path,
                                "css-animation-layout-property",
                                f"keyframe animates {property_name}, which requires repeated style/layout work and is outside JellyFrame's low-cost animation subset",
                                property_name)
                        else:
                            add(source_path,
                                "css-animation-keyframe-property-unsupported",
                                f"keyframe property {property_name} is outside JellyFrame's supported animation subset",
                                property_name)
    return {
        "model": "static-css-animation-preflight",
        "entries": entries,
        "entryCount": len(entries),
        "warningCount": len(warnings),
    }, warnings


def collect_background_image_diagnostics(resources: list[dict]) -> tuple[dict, list[dict]]:
    resource_by_path = {resource["path"]: resource for resource in resources}
    entries = []
    warnings = []
    seen = set()
    declaration_pattern = re.compile(r"\b(background(?:-image)?)\s*:\s*([^;{}]+)", flags=re.I)
    url_pattern = re.compile(r"^url\(\s*(?:'([^']+)'|\"([^\"]+)\"|([^\s()]+))\s*\)$", flags=re.I)

    for resource in resources:
        for source_path, source_text in css_sources_for_resource(resource):
            for declaration in declaration_pattern.finditer(strip_css_comments(source_text)):
                raw_value = declaration.group(2).strip()
                if not raw_value.lower().startswith("url("):
                    continue
                match = url_pattern.fullmatch(raw_value)
                url = next((group for group in match.groups() if group is not None), "") if match else ""
                valid = bool(match and url.startswith("/") and not url.startswith("//") and
                             ".." not in url and "\\" not in url and "?" not in url and "#" not in url)
                key = (source_path, raw_value)
                if key in seen:
                    continue
                seen.add(key)
                entry = {
                    "source": source_path,
                    "property": declaration.group(1).lower(),
                    "value": raw_value,
                    "url": url,
                    "supported": valid,
                }
                if not valid:
                    warnings.append({
                        "level": "warning",
                        "code": "css-background-image-url-unsupported",
                        "message": "CSS background image must be one package-absolute url('/assets/image.bmp') without query, fragment, traversal or remote scheme",
                        "source": source_path,
                        "property": declaration.group(1).lower(),
                        "value": raw_value,
                    })
                    entries.append(entry)
                    continue
                target = resource_by_path.get(url)
                if target is None:
                    warnings.append({
                        "level": "warning",
                        "code": "css-background-image-resource-missing",
                        "message": f"CSS background image is not packaged: {url}",
                        "source": source_path,
                        "url": url,
                    })
                elif resource_kind_name(target["kind"]) != "Image":
                    warnings.append({
                        "level": "warning",
                        "code": "css-background-image-resource-not-image",
                        "message": f"CSS background image path is not an image resource: {url}",
                        "source": source_path,
                        "url": url,
                    })
                entries.append(entry)
    return {
        "model": "package-local-css-background-image-v0",
        "entryCount": len(entries),
        "entries": entries,
    }, warnings


def strip_html_inert_content(text: str) -> str:
    text = re.sub(r"<!--.*?-->", " ", text, flags=re.S)
    text = re.sub(r"<script\b[^>]*>.*?</script>", " ", text, flags=re.I | re.S)
    text = re.sub(r"<style\b[^>]*>.*?</style>", " ", text, flags=re.I | re.S)
    return text


def collect_html_api_diagnostics(resources: list[dict]) -> tuple[dict, list[dict]]:
    entries = []
    warnings = []
    seen = set()
    unsupported_pattern = re.compile(
        r"<\s*(iframe|embed|object|slot|map|area)\b",
        flags=re.I,
    )
    form_submit_pattern = re.compile(
        r"<\s*form\b(?=[^>]*(?:\baction\s*=|\bmethod\s*=))[^>]*>",
        flags=re.I | re.S,
    )
    subset_markup_pattern = re.compile(
        r"<\s*(" + "|".join(HTML_SUBSET_MARKUP) + r")\b",
        flags=re.I,
    )
    srcset_pattern = re.compile(r"<\s*(?:img|source)\b[^>]*\ssrcset\s*=", flags=re.I | re.S)
    rtl_direction_pattern = re.compile(
        r"<\s*[a-z][^>]*\sdir\s*=\s*(?:['\"]\s*)?rtl\b",
        flags=re.I | re.S,
    )
    contenteditable_pattern = re.compile(
        r"<\s*[a-z][^>]*\scontenteditable(?:\s*=|\s|>)",
        flags=re.I | re.S,
    )

    def add_subset_warning(source: str, code: str, feature: str, message: str) -> None:
        key = (source, code)
        if key in seen:
            return
        seen.add(key)
        entries.append({
            "feature": feature,
            "source": source,
            "warningCode": code,
        })
        warnings.append({
            "level": "warning",
            "code": code,
            "message": message,
            "source": source,
            "feature": feature,
        })

    for resource in resources:
        for source_text in html_sources_for_resource(resource):
            searchable = strip_html_inert_content(source_text)
            for match in unsupported_pattern.finditer(searchable):
                tag = match.group(1).lower()
                key = (resource["path"], "html-element-unsupported", tag)
                if key in seen:
                    continue
                seen.add(key)
                message = HTML_UNSUPPORTED_ELEMENT_MESSAGES[tag]
                entries.append({
                    "tag": tag,
                    "source": resource["path"],
                    "warningCode": "html-element-unsupported",
                })
                warnings.append({
                    "level": "warning",
                    "code": "html-element-unsupported",
                    "message": f"<{tag}> is outside JellyFrame's app HTML subset: {message}",
                    "source": resource["path"],
                    "tag": tag,
                })
            for match in subset_markup_pattern.finditer(searchable):
                tag = match.group(1).lower()
                code, limitation = HTML_SUBSET_MARKUP[tag]
                add_subset_warning(
                    resource["path"],
                    code,
                    tag,
                    f"<{tag}> uses semantics outside JellyFrame's HTML subset: {limitation}",
                )
            if srcset_pattern.search(searchable):
                add_subset_warning(
                    resource["path"],
                    "html-responsive-image-subset",
                    "srcset",
                    "srcset responsive image selection is not implemented; use one package-local image selected by the app target",
                )
            if rtl_direction_pattern.search(searchable):
                add_subset_warning(
                    resource["path"],
                    "html-ruby-bidi-subset",
                    "dir=rtl",
                    "dir=rtl requests browser bidi layout, which is outside JellyFrame's text-layout subset",
                )
            if contenteditable_pattern.search(searchable):
                add_subset_warning(
                    resource["path"],
                    "html-rich-text-deferred",
                    "contenteditable",
                    "contenteditable is not implemented; JellyFrame supports bounded input and textarea controls only",
                )
            if form_submit_pattern.search(searchable):
                key = (resource["path"], "html-form-submit-deferred")
                if key in seen:
                    continue
                seen.add(key)
                entries.append({
                    "tag": "form",
                    "source": resource["path"],
                    "warningCode": "html-form-submit-deferred",
                })
                warnings.append({
                    "level": "warning",
                    "code": "html-form-submit-deferred",
                    "message": "form action/method submission is not implemented; handle the control with app script or host services",
                    "source": resource["path"],
                    "tag": "form",
                })
    return {
        "model": "static-html-api-preflight",
        "entries": entries,
        "entryCount": len(entries),
        "warningCount": len(warnings),
    }, warnings


def script_sources_for_resource(resource: dict) -> list[tuple[str, str]]:
    diagnostic_sources = resource.get("scriptDiagnosticSources")
    if isinstance(diagnostic_sources, list):
        return [(str(source["path"]), str(source["text"]))
                for source in diagnostic_sources
                if isinstance(source, dict) and isinstance(source.get("path"), str) and
                isinstance(source.get("text"), str)]
    suffix = resource["file"].suffix.lower()
    kind = resource_kind_name(resource["kind"])
    if kind == "ClassicScript":
        try:
            return [(resource["path"], resource["file"].read_text(encoding="utf-8-sig"))]
        except UnicodeDecodeError:
            return []
    if suffix not in {".html", ".htm"}:
        return []
    try:
        text = resource["file"].read_text(encoding="utf-8-sig")
    except UnicodeDecodeError:
        return []
    return [(resource["path"], match.group(1))
            for match in re.finditer(r"<script\b(?![^>]*\bsrc\s*=)[^>]*>(.*?)</script>",
                                     text,
                                     flags=re.I | re.S)]


def collect_script_api_diagnostics(manifest: dict, resources: list[dict]) -> tuple[dict, list[dict]]:
    capabilities = manifest.get("capabilities", [])
    if not isinstance(capabilities, list):
        capabilities = []
    declared = {capability for capability in capabilities if isinstance(capability, str)}
    entries = []
    warnings = []
    missing_capability_count = 0
    seen = set()
    for resource in resources:
        for source_path, source_text in script_sources_for_resource(resource):
            searchable = strip_script_comments_and_strings(source_text)
            searchable_with_strings = strip_script_comments(source_text)
            for api in SCRIPT_API_CAPABILITIES:
                source = searchable_with_strings if api.get("preserveStrings") else searchable
                if not api["pattern"].search(source):
                    continue
                supported_capabilities = api.get("capabilities", [api.get("capability", "")])
                supported_capabilities = [capability for capability in supported_capabilities if capability]
                key = (source_path, api["api"], tuple(supported_capabilities))
                if key in seen:
                    continue
                seen.add(key)
                declared_capability = any(capability in declared for capability in supported_capabilities)
                entry = {
                    "api": api["api"],
                    "capability": supported_capabilities[0] if supported_capabilities else "",
                    "source": source_path,
                    "declared": declared_capability,
                }
                if len(supported_capabilities) > 1:
                    entry["capabilities"] = supported_capabilities
                entries.append(entry)
                if not declared_capability:
                    missing_capability_count += 1
                    capability_text = " or ".join(supported_capabilities)
                    warnings.append({
                        "level": "warning",
                        "code": "script-capability-missing",
                        "message": f"script uses {api['api']} but manifest does not declare {capability_text}",
                        "source": source_path,
                        "api": api["api"],
                        "capability": supported_capabilities[0] if supported_capabilities else "",
                        "capabilities": supported_capabilities,
                    })
            for usage in SCRIPT_API_USAGE_WARNINGS:
                source = searchable_with_strings if usage.get("preserveStrings") else searchable
                if not usage["pattern"].search(source):
                    continue
                key = (source_path, usage["code"], usage["api"])
                if key in seen:
                    continue
                seen.add(key)
                entries.append({
                    "api": usage["api"],
                    "source": source_path,
                    "declared": True,
                    "warningCode": usage["code"],
                })
                warnings.append({
                    "level": "warning",
                    "code": usage["code"],
                    "message": usage["message"],
                    "source": source_path,
                    "api": usage["api"],
                })
            query_entries, query_warnings = collect_query_selector_diagnostics(source_path, source_text, seen)
            entries.extend(query_entries)
            warnings.extend(query_warnings)
    return {
        "model": "static-script-api-capability-preflight",
        "entries": entries,
        "entryCount": len(entries),
        "missingCapabilityCount": missing_capability_count,
        "warningCount": len(warnings),
    }, warnings


def parse_bmp_metadata(data: bytes) -> dict:
    if len(data) < 54 or data[:2] != b"BM":
        return {"ok": False, "reason": "invalid-signature"}
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size or pixel_offset >= len(data):
        return {"ok": False, "reason": "invalid-header"}
    width = struct.unpack_from("<i", data, 18)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0 or planes != 1:
        return {"ok": False, "reason": "invalid-dimensions"}
    height = abs(signed_height)
    supported = compression == 0 and bits_per_pixel in {24, 32}
    row_stride = ((width * bits_per_pixel + 31) // 32) * 4
    required_end = pixel_offset + row_stride * height
    if required_end > len(data):
        return {"ok": False, "reason": "truncated-pixels", "width": width, "height": height}
    return {
        "ok": True,
        "codec": "bmp",
        "width": width,
        "height": height,
        "bitsPerPixel": bits_per_pixel,
        "compression": compression,
        "topDown": signed_height < 0,
        "supportedByWin32DebugShell": supported,
    }


def parse_png_metadata(data: bytes) -> dict:
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return {"ok": False, "reason": "invalid-signature"}
    width = struct.unpack_from(">I", data, 16)[0]
    height = struct.unpack_from(">I", data, 20)[0]
    bit_depth = data[24]
    color_type = data[25]
    return {
        "ok": width > 0 and height > 0,
        "codec": "png",
        "width": width,
        "height": height,
        "bitDepth": bit_depth,
        "colorType": color_type,
    }


def image_codec_for_resource(resource: dict) -> str:
    return IMAGE_CODEC_BY_SUFFIX.get(Path(resource.get("path", "")).suffix.lower(), "unknown")


def image_target_support(codec: str, target_config: dict) -> str:
    host_services = target_config.get("hostServices", {})
    if not isinstance(host_services, dict):
        return "unknown"
    image_decode = host_services.get("imageDecode")
    if image_decode is False:
        return "unsupported"
    codecs = host_services.get("imageCodecs")
    if isinstance(codecs, list):
        normalized = {str(item).lower() for item in codecs if isinstance(item, str)}
        return "supported" if codec in normalized else "unsupported"
    if image_decode is True:
        return "supported" if codec in STANDARD_IMAGE_CODECS else "unsupported"
    return "unknown"


def collect_image_diagnostics(resources: list[dict], target_config: dict) -> tuple[dict, list[dict]]:
    entries = []
    warnings = []
    codec_counts = {}
    target_id = target_config.get("id", "")
    for resource in resources:
        if resource_kind_name(resource["kind"]) != "Image":
            continue
        codec = image_codec_for_resource(resource)
        codec_counts[codec] = codec_counts.get(codec, 0) + 1
        support = image_target_support(codec, target_config)
        entry = {
            "path": resource["path"],
            "codec": codec,
            "size": resource["size"],
            "targetSupport": support,
        }
        data = resource["file"].read_bytes()
        if codec == "bmp":
            metadata = parse_bmp_metadata(data)
            entry["metadata"] = metadata
            if not metadata.get("ok"):
                warnings.append({
                    "level": "warning",
                    "code": "image-bmp-invalid",
                    "message": f"BMP image metadata is invalid: {resource['path']} ({metadata.get('reason', 'invalid')})",
                    "source": resource["path"],
                })
            elif not metadata.get("supportedByWin32DebugShell", False):
                warnings.append({
                    "level": "warning",
                    "code": "image-bmp-unsupported-debug-format",
                    "message": f"BMP image is packaged but not supported by the Win32 debug shell decoder: {resource['path']}",
                    "source": resource["path"],
                })
        elif codec == "png":
            entry["metadata"] = parse_png_metadata(data)
        if codec not in STANDARD_IMAGE_CODECS:
            warnings.append({
                "level": "warning",
                "code": "image-codec-unsupported",
                "message": f"image resource uses unsupported codec '{codec}': {resource['path']}",
                "source": resource["path"],
            })
        if support == "unsupported":
            warnings.append({
                "level": "warning",
                "code": "image-codec-target-unsupported",
                "message": f"target {target_id or '<custom>'} does not declare support for image codec '{codec}': {resource['path']}",
                "source": resource["path"],
                "target": target_id if isinstance(target_id, str) else "",
                "codec": codec,
            })
        entries.append(entry)
    return {
        "model": "package-image-codec-and-target-profile",
        "target": target_id if isinstance(target_id, str) else "",
        "imageCount": len(entries),
        "entries": entries,
        "codecCounts": {codec: codec_counts[codec] for codec in sorted(codec_counts)},
    }, warnings


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
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        fail(f"resource path escapes app root: {app_path}")
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


def build_generated_resource_entry(staging_root: Path,
                                   app_path: str,
                                   data: bytes,
                                   kind: str,
                                   max_resource_bytes: int) -> dict:
    if max_resource_bytes > 0 and len(data) > max_resource_bytes:
        fail(f"generated resource exceeds maxResourceBytes: {app_path} ({len(data)} bytes)")
    relative = PurePosixPath(app_path.lstrip("/"))
    file = staging_root.joinpath(*relative.parts)
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(data)
    return {
        "path": app_path,
        "file": file,
        "kind": kind,
        "size": len(data),
        "crc32": f"{zlib.crc32(data) & 0xffffffff:08x}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "relativeFile": relative.as_posix(),
        "generated": True,
    }


MODULE_SCRIPT_RE = re.compile(
    r'<script\b(?=[^>]*\btype\s*=\s*(["\'])module\1)(?=[^>]*\bsrc\s*=\s*(["\'])([^"\']+)\2)[^>]*>\s*</script\s*>',
    flags=re.I | re.S,
)
MODULE_IMPORT_RE = re.compile(r'^\s*import\s+(.+?)\s+from\s+(["\'])([^"\']+)\2\s*;?', flags=re.M)
MODULE_SIDE_EFFECT_IMPORT_RE = re.compile(r'^\s*import\s+(["\'])([^"\']+)\1\s*;?', flags=re.M)
MODULE_NAMED_EXPORT_RE = re.compile(r'^\s*export\s*{\s*([^}]+)\s*}\s*;?\s*$', flags=re.M)
MODULE_DECL_EXPORT_RE = re.compile(r'\bexport\s+(const|let|var|function|class)\s+([A-Za-z_$][\w$]*)')


def module_symbol(path: str) -> str:
    return f"__jf_module_{fnv1a_32(path):08x}"


def module_default_symbol(path: str) -> str:
    return f"__jf_default_{fnv1a_32(path):08x}"


def parse_module_named_bindings(spec: str, source: str) -> list[tuple[str, str]]:
    bindings = []
    for item in spec.split(","):
        parts = [part.strip() for part in re.split(r"\s+as\s+", item.strip(), maxsplit=1, flags=re.I)]
        if not parts[0]:
            fail(f"empty named import/export binding in module: {source}")
        bindings.append((parts[0], parts[-1]))
    return bindings


def compile_static_module(path: str, source: str, dependency_symbols: dict[str, str]) -> str:
    exports = []
    default_symbol = module_default_symbol(path)

    def replace_import(match: re.Match) -> str:
        spec = match.group(1).strip()
        ref = match.group(3)
        dependency = resolve_reference(ref, path)
        symbol = dependency_symbols.get(dependency)
        if symbol is None:
            fail(f"module import is not part of the static local graph: {path} -> {ref}")
        statements = []
        if spec.startswith("{") and spec.endswith("}"):
            for imported, local in parse_module_named_bindings(spec[1:-1], path):
                statements.append(f"var {local} = {symbol}.{imported};")
        elif spec.startswith("* as "):
            local = spec[5:].strip()
            if not re.fullmatch(r"[A-Za-z_$][\w$]*", local):
                fail(f"unsupported namespace import in module: {path}")
            statements.append(f"var {local} = {symbol};")
        else:
            pieces = [piece.strip() for piece in spec.split(",", 1)]
            default_local = pieces[0]
            if not re.fullmatch(r"[A-Za-z_$][\w$]*", default_local):
                fail(f"unsupported default import in module: {path}")
            statements.append(f"var {default_local} = {symbol}.default;")
            if len(pieces) == 2:
                named = pieces[1]
                if not named.startswith("{") or not named.endswith("}"):
                    fail(f"unsupported mixed import in module: {path}")
                for imported, local in parse_module_named_bindings(named[1:-1], path):
                    statements.append(f"var {local} = {symbol}.{imported};")
        return "\n".join(statements)

    source = MODULE_IMPORT_RE.sub(replace_import, source)

    def replace_side_effect_import(match: re.Match) -> str:
        ref = match.group(2)
        dependency = resolve_reference(ref, path)
        if dependency not in dependency_symbols:
            fail(f"module import is not part of the static local graph: {path} -> {ref}")
        return ""

    source = MODULE_SIDE_EFFECT_IMPORT_RE.sub(replace_side_effect_import, source)

    def replace_named_export(match: re.Match) -> str:
        for local, exported in parse_module_named_bindings(match.group(1), path):
            exports.append((exported, local))
        return ""

    source = MODULE_NAMED_EXPORT_RE.sub(replace_named_export, source)

    def replace_decl_export(match: re.Match) -> str:
        exports.append((match.group(2), match.group(2)))
        return f"{match.group(1)} {match.group(2)}"

    source = MODULE_DECL_EXPORT_RE.sub(replace_decl_export, source)
    default_function = re.compile(r"\bexport\s+default\s+(function|class)\s+([A-Za-z_$][\w$]*)")
    default_match = default_function.search(source)
    if default_match:
        exports.append(("default", default_match.group(2)))
        source = default_function.sub(lambda match: f"{match.group(1)} {match.group(2)}", source, count=1)
    elif re.search(r"\bexport\s+default\b", source):
        source = re.sub(r"\bexport\s+default\s+", f"var {default_symbol} = ", source, count=1)
        exports.append(("default", default_symbol))

    if re.search(r"^\s*(?:import|export)\b", source, flags=re.M):
        fail(f"unsupported ES module syntax in {path}; use static local import/export declarations only")
    assignments = ", ".join(f"{name}: {value}" for name, value in exports)
    return f"var {module_symbol(path)} = (function() {{\n{source}\nreturn {{{assignments}}};\n}})();\n"


def module_import_references(source: str) -> list[str]:
    matches = []
    for match in MODULE_IMPORT_RE.finditer(source):
        matches.append((match.start(), match.group(3)))
    for match in MODULE_SIDE_EFFECT_IMPORT_RE.finditer(source):
        matches.append((match.start(), match.group(2)))
    return [reference for _, reference in sorted(matches)]


def collect_module_graph(resources: list[dict], entry: str) -> tuple[dict, list[str], str, list[dict]]:
    resources_by_path = {resource["path"]: resource for resource in resources}
    entry_resource = resources_by_path.get(entry)
    if entry_resource is None:
        fail(f"entry resource is not packaged: {entry}")
    entry_html = entry_resource["file"].read_text(encoding="utf-8-sig")
    module_tags = list(MODULE_SCRIPT_RE.finditer(entry_html))
    if not module_tags:
        return {}, [], entry_html, []
    if len(module_tags) != 1:
        fail("static ES module authoring V0 accepts exactly one external type=module entry script")
    entry_module = resolve_reference(module_tags[0].group(3), entry)
    graph = {}
    order = []
    visiting = set()

    def visit(path: str) -> None:
        if path in graph:
            return
        if path in visiting:
            fail(f"static ES module import cycle is not supported: {path}")
        resource = resources_by_path.get(path)
        if resource is None or resource_kind_name(resource["kind"]) != "ClassicScript":
            fail(f"module import must resolve to a packaged local .js file: {path}")
        visiting.add(path)
        source = resource["file"].read_text(encoding="utf-8-sig")
        dependencies = []
        for ref in module_import_references(source):
            dependency = resolve_reference(ref, path)
            if not dependency.endswith(".js"):
                fail(f"module import must target a local .js file: {path} -> {ref}")
            dependencies.append(dependency)
            visit(dependency)
        graph[path] = {"source": source, "dependencies": dependencies}
        visiting.remove(path)
        order.append(path)

    visit(entry_module)
    replacement = '<script src="/__jellyframe/modules.bundle.js"></script>'
    transformed_html = entry_html[:module_tags[0].start()] + replacement + entry_html[module_tags[0].end():]
    entries = [{"path": path, "dependencies": graph[path]["dependencies"]} for path in order]
    return graph, order, transformed_html, entries


def apply_static_module_bundle(resources: list[dict],
                               entry: str,
                               staging_root: Path,
                               max_resource_bytes: int) -> tuple[list[dict], dict]:
    graph, order, transformed_html, entries = collect_module_graph(resources, entry)
    if not order:
        return resources, {"model": "static-local-es-modules", "enabled": False, "entryCount": 0}
    bundle_path = "/__jellyframe/modules.bundle.js"
    if any(resource["path"] == bundle_path for resource in resources):
        fail(f"generated module bundle path is already occupied: {bundle_path}")
    dependency_symbols = {path: module_symbol(path) for path in order}
    bundle = "// Generated by JellyFrame static module bundler.\n"
    for path in order:
        bundle += compile_static_module(path, graph[path]["source"], dependency_symbols)
    module_paths = set(order)
    output = [resource for resource in resources if resource["path"] not in module_paths and resource["path"] != entry]
    output.append(build_generated_resource_entry(
        staging_root, entry, transformed_html.encode("utf-8"), "jellyframe::HostResourceKind::Other", max_resource_bytes))
    bundle_resource = build_generated_resource_entry(
        staging_root, bundle_path, bundle.encode("utf-8"), "jellyframe::HostResourceKind::ClassicScript", max_resource_bytes)
    bundle_resource["scriptDiagnosticSources"] = [
        {"path": path, "text": graph[path]["source"]} for path in order
    ]
    output.append(bundle_resource)
    return sorted(output, key=lambda item: item["path"]), {
        "model": "static-local-es-modules",
        "enabled": True,
        "entry": entries[-1]["path"],
        "entryCount": 1,
        "moduleCount": len(order),
        "modules": entries,
        "bundlePath": bundle_path,
        "sourceBytes": sum(len(graph[path]["source"].encode("utf-8")) for path in order),
        "bundleBytes": len(bundle.encode("utf-8")),
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
        if path.is_symlink():
            fail(f"resource symlinks are not allowed: {path.relative_to(root).as_posix()}")
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


def collect_resource_budget_warnings(resources: list[dict], budgets: dict) -> list[dict]:
    limit = int_field(budgets, "maxResourceBytes", 0)
    used = sum(resource["size"] for resource in resources)
    if limit <= 0 or used <= limit:
        return []
    return [{
        "level": "warning",
        "code": "resource-budget-exceeded",
        "message": f"total packaged resources exceed maxResourceBytes: {used} > {limit}",
        "source": "jellyframe.app.json:budgets.maxResourceBytes",
        "used": used,
        "limit": limit,
    }]


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


def budget_meter(used: int, limit: int) -> dict:
    return {
        "used": max(0, int(used)),
        "limit": max(0, int(limit)),
        "exhausted": bool(limit and used >= limit),
    }


def collect_runtime_budget_estimate(resources: list[dict],
                                    budgets: dict,
                                    font_diagnostics: dict) -> dict:
    total_resource_bytes = sum(resource["size"] for resource in resources)
    runtime_font_budget = font_diagnostics.get("runtimeFontBudget", {})
    if not isinstance(runtime_font_budget, dict):
        runtime_font_budget = {}
    return {
        "format": "jellyframe.runtime-budget.estimate",
        "source": "package-preflight",
        "dynamicRuntimeCounters": "available from AppBudgetSnapshot in host/runtime capture",
        "resources": budget_meter(total_resource_bytes, int_field(budgets, "maxResourceBytes", 0)),
        "domNodes": budget_meter(0, int_field(budgets, "maxDomNodes", 0)),
        "cssRules": budget_meter(0, int_field(budgets, "maxCssRules", 0)),
        "renderObjects": budget_meter(0, int_field(budgets, "maxRenderObjects", 0)),
        "layoutBoxes": budget_meter(0, int_field(budgets, "maxLayoutBoxes", 0)),
        "layers": budget_meter(0, int_field(budgets, "maxLayers", 0)),
        "displayCommands": budget_meter(0, int_field(budgets, "maxDisplayCommands", 0)),
        "dirtyRects": budget_meter(0, int_field(budgets, "maxDirtyRects", 0)),
        "timers": budget_meter(0, int_field(budgets, "maxTimers", 0)),
        "eventListeners": budget_meter(0, int_field(budgets, "maxEventListeners", 0)),
        "framebufferPixels": budget_meter(0, int_field(budgets, "maxFramebufferPixels", 0)),
        "appFonts": budget_meter(font_diagnostics.get("usableRuntimeFontCount", 0),
                                 runtime_font_budget.get("maxAppFonts", 0)),
        "appFontBytes": budget_meter(font_diagnostics.get("runtimeFontBytes", 0),
                                     runtime_font_budget.get("maxAppFontBytes", 0)),
        "appFontGlyphs": budget_meter(font_diagnostics.get("runtimeFontGlyphs", 0),
                                      runtime_font_budget.get("maxAppFontGlyphs", 0)),
    }


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
    (output_dir / "jellyframe.app.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
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
    module_staging = tempfile.TemporaryDirectory(prefix="jellyframe-static-modules-")
    resources, module_diagnostics = apply_static_module_bundle(
        resources, manifest["entry"], Path(module_staging.name), max_resource_bytes)
    warnings.extend(collect_resource_budget_warnings(resources, budgets))
    warnings.extend(collect_audio_resource_warnings(manifest, resources))
    warnings.extend(collect_service_target_warnings(manifest, target_config))
    html_api_diagnostics, html_api_warnings = collect_html_api_diagnostics(resources)
    warnings.extend(html_api_warnings)
    animation_diagnostics, animation_warnings = collect_animation_diagnostics(resources)
    warnings.extend(animation_warnings)
    script_api_diagnostics, script_api_warnings = collect_script_api_diagnostics(manifest, resources)
    warnings.extend(script_api_warnings)
    image_diagnostics, image_warnings = collect_image_diagnostics(resources, target_config)
    warnings.extend(image_warnings)
    background_image_diagnostics, background_image_warnings = collect_background_image_diagnostics(resources)
    warnings.extend(background_image_warnings)
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
        "staticModules": module_diagnostics,
        "serviceIntent": service_intent_report(manifest, target_config),
        "htmlApiDiagnostics": html_api_diagnostics,
        "animationDiagnostics": animation_diagnostics,
        "scriptApiDiagnostics": script_api_diagnostics,
        "imageDiagnostics": image_diagnostics,
        "backgroundImageDiagnostics": background_image_diagnostics,
        "fontDiagnostics": font_diagnostics,
        "runtimeBudgetEstimate": collect_runtime_budget_estimate(resources, budgets, font_diagnostics),
        "warnings": warnings,
    }
    if bundle_report is not None:
        report["bundle"] = bundle_report
    report_path = Path(args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.debug_dir:
        write_debug_dir(root, Path(args.debug_dir).resolve(), raw_manifest, resources, report)

    print(
        f"packaged {manifest['id']} resources={len(resources)} "
        f"bytes={report['totalResourceBytes']} network_allowed={manifest['networkAllowed']} "
        f"storage_kv_allowed={manifest['storageKvAllowed']} "
        f"video_frame_allowed={manifest['videoFrameAllowed']} "
        f"canvas2d_allowed={manifest['canvas2dAllowed']} "
        f"audio_playback_allowed={manifest['audioPlaybackAllowed']} "
        f"background_services={json.dumps(manifest['backgroundServices'], separators=(',', ':'))} "
        f"warnings={len(warnings)}"
    )
    for warning in warnings:
        print(f"{warning['level']}: {warning['message']}")
    module_staging.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
