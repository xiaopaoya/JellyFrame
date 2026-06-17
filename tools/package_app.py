#!/usr/bin/env python3
import argparse
import json
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
        "permissions": permissions,
        "capabilities": capabilities,
        "networkAllowed": network_allowed,
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
        data = path.read_bytes()
        if max_resource_bytes > 0 and len(data) > max_resource_bytes:
            fail(f"resource exceeds maxResourceBytes: {app_path} ({len(data)} bytes)")
        resources.append({
            "path": app_path,
            "file": path,
            "kind": resource_kind(path),
            "size": len(data),
            "crc32": f"{zlib.crc32(data) & 0xffffffff:08x}",
        })
    return resources


def extract_local_references(text: str) -> list[str]:
    refs = []
    patterns = [
        r"""<(?:link|script|img)\b[^>]*(?:href|src)\s*=\s*["']([^"']+)["']""",
        r"""url\(\s*["']?([^"')]+)["']?\s*\)""",
    ]
    for pattern in patterns:
        for match in re.finditer(pattern, text, re.IGNORECASE):
            value = match.group(1).strip()
            if value and "://" not in value and not value.startswith("//") and not value.startswith("data:"):
                refs.append(value)
    return refs


def validate_entry_references(root: Path, entry: str, resources_by_path: dict[str, dict]) -> list[str]:
    warnings = []
    entry_file = local_path_for(root, entry)
    if not entry_file.is_file():
        fail(f"entry resource does not exist: {entry}")
    try:
        text = entry_file.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return warnings
    base = PurePosixPath(entry).parent
    for ref in extract_local_references(text):
        if ref.startswith("/"):
            resolved = normalize_app_path(ref)
        else:
            resolved = normalize_app_path(str(base / ref))
        if resolved not in resources_by_path and resolved != entry:
            warnings.append(f"referenced resource is not packaged: {resolved}")
    return warnings


def write_cpp(resources: list[dict], output: Path, namespace: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as generated:
        generated.write("// Generated by tools/package_app.py. Do not edit by hand.\n\n")
        generated.write('#include "jellyframe_esp32s3_resources.h"\n\n')
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate and package a JellyFrame app directory.")
    parser.add_argument("--root", required=True, help="App package source directory.")
    parser.add_argument("--output-cpp", required=True, help="Generated C++ resource table.")
    parser.add_argument("--report", required=True, help="Generated JSON report.")
    parser.add_argument("--namespace", default="jellyframe_esp32s3", help="C++ namespace for generated resources.")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    manifest = validate_manifest(read_manifest(root))
    max_resource_bytes = int_field(manifest["budgets"], "maxResourceBytes", 0)
    resources = discover_resources(root, max_resource_bytes)
    resources_by_path = {resource["path"]: resource for resource in resources}
    warnings = validate_entry_references(root, manifest["entry"], resources_by_path)

    write_cpp(resources, Path(args.output_cpp).resolve(), args.namespace)
    report = {
        "format": "jellyframe.package.report",
        "app": manifest,
        "resourceCount": len(resources),
        "totalResourceBytes": sum(resource["size"] for resource in resources),
        "resources": [
            {
                "path": resource["path"],
                "kind": resource["kind"].split("::")[-1],
                "size": resource["size"],
                "crc32": resource["crc32"],
            }
            for resource in resources
        ],
        "warnings": warnings,
    }
    report_path = Path(args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(
        f"packaged {manifest['id']} resources={len(resources)} "
        f"bytes={report['totalResourceBytes']} network_allowed={manifest['networkAllowed']}"
    )
    for warning in warnings:
        print(f"warning: {warning}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
