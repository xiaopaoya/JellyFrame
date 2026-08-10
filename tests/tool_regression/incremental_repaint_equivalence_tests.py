#!/usr/bin/env python3
"""Verify that retained dirty repaint is pixel-identical to full repaint."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run_capture(shell: Path, repo: Path, output: Path, force_full: bool) -> None:
    command = [
        str(shell),
        "--app",
        str(repo / "samples" / "apps" / "packages" / "jelly_motion_lab"),
        "--capture-frames",
        str(output),
        "--frame-count",
        "30",
        "--frame-step-ms",
        "33",
        "--frame-start-ms",
        "1000",
        "--viewport-width",
        "300",
        "--viewport-height",
        "300",
        "--frame-event",
        "8:click:150:260",
    ]
    if force_full:
        command.append("--force-full-repaint")
    result = subprocess.run(command, cwd=repo, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"capture failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: incremental_repaint_equivalence_tests.py SHELL REPO", file=sys.stderr)
        return 2
    shell = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-repaint-") as temporary:
        root = Path(temporary)
        incremental = root / "incremental"
        full = root / "full"
        run_capture(shell, repo, incremental, False)
        run_capture(shell, repo, full, True)

        incremental_frames = sorted(incremental.glob("frame_*.bmp"))
        full_frames = sorted(full.glob("frame_*.bmp"))
        if len(incremental_frames) != 30 or len(full_frames) != 30:
            raise RuntimeError(
                f"expected 30 frames per mode, got {len(incremental_frames)} and {len(full_frames)}"
            )
        for index, (incremental_frame, full_frame) in enumerate(
            zip(incremental_frames, full_frames)
        ):
            if incremental_frame.read_bytes() != full_frame.read_bytes():
                raise RuntimeError(
                    f"incremental repaint diverged from full repaint at frame {index}: "
                    f"{incremental_frame.name}"
                )

    print("incremental repaint matches full repaint for 30 deterministic frames")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"incremental repaint equivalence test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
