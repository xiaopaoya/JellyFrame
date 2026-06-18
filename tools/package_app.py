#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import re
import zlib
from pathlib import Path, PurePosixPath


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
    ".ttf": "jellyframe::HostResourceKind::Font",
    ".otf": "jellyframe::HostResourceKind::Font",
    ".woff": "jellyframe::HostResourceKind::Font",
    ".woff2": "jellyframe::HostResourceKind::Font",
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
    targets = manifest.get("targets", {})
    if not isinstance(targets, dict):
        targets = {}
    network_allowed = "network" in permissions or "network.fetch" in capabilities
    return {
        "id": app_id,
        "name": manifest.get("name", app_id),
        "versionName": version.get("name", "0.0.0"),
        "versionCode": int_field(version, "code", 0),
        "entry": entry,
        "minJellyFrame": runtime.get("minJellyFrame", ""),
        "script": runtime.get("script", "classic"),
        "viewport": viewport,
        "budgets": budgets,
        "targets": targets,
        "permissions": permissions,
        "capabilities": capabilities,
        "networkAllowed": network_allowed,
    }


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


def discover_resources(root: Path, max_resource_bytes: int) -> list[dict]:
    resources = []
    seen = set()
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "jellyframe.app.json":
            continue
        relative = path.relative_to(root).as_posix()
        app_path = normalize_app_path(relative)
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate and package a JellyFrame app directory.")
    parser.add_argument("--root", required=True, help="App package source directory.")
    parser.add_argument("--output-cpp", help="Generated C++ resource table.")
    parser.add_argument("--report", required=True, help="Generated JSON report.")
    parser.add_argument("--namespace", default="jellyframe_esp32s3", help="C++ namespace for generated resources.")
    parser.add_argument("--include", default="jellyframe_esp32s3_resources.h", help="C++ include used by generated table.")
    parser.add_argument("--debug-dir", help="Optional copied debug package directory.")
    parser.add_argument("--validate-only", action="store_true", help="Validate and report without emitting C++.")
    parser.add_argument("--target", help="Optional target id. Loads tools/presets/targets/<id>.json and overlays manifest target settings.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    manifest = validate_manifest(read_manifest(root))
    target_config = effective_target_config(manifest, args.target)
    budgets = effective_budgets(manifest, target_config)
    max_resource_bytes = int_field(budgets, "maxResourceBytes", 0)
    resources = discover_resources(root, max_resource_bytes)
    warnings, references = collect_reference_diagnostics(root, resources, manifest["entry"])

    if not args.validate_only:
        if not args.output_cpp:
            fail("--output-cpp is required unless --validate-only is used")
        write_cpp(resources, Path(args.output_cpp).resolve(), args.namespace, args.include)
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
    report_path = Path(args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.debug_dir:
        write_debug_dir(root, Path(args.debug_dir).resolve(), manifest, resources, report)

    print(
        f"packaged {manifest['id']} resources={len(resources)} "
        f"bytes={report['totalResourceBytes']} network_allowed={manifest['networkAllowed']} "
        f"warnings={len(warnings)}"
    )
    for warning in warnings:
        print(f"{warning['level']}: {warning['message']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
