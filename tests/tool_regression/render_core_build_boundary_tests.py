#!/usr/bin/env python3
"""Regression coverage for the Render Core and Device contracts build boundary."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TEST_ARGS: argparse.Namespace | None = None


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    """Capture tool output without making the test depend on the host code page."""
    return subprocess.run(
        command,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--generator")
    return parser.parse_args()


class RenderCoreBuildBoundaryTests(unittest.TestCase):
    cmake: Path
    generator: str | None
    source_root: Path

    @classmethod
    def setUpClass(cls) -> None:
        if TEST_ARGS is None:
            raise RuntimeError("test arguments were not initialized")
        cls.cmake = TEST_ARGS.cmake
        cls.generator = TEST_ARGS.generator
        cls.source_root = TEST_ARGS.source_root

    def configure(self, build_dir: Path, definitions: list[str]) -> None:
        command = [str(self.cmake), "-S", str(self.source_root), "-B", str(build_dir)]
        if self.generator:
            command.extend(["-G", self.generator])
        command.extend(definitions)
        result = run_capture(command)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def build_target(self, build_dir: Path, target: str, *, succeeds: bool) -> None:
        result = run_capture([
            str(self.cmake), "--build", str(build_dir), "--target", target, "--parallel"
        ])
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def ctest_names(self, build_dir: Path) -> str:
        result = run_capture(["ctest", "--test-dir", str(build_dir), "-N"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_core_only_and_contracts_only_builds_are_explicit(self) -> None:
        common = [
            "-DCMAKE_BUILD_TYPE=Release",
            "-DJELLYFRAME_BUILD_SCRIPTING=OFF",
            "-DJELLYFRAME_BUILD_EXAMPLES=OFF",
            "-DJELLYFRAME_BUILD_BENCHMARKS=OFF",
            "-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF",
            "-DJELLYFRAME_BUILD_TESTS=ON",
        ]
        with tempfile.TemporaryDirectory(prefix="jellyframe-build-boundary-") as directory:
            root = Path(directory)
            core_only = root / "core-only"
            self.configure(
                core_only,
                common + [
                    "-DJELLYFRAME_BUILD_APP_RUNTIME=OFF",
                    "-DJELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS=OFF",
                    "-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=ON",
                ],
            )
            self.build_target(core_only, "jellyframe_render_core_tests", succeeds=True)
            self.build_target(core_only, "jellyframe_device_runtime_contracts", succeeds=False)
            core_tests = self.ctest_names(core_only)
            self.assertIn("jellyframe_render_core_tests", core_tests)
            self.assertNotIn("jellyframe_device_runtime_contracts", core_tests)
            self.assertNotIn("jellyframe_device_reference_cli_tests", core_tests)
            self.assertNotIn("jellyframe_device_provider_contract_tests", core_tests)

            contracts_only = root / "contracts-only"
            self.configure(
                contracts_only,
                common + [
                    "-DJELLYFRAME_BUILD_APP_RUNTIME=OFF",
                    "-DJELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS=ON",
                    "-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF",
                ],
            )
            self.build_target(contracts_only, "jellyframe_device_runtime_contracts_tests", succeeds=True)
            contracts_tests = self.ctest_names(contracts_only)
            self.assertIn("jellyframe_device_runtime_contracts_tests", contracts_tests)
            self.assertIn("jellyframe_device_reference_cli_tests", contracts_tests)
            self.assertIn("jellyframe_device_provider_contract_tests", contracts_tests)
            self.assertNotIn("jellyframe_app_runtime_tests", contracts_tests)


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=[sys.argv[0]])
