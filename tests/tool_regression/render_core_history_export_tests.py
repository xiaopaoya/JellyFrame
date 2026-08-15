#!/usr/bin/env python3
"""Exercise the disposable history-preserving Render Core export."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TEST_ARGS: argparse.Namespace | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--generator")
    return parser.parse_args()


def run(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
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
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


class RenderCoreHistoryExportTests(unittest.TestCase):
    cmake: Path
    source_root: Path
    generator: str | None

    @classmethod
    def setUpClass(cls) -> None:
        if TEST_ARGS is None:
            raise RuntimeError("test arguments were not initialized")
        cls.cmake = TEST_ARGS.cmake
        cls.source_root = TEST_ARGS.source_root.resolve()
        cls.generator = TEST_ARGS.generator

    def test_export_retains_history_and_builds_standalone(self) -> None:
        export_tool = self.source_root / "project_tools" / "rehearse_render_core_history_export.py"
        with tempfile.TemporaryDirectory(prefix="jellyframe-render-core-history-") as directory:
            root = Path(directory)
            export_root = root / "jellyframe-render-core"
            run(
                [
                    sys.executable,
                    str(export_tool),
                    "--source-root",
                    str(self.source_root),
                    "--output-dir",
                    str(export_root),
                ],
                cwd=self.source_root,
            )

            self.assertTrue((export_root / ".git").is_dir())
            self.assertTrue((export_root / "CMakeLists.txt").is_file())
            self.assertTrue((export_root / "CMakePresets.json").is_file())
            self.assertTrue((export_root / "README.md").is_file())
            self.assertFalse((export_root / "src" / "app_runtime").exists())
            self.assertFalse((export_root / "ports").exists())

            archive_dir = root / "archive"
            run(
                [
                    sys.executable,
                    str(export_root / "tools" / "package_render_core_source.py"),
                    "--source-root",
                    str(export_root),
                    "--output-dir",
                    str(archive_dir),
                ],
                cwd=export_root,
            )
            self.assertEqual(len(list(archive_dir.glob("jellyframe-render-core-*.tar.gz"))), 1)

            build_root = root / "build"
            install_root = root / "install"
            configure = [str(self.cmake), "-S", str(export_root), "-B", str(build_root)]
            if self.generator:
                configure.extend(["-G", self.generator])
            configure.extend(
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_BUILD_TESTS=ON",
                    "-DJELLYFRAME_INSTALL_RENDER_CORE=ON",
                ]
            )
            run(configure, cwd=export_root)
            run([str(self.cmake), "--build", str(build_root), "--parallel"], cwd=export_root)
            run(["ctest", "--test-dir", str(build_root), "--output-on-failure"], cwd=export_root)
            run([str(self.cmake), "--install", str(build_root), "--prefix", str(install_root)],
                cwd=export_root)
            self.assertTrue((install_root / "share" / "jellyframe-render-core" /
                             "jellyframe_render_core_source_manifest.json").is_file())


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=["render_core_history_export_tests.py"])
