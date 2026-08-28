#!/usr/bin/env python3
import json
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
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    require(completed.returncode == 0, completed.stdout)
    return completed.stdout


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: app_author_sdk_tests.py <desktop-release-dir>")
    build_dir = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-author-sdk-test-") as directory:
        root = Path(directory)
        archive = root / "jellyframe-app-sdk.zip"
        run([sys.executable, str(PACKAGER), "--build-dir", str(build_dir), "--output", str(archive)])
        with zipfile.ZipFile(archive) as package:
            names = set(package.namelist())
            prefix = next(name.split("/", 1)[0] for name in names if name.endswith("/sdk-manifest.json"))
            expected = {
                f"{prefix}/tools/jellyframe_cli.py",
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
        run([
            sys.executable, str(sdk / "tools" / "jellyframe_cli.py"), "validate", "--root", str(app),
            "--report", str(app / ".jellyframe" / "validation.json"),
        ], sdk)
        manifest = json.loads((sdk / "sdk-manifest.json").read_text(encoding="utf-8"))
        require(manifest.get("format") == "jellyframe.app-author-sdk", "SDK manifest format is missing")
        require(manifest.get("desktopProfiles", {}).get("desktop-release", {}).get("tools"),
                "SDK manifest does not describe the desktop runtime")
    print("App Author SDK packaging tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
