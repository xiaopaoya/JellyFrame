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
        return json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"invalid manifest JSON: {error}")


def int_field(mapping: dict, key: str, default: int = 0) -> int:
    value = mapping.get(key, default)
    return value if isinstance(value, int) else default


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
            fonts.append({
                "id": font_id,
                "source": normalize_app_path(source),
                "profile": profile,
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
    }


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
        "media.audio.mp3",
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
        },
    }
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


def load_target_preset(target: str) -> dict:
    if not target:
        return {}
    path = presets_dir() / f"{target}.json"
    if not path.is_file():
        return {}
    try:
        preset = json.loads(path.read_text(encoding="utf-8"))
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


def collect_manifest_font_diagnostics(manifest: dict, resources: list[dict]) -> list[dict]:
    resources_by_path = {resource["path"] for resource in resources}
    warnings = []
    for font in manifest.get("fonts", []):
        source = font.get("source", "") if isinstance(font, dict) else ""
        if source and source not in resources_by_path:
            warnings.append({
                "level": "warning",
                "code": "missing-font-resource",
                "message": f"manifest font source is not packaged: {source}",
                "source": "jellyframe.app.json",
            })
    return warnings


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
    reference_warnings, references = collect_reference_diagnostics(root, resources, manifest["entry"])
    warnings.extend(reference_warnings)
    warnings.extend(collect_manifest_font_diagnostics(manifest, resources))

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
        f"warnings={len(warnings)}"
    )
    for warning in warnings:
        print(f"{warning['level']}: {warning['message']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
