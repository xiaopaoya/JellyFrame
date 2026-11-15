#!/usr/bin/env python3
"""Stage isolated sample and fixture apps for a configured Core candidate."""

import json
import shutil
import sys
from pathlib import Path


def stage_tree(source: Path, destination: Path, core_version: str) -> None:
    shutil.copytree(source, destination, dirs_exist_ok=True)
    for manifest_path in destination.rglob("jellyframe.app.json"):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        runtime = manifest.get("runtime")
        if isinstance(runtime, dict) and "minRenderCore" in runtime:
            runtime["minRenderCore"] = core_version
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 6:
        print("usage: stage_candidate_app_packages.py PACKAGES FIXTURES SYSTEM_APPS DESTINATION CORE_VERSION")
        return 2
    packages, fixtures, system_apps, destination = map(Path, sys.argv[1:5])
    core_version = sys.argv[5]
    if destination.exists():
        shutil.rmtree(destination)
    stage_tree(packages, destination / "packages", core_version)
    stage_tree(fixtures, destination / "fixtures", core_version)
    stage_tree(system_apps, destination / "system", core_version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
