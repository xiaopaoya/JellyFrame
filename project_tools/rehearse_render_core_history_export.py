#!/usr/bin/env python3
"""Create and verify a disposable history-preserving Render Core export."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path


CORE_PATHS = (
    "LICENSE",
    "src/render_core/",
    "cmake/JellyFrameRenderCoreConfig.cmake.in",
    "cmake/render_core_build.cmake",
    "cmake/render_core_feature_profile.cmake",
    "cmake/render_core_feature_profile.json.in",
    "cmake/render_core_feature_registry.cmake",
    "cmake/render_core_feature_registry.csv",
    "cmake/render_core_link_map.cmake",
    "cmake/render_core_provenance.json.in",
    "cmake/render_core_source_hash.cmake",
    "cmake/render_core_source_manifest.json.in",
    "cmake/render_core_sources.cmake",
    "cmake/render_core_standalone.cmake",
    "cmake/render_core_tests.cmake",
    "cmake/render_core_version.cmake",
    "docs/render_core_release_policy.md",
    "docs/render_core_release_policy_zh.md",
    "project_tools/render_core_ci.yml",
    "project_tools/package_render_core_source.py",
    "tests/tool_regression/render_core_source_archive_tests.py",
)

PATH_RENAMES = (
    ("cmake/render_core_standalone_root.cmake", "CMakeLists.txt"),
    ("cmake/render_core_standalone_presets.json.in", "CMakePresets.json"),
    ("src/render_core/STANDALONE_README.md", "README.md"),
    ("project_tools/render_core_ci.yml", ".github/workflows/ci.yml"),
    ("project_tools/package_render_core_source.py", "tools/package_render_core_source.py"),
    ("tests/tool_regression/render_core_source_archive_tests.py",
     "tests/render_core_source_archive_tests.py"),
    ("docs/render_core_release_policy.md", "docs/release_policy.md"),
    ("docs/render_core_release_policy_zh.md", "docs/release_policy_zh.md"),
)

REQUIRED_EXPORT_FILES = (
    "LICENSE",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "cmake/render_core_build.cmake",
    "cmake/render_core_standalone.cmake",
    "src/render_core/layout.cpp",
    "src/render_core/tests/render_core_tests.cpp",
    "tools/package_render_core_source.py",
    "tests/render_core_source_archive_tests.py",
    ".github/workflows/ci.yml",
    "docs/release_policy.md",
)

FORBIDDEN_EXPORT_PREFIXES = (
    "src/app_runtime/",
    "src/script/",
    "ports/",
    "third_party/",
    "samples/apps/",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Git checkout to export from (committed HEAD only)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="New disposable repository directory",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Remove an existing output directory before cloning",
    )
    return parser.parse_args()


def run(command: list[str], *, cwd: Path) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout.strip()


def require_filter_repo() -> list[str]:
    executable = shutil.which("git-filter-repo")
    if executable is not None:
        return [executable]
    if importlib.util.find_spec("git_filter_repo") is not None:
        # Windows --user installs often leave Scripts outside PATH. The module
        # entry point avoids making that installation detail a prerequisite.
        return [sys.executable, "-m", "git_filter_repo"]
    raise RuntimeError(
        "git-filter-repo is required for a history-preserving export; install it "
        "with 'python -m pip install git-filter-repo'"
    )


def verify_source(source_root: Path) -> str:
    if not source_root.is_dir():
        raise RuntimeError(f"source root does not exist: {source_root}")
    if run(["git", "rev-parse", "--is-inside-work-tree"], cwd=source_root) != "true":
        raise RuntimeError(f"source root is not a Git worktree: {source_root}")
    for path in (*CORE_PATHS, *(source for source, _ in PATH_RENAMES)):
        run(["git", "ls-files", "--error-unmatch", "--", path], cwd=source_root)
    return run(["git", "rev-parse", "HEAD"], cwd=source_root)


def prepare_output(source_root: Path, output_dir: Path, force: bool) -> None:
    if output_dir == source_root:
        raise RuntimeError("--output-dir must not be the source root")
    if output_dir.is_relative_to(source_root) and not output_dir.is_relative_to(source_root / "build"):
        raise RuntimeError("an in-tree --output-dir must be under the ignored build directory")
    if output_dir.exists():
        if not force:
            raise RuntimeError(f"output directory already exists: {output_dir} (use --force)")
        shutil.rmtree(output_dir, onerror=remove_readonly)
    output_dir.parent.mkdir(parents=True, exist_ok=True)


def remove_readonly(function: object, path: str, exception: object) -> None:
    del exception
    os.chmod(path, stat.S_IWRITE)
    function(path)


def filter_history(source_root: Path, output_dir: Path, filter_repo: list[str]) -> None:
    run(["git", "clone", "--no-local", "--no-hardlinks", str(source_root), str(output_dir)], cwd=source_root)
    command = [*filter_repo, "--force"]
    for path in CORE_PATHS:
        command.extend(["--path", path])
    for source, destination in PATH_RENAMES:
        command.extend(["--path", source, "--path-rename", f"{source}:{destination}"])
    run(command, cwd=output_dir)


def verify_export(source_root: Path, output_dir: Path) -> dict[str, object]:
    for path in REQUIRED_EXPORT_FILES:
        if not (output_dir / path).is_file():
            raise RuntimeError(f"filtered export is missing required file: {path}")

    exported_files = run(["git", "ls-files"], cwd=output_dir).splitlines()
    forbidden = [
        path for path in exported_files if path.startswith(FORBIDDEN_EXPORT_PREFIXES)
    ]
    if forbidden:
        raise RuntimeError(
            "filtered export contains non-Core paths: " + ", ".join(forbidden[:10])
        )

    retained_paths = [*CORE_PATHS, *(source for source, _ in PATH_RENAMES)]
    source_history = int(
        run(["git", "rev-list", "--count", "HEAD", "--", *retained_paths], cwd=source_root)
    )
    exported_history = int(run(["git", "rev-list", "--count", "HEAD"], cwd=output_dir))
    if source_history != exported_history:
        raise RuntimeError(
            "retained Render Core history was not preserved: "
            f"source={source_history}, export={exported_history}"
        )
    if exported_history < 2:
        raise RuntimeError("filtered export must retain more than one Render Core commit")

    return {
        "sourceRevision": run(["git", "rev-parse", "HEAD"], cwd=source_root),
        "exportRevision": run(["git", "rev-parse", "HEAD"], cwd=output_dir),
        "renderCoreHistoryCount": exported_history,
        "trackedFileCount": len(exported_files),
    }


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    output_dir = args.output_dir.resolve()
    source_revision = verify_source(source_root)
    prepare_output(source_root, output_dir, args.force)
    filter_history(source_root, output_dir, require_filter_repo())
    summary = verify_export(source_root, output_dir)
    if summary["sourceRevision"] != source_revision:
        raise RuntimeError("source revision changed during export rehearsal")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
