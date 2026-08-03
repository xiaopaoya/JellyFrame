#!/usr/bin/env python3
"""Verify the dedicated Flex/Grid ON desktop capture fixture."""

import subprocess
import sys
import tempfile
from pathlib import Path

from win32_browser_cli_tests import read_bmp_rgb


REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = REPO_ROOT / "tests" / "fixtures" / "apps" / "jelly_flex_grid_probe"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: flex_grid_capture_tests.py PATH_TO_EXE")
        return 2

    executable = Path(sys.argv[1])
    require(executable.exists(), f"missing executable: {executable}")
    with tempfile.TemporaryDirectory(prefix="jellyframe-flex-grid-capture-") as directory:
        result = subprocess.run(
            [
                str(executable),
                "--app",
                str(FIXTURE),
                "--capture-frames",
                directory,
                "--frame-count",
                "2",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        require(result.returncode == 0, "Flex/Grid ON capture must succeed")
        require("diagnostics: 0" in result.stdout,
                "Flex/Grid ON fixture must not emit unsupported-feature diagnostics")
        frame = Path(directory) / "frame_000.bmp"
        require(frame.is_file(), "Flex/Grid ON capture must produce frame_000.bmp")

        # Sample solid interiors, away from labels and edges. These positions
        # distinguish flex order and the two-column grid placement.
        require(read_bmp_rgb(frame, 80, 75) == (102, 76, 52),
                "ON flex order must place SECOND in the first slot")
        require(read_bmp_rgb(frame, 150, 75) == (49, 92, 75),
                "ON flex order must place FIRST in the second slot")
        require(read_bmp_rgb(frame, 250, 75) == (42, 80, 97),
                "ON flex order must place THIRD in the third slot")
        require(read_bmp_rgb(frame, 120, 185) == (42, 80, 97),
                "ON grid wide item must span both columns")
        require(read_bmp_rgb(frame, 120, 225) == (49, 92, 75),
                "ON grid A item must occupy the first column")
        require(read_bmp_rgb(frame, 220, 225) == (102, 76, 52),
                "ON grid B item must occupy the second column")

    print("Flex/Grid ON capture passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flex/grid capture test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
