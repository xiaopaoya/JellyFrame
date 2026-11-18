#!/usr/bin/env python3
"""Create the minimal, versioned SDK consumed by JellyFrame App-author tools."""

import argparse
import hashlib
import json
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

from sdk_python_runtime import install_runtime


REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / "tools"))
try:
    from bootstrap_render_core import read_lock
finally:
    sys.path.pop(0)
AUTHOR_TOOL_FILES = (
    "app_registry.py",
    "device_image_manifest.py",
    "device_provider_client.py",
    "device_provider_contract.py",
    "device_render_core_capabilities.py",
    "device_reference.py",
    "jellyframe_cli.py",
    "jellyframe_versions.py",
    "package_app.py",
    "render_performance_report.py",
    "svg_rasterize.py",
)
DESKTOP_TOOL_NAMES = (
    "jellyframe_desktop_shell",
    "jellyframe_pseudo_browser",
    "jellyframe_font_resource_check",
    "jellyframe_font_pack_gen",
)


def executable(name: str) -> str:
    return f"{name}.exe" if __import__("sys").platform.startswith("win") else name


def required_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise SystemExit(f"missing {description}: {path}")
    return path


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"missing required SDK directory: {source}")
    shutil.copytree(source, destination, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))


def copy_desktop_runtime(build_dir: Path, destination: Path, tool_names: tuple[str, ...]) -> list[str]:
    copied = []
    for name in tool_names:
        source = required_file(build_dir / executable(name), f"desktop tool {name}")
        copy_file(source, destination / source.name)
        copied.append(source.name)
    return copied


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def archive_contents(root: Path) -> list[dict[str, object]]:
    return [
        {"path": str(path.relative_to(root)).replace("\\", "/"), "bytes": path.stat().st_size,
         "sha256": file_sha256(path)}
        for path in sorted(root.rglob("*")) if path.is_file()
    ]


def write_sdk_readme(root: Path, version: str) -> None:
    (root / "README.md").write_text(
        "# JellyFrame App Author SDK\n\n"
        f"Version: `{version}`\n\n"
        "This SDK is for VS Code App authors. It contains the JellyFrame CLI, App templates,\n"
        "schema, target presets and desktop runtime. It is not a framework source checkout and\n"
        "does not contain ports, ESP-IDF, Render Core maintenance tools or project test fixtures.\n\n"
        "In VS Code, open an App in its own directory and run **JellyFrame: Configure Author\n"
        "Environment** once, selecting this directory. Reports and temporary output belong in\n"
        "the App's `.jellyframe/build`, not in this SDK.\n\n"
        "Official Windows x64 packages include `runtime/python/python.exe` and pyserial.\n"
        "No system Python, pip, or PATH changes are needed. Leave `jellyframe.pythonPath`\n"
        "empty to select it automatically. Upstream licenses and pinned download provenance\n"
        "are retained in `runtime/python`.\n",
        encoding="utf-8",
    )


def desktop_core_identity(build_dir: Path, expected: dict[str, str], qualification: bool) -> dict:
    candidates = [build_dir / "generated" / "jellyframe_render_core_provenance.json",
                  build_dir.parent / "generated" / "jellyframe_render_core_provenance.json"]
    provenance_file = next((path for path in candidates if path.is_file()), None)
    if provenance_file is None:
        raise SystemExit(f"missing desktop Render Core build provenance: {build_dir}")
    provenance = json.loads(provenance_file.read_text(encoding="utf-8"))
    if provenance.get("schemaVersion") != 1 or provenance.get("consumer") != "jellyframe-runtime":
        raise SystemExit(f"invalid desktop Render Core build provenance: {provenance_file}")
    development = not qualification and provenance.get("lockEnforced") is False and provenance.get(
        "provider") in ("in-tree", "source-override")
    if development:
        identity = {key: provenance.get(key) for key in ("packageVersion", "engineAbi", "sourceHash", "sourceFileCount")}
        if (not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", str(identity["packageVersion"])) or
                type(identity["engineAbi"]) is not int or identity["engineAbi"] <= 0 or
                not re.fullmatch(r"[0-9a-f]{64}", str(identity["sourceHash"])) or
                type(identity["sourceFileCount"]) is not int or identity["sourceFileCount"] <= 0):
            raise SystemExit(f"invalid development Render Core identity: {provenance_file}")
        return {**identity, "lockKind": "development"}
    for field, value in (("packageVersion", expected["version"]), ("engineAbi", expected["abi"]),
                         ("sourceHash", expected["source_hash"])):
        if str(provenance.get(field, "")) != value:
            raise SystemExit(f"desktop Render Core {field} does not match selected SDK lock: {build_dir}")
    if qualification and provenance.get("lockEnforced") is not True:
        raise SystemExit(f"qualification SDK requires an exact installed-package build: {build_dir}")
    return {**{key: provenance[key] for key in ("packageVersion", "engineAbi", "sourceHash", "sourceFileCount")},
            "lockKind": "qualification" if qualification else "production"}


def build_sdk(build_dir: Path, scripting_build_dir: Path | None, output: Path,
              embedded_python_cache: Path | None = None, msvc_runtime_dir: Path | None = None,
              qualification_lock: Path | None = None) -> None:
    version = (REPOSITORY / "VERSION").read_text(encoding="utf-8").strip()
    selected_lock = qualification_lock or REPOSITORY / "cmake" / "jellyframe_dependency_lock.cmake"
    expected_core = read_lock(required_file(selected_lock, "SDK Render Core lock"))
    core_identity = desktop_core_identity(build_dir.resolve(), expected_core, qualification_lock is not None)
    if scripting_build_dir is not None:
        scripting_core = desktop_core_identity(scripting_build_dir.resolve(), expected_core, qualification_lock is not None)
        if any(scripting_core[key] != core_identity[key] for key in
               ("packageVersion", "engineAbi", "sourceHash", "sourceFileCount")):
            raise SystemExit("SDK desktop and scripting profiles have different Render Core identities")
    output = output.resolve()
    if output.suffix.lower() != ".zip":
        raise SystemExit("SDK output must use a .zip extension")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="jellyframe-author-sdk-") as directory:
        root = Path(directory) / f"jellyframe-app-sdk-{version}"
        tools = root / "tools"
        for name in AUTHOR_TOOL_FILES:
            copy_file(required_file(REPOSITORY / "tools" / name, f"author tool {name}"), tools / name)
        copy_file(required_file(REPOSITORY / "tools" / "debug" / "jellyframe_debug.py", "desktop debug helper"),
                  tools / "debug" / "jellyframe_debug.py")
        copy_tree(REPOSITORY / "tools" / "presets", tools / "presets")
        copy_tree(REPOSITORY / "tools" / "schemas", tools / "schemas")
        copy_tree(REPOSITORY / "tools" / "templates", tools / "templates")
        copy_file(required_file(REPOSITORY / "project_tools" / "__init__.py", "feature registry package"),
                  root / "project_tools" / "__init__.py")
        copy_file(required_file(REPOSITORY / "project_tools" / "render_core_feature_registry.py", "feature registry"),
                  root / "project_tools" / "render_core_feature_registry.py")
        copy_file(required_file(REPOSITORY / "cmake" / "render_core_feature_registry.csv", "feature registry data"),
                  root / "cmake" / "render_core_feature_registry.csv")
        copy_file(selected_lock, root / "cmake" / "jellyframe_dependency_lock.cmake")
        if core_identity["lockKind"] == "development":
            (root / "cmake" / "jellyframe_dependency_lock.cmake").write_text(
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "{core_identity["packageVersion"]}")\n'
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "{core_identity["engineAbi"]}")\n'
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "{core_identity["sourceHash"]}")\n', encoding="utf-8")
        if core_identity["lockKind"] != "production":
            for path in (tools / "templates").rglob("jellyframe.app.json"):
                template = json.loads(path.read_text(encoding="utf-8"))
                template["runtime"]["minRenderCore"] = core_identity["packageVersion"]
                path.write_text(json.dumps(template, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        copy_file(required_file(REPOSITORY / "VERSION", "version file"), root / "VERSION")

        normal_destination = root / "build" / "desktop-release" / "Release"
        normal_tools = copy_desktop_runtime(build_dir.resolve(), normal_destination, DESKTOP_TOOL_NAMES)
        scripting_tools = []
        if scripting_build_dir is not None:
            scripting_destination = root / "build" / "desktop-scripting-release" / "Release"
            scripting_tools = copy_desktop_runtime(
                scripting_build_dir.resolve(), scripting_destination, ("jellyframe_desktop_shell",))
        if msvc_runtime_dir is not None:
            for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                required_file(msvc_runtime_dir / name, "MSVC redistributable runtime")
            for library in msvc_runtime_dir.glob("*.dll"):
                copy_file(library, normal_destination / library.name)
                if scripting_build_dir is not None:
                    copy_file(library, scripting_destination / library.name)
        write_sdk_readme(root, version)
        python_runtime = None
        if embedded_python_cache is not None:
            python_runtime = install_runtime(root / "runtime" / "python", embedded_python_cache)
        manifest = {
            "format": "jellyframe.app-author-sdk",
            "formatVersion": 1,
            "runtimeVersion": version,
            "renderCore": core_identity,
            **({"pythonRuntime": python_runtime} if python_runtime else {}),
            **({"nativeRuntime": {"kind": "msvc-app-local", "files": sorted(
                library.name for library in msvc_runtime_dir.glob("*.dll"))}} if msvc_runtime_dir else {}),
            "desktopProfiles": {
                "desktop-release": {"tools": normal_tools},
                **({"desktop-scripting-release": {"tools": scripting_tools}} if scripting_build_dir else {}),
            },
            "files": archive_contents(root),
        }
        (root / "sdk-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for source in sorted(root.rglob("*")):
                if source.is_file():
                    archive.write(source, source.relative_to(root.parent).as_posix())
    print(f"created App Author SDK: {output}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Package the minimal JellyFrame App Author SDK.")
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Release directory containing the standard desktop tools.")
    parser.add_argument("--scripting-build-dir", type=Path,
                        help="Optional scripting Release directory for classic-script App debugging.")
    parser.add_argument("--output", required=True, type=Path, help="Output .zip path.")
    parser.add_argument("--embedded-python-cache", type=Path,
                        help="Include pinned Windows x64 Python and pyserial; download/cache artifacts here at build time.")
    parser.add_argument("--msvc-runtime-dir", type=Path,
                        help="Visual Studio redistributable x64 Microsoft.VC*.CRT directory for app-local DLLs.")
    parser.add_argument("--qualification-lock", type=Path,
                        help="Exact candidate Core lock for an isolated qualification SDK.")
    args = parser.parse_args()
    build_sdk(args.build_dir, args.scripting_build_dir, args.output, args.embedded_python_cache,
              args.msvc_runtime_dir, args.qualification_lock)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
