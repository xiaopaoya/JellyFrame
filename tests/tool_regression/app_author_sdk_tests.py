#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PACKAGER = REPO_ROOT / "project_tools" / "package_app_author_sdk.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(command: list[str], cwd: Path = REPO_ROOT) -> str:
    environment = os.environ.copy()
    environment.pop("JELLYFRAME_TEST_RENDER_CORE_VERSION", None)
    completed = subprocess.run(command, cwd=cwd, env=environment, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    require(completed.returncode == 0, completed.stdout)
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--qualification-lock", type=Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-author-sdk-test-") as directory:
        root = Path(directory)
        archive = root / "jellyframe-app-sdk.zip"
        command = [sys.executable, str(PACKAGER), "--build-dir", str(build_dir), "--output", str(archive)]
        if args.qualification_lock:
            command.extend(["--qualification-lock", str(args.qualification_lock)])
        run(command)
        with zipfile.ZipFile(archive) as package:
            names = set(package.namelist())
            prefix = next(name.split("/", 1)[0] for name in names if name.endswith("/sdk-manifest.json"))
            expected = {
                f"{prefix}/tools/jellyframe_cli.py",
                f"{prefix}/tools/device_render_core_capabilities.py",
                f"{prefix}/tools/schemas/jellyframe.app.schema.json",
                f"{prefix}/tools/templates/apps/blank/jellyframe.app.json",
                f"{prefix}/tools/presets/targets/round-300.json",
                f"{prefix}/cmake/render_core_feature_registry.csv",
                f"{prefix}/cmake/jellyframe_dependency_lock.cmake",
                f"{prefix}/build/desktop-release/Release/jellyframe_desktop_shell.exe",
                f"{prefix}/build/desktop-release/Release/jellyframe_pseudo_browser.exe",
            }
            require(expected <= names, f"SDK archive misses required App-author files: {expected - names}")
            package.extractall(root / "unpacked")
        sdk = root / "unpacked" / prefix
        templates = json.loads(run([sys.executable, str(sdk / "tools" / "jellyframe_cli.py"), "templates", "--json"], sdk))
        require("blank" in templates.get("templates", []), "unpacked SDK cannot list templates")
        app = root / "independent-app"
        run([
            sys.executable, str(sdk / "tools" / "jellyframe_cli.py"), "new", "--template", "blank",
            "--output", str(app), "--id", "org.example.sdk-test", "--name", "SDK Test", "--target", "round-300",
        ], sdk)
        native_report = app / ".jellyframe" / "native-check.json"
        run([
            sys.executable, str(sdk / "tools" / "jellyframe_cli.py"), "check", "--root", str(app),
            "--target", "round-300", "--build-dir", str(sdk / "build" / "desktop-release" / "Release"),
            "--report", str(native_report),
        ], sdk)
        require(json.loads(native_report.read_text(encoding="utf-8-sig")).get("pipelineDiagnostics", {}).get(
            "pipeline", {}).get("displayCommands", 0) > 0,
                "unpacked SDK must run its bundled native pipeline")
        run([
            sys.executable, str(sdk / "tools" / "jellyframe_cli.py"), "validate", "--root", str(app),
            "--report", str(app / ".jellyframe" / "validation.json"),
        ], sdk)
        manifest = json.loads((sdk / "sdk-manifest.json").read_text(encoding="utf-8"))
        require(manifest.get("format") == "jellyframe.app-author-sdk", "SDK manifest format is missing")
        require(manifest.get("desktopProfiles", {}).get("desktop-release", {}).get("tools"),
                "SDK manifest does not describe the desktop runtime")
        require(manifest.get("renderCore", {}).get("sourceHash"), "SDK manifest must identify the native Core")
        core = manifest["renderCore"]
        for field, value in (("packageVersion", "999.0.0"), ("engineAbi", int(core["engineAbi"]) + 1),
                             ("sourceHash", "0" * 64)):
            wrong = {**core, field: value}
            lock = root / f"wrong-{field}.cmake"
            lock.write_text(
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "{wrong["packageVersion"]}")\n'
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "{wrong["engineAbi"]}")\n'
                f'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "{wrong["sourceHash"]}")\n', encoding="utf-8")
            rejected_archive = root / f"wrong-{field}.zip"
            rejected = subprocess.run([sys.executable, str(PACKAGER), "--build-dir", str(build_dir),
                "--output", str(rejected_archive), "--qualification-lock", str(lock)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            require(rejected.returncode != 0 and "does not match selected SDK lock" in rejected.stdout,
                    f"SDK must refuse mismatched {field}: {rejected.stdout}")
            require(not rejected_archive.exists(), "mismatched SDK must refuse before writing its archive")
    print("App Author SDK packaging tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
