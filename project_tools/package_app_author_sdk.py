#!/usr/bin/env python3
"""Create the minimal, versioned SDK consumed by JellyFrame App-author tools."""

import argparse
import hashlib
import json
import shutil
import tempfile
import zipfile
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
AUTHOR_TOOL_FILES = (
    "app_registry.py",
    "device_image_manifest.py",
    "device_provider_client.py",
    "device_provider_contract.py",
    "device_reference.py",
    "jellyframe_cli.py",
    "jellyframe_versions.py",
    "package_app.py",
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
        "the App's `.jellyframe/build`, not in this SDK.\n",
        encoding="utf-8",
    )


def build_sdk(build_dir: Path, scripting_build_dir: Path | None, output: Path) -> None:
    version = (REPOSITORY / "VERSION").read_text(encoding="utf-8").strip()
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
        copy_file(required_file(REPOSITORY / "cmake" / "jellyframe_dependency_lock.cmake", "Render Core dependency lock"),
                  root / "cmake" / "jellyframe_dependency_lock.cmake")
        copy_file(required_file(REPOSITORY / "VERSION", "version file"), root / "VERSION")

        normal_destination = root / "build" / "desktop-release" / "Release"
        normal_tools = copy_desktop_runtime(build_dir.resolve(), normal_destination, DESKTOP_TOOL_NAMES)
        scripting_tools = []
        if scripting_build_dir is not None:
            scripting_destination = root / "build" / "desktop-scripting-release" / "Release"
            scripting_tools = copy_desktop_runtime(
                scripting_build_dir.resolve(), scripting_destination, ("jellyframe_desktop_shell",))
        write_sdk_readme(root, version)
        manifest = {
            "format": "jellyframe.app-author-sdk",
            "formatVersion": 1,
            "runtimeVersion": version,
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
    args = parser.parse_args()
    build_sdk(args.build_dir, args.scripting_build_dir, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
