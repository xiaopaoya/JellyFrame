#!/usr/bin/env python3
"""Stable desktop debugging facade for IDEs and app authors.

The facade resolves the current desktop shell name and build profile, then
delegates all rendering and interaction semantics to the native executable.
It intentionally does not implement a second renderer.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SHELL_NAMES = ("jellyframe_desktop_shell",)


def executable_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def shell_path(build_dir: Path, explicit: Path | None) -> Path:
    if explicit:
        return explicit
    for name in SHELL_NAMES:
        candidate = build_dir / executable_name(name)
        if candidate.is_file():
            return candidate
    return build_dir / executable_name(SHELL_NAMES[0])


def candidate_builds() -> list[Path]:
    roots = [REPO_ROOT / "build", REPO_ROOT / "build-script"]
    result: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for candidate in (root / "Release", root / "Debug", root):
            if candidate.is_dir() and any((candidate / executable_name(name)).is_file() for name in SHELL_NAMES):
                result.append(candidate)
    return list(dict.fromkeys(result))


def default_build_dir() -> Path:
    for candidate in (
        REPO_ROOT / "build" / "desktop-release" / "Release",
        REPO_ROOT / "build" / "desktop-debug" / "Debug",
        REPO_ROOT / "build" / "Release",
        REPO_ROOT / "build" / "Debug",
    ):
        if candidate.is_dir():
            return candidate
    return REPO_ROOT / "build" / "desktop-release" / "Release"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Launch JellyFrame's desktop app-debugging shell without knowing its build/name."
    )
    parser.add_argument("--build-dir", type=Path, default=default_build_dir(),
                        help="Build directory containing jellyframe_desktop_shell.")
    parser.add_argument("--shell", type=Path, help="Explicit shell executable path.")
    parser.add_argument("--app", type=Path, help="Source package or .jfapp to open.")
    parser.add_argument("--capture", type=Path, help="Capture one frame to BMP/PPM.")
    parser.add_argument("--frame-script", type=Path, help="Run a deterministic frame script.")
    parser.add_argument("--runtime-log", type=Path,
                        help="Tee the desktop shell output to this runtime log while --wait is active.")
    parser.add_argument("--vscode-debug", action="store_true",
                        help="Run the isolated VS Code frame-stream mode.")
    parser.add_argument("--vscode-frame-dir", type=Path,
                        help="Directory for complete VS Code frame snapshots (requires --vscode-debug).")
    parser.add_argument("--viewport-width", type=int,
                        help="Override the app viewport width for this desktop-shell session.")
    parser.add_argument("--viewport-height", type=int,
                        help="Override the app viewport height for this desktop-shell session.")
    parser.add_argument("--list-builds", action="store_true", help="List discovered desktop builds and exit.")
    parser.add_argument("--wait", action="store_true", help="Wait for the shell and return its exit code.")
    parser.add_argument("shell_args", nargs=argparse.REMAINDER,
                        help="Additional shell arguments after '--'.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list_builds:
        for build in candidate_builds():
            print(build)
        return 0

    shell = shell_path(args.build_dir.resolve(), args.shell.resolve() if args.shell else None)
    if not shell.is_file():
        print(f"JellyFrame desktop shell was not found: {shell}", file=sys.stderr)
        print("Build the desktop examples or pass --shell PATH. Use --list-builds to inspect candidates.", file=sys.stderr)
        return 2

    command = [str(shell)]
    if args.capture:
        command.extend(["--capture", str(args.capture)])
    if args.app:
        command.extend(["--app", str(args.app)])
    if args.frame_script:
        command.extend(["--frame-script", str(args.frame_script)])
    if args.vscode_debug:
        if not args.vscode_frame_dir:
            print("--vscode-debug requires --vscode-frame-dir", file=sys.stderr)
            return 2
        command.extend(["--vscode-debug", "--vscode-frame-dir", str(args.vscode_frame_dir)])
    if args.viewport_width is not None:
        command.extend(["--viewport-width", str(args.viewport_width)])
    if args.viewport_height is not None:
        command.extend(["--viewport-height", str(args.viewport_height)])
    extra = args.shell_args
    if extra and extra[0] == "--":
        extra = extra[1:]
    command.extend(extra)
    print("+ " + " ".join(command), flush=True)
    if args.runtime_log:
        args.runtime_log.parent.mkdir(parents=True, exist_ok=True)
        process = subprocess.Popen(
            command,
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        with args.runtime_log.open("w", encoding="utf-8") as log:
            if process.stdout is not None:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
        return process.wait()
    if args.wait or not sys.platform.startswith("win"):
        return subprocess.call(command, cwd=REPO_ROOT)
    process = subprocess.Popen(command, cwd=REPO_ROOT)
    print(f"JellyFrame desktop shell started (pid={process.pid}). Use --wait for a blocking run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
