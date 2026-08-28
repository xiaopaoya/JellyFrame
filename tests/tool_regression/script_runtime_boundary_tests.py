#!/usr/bin/env python3
"""Regression checks for the compile-time script backend boundary."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TEST_ARGS: argparse.Namespace | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--generator")
    return parser.parse_args()


class ScriptRuntimeBoundaryTests(unittest.TestCase):
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

    def read(self, relative_path: str) -> str:
        return (self.source_root / relative_path).read_text(encoding="utf-8-sig")

    def source_files(self, relative_directory: str) -> list[Path]:
        root = self.source_root / relative_directory
        return sorted(
            path for path in root.rglob("*")
            if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}
        )

    def test_common_consumers_do_not_depend_on_the_current_backend_type(self) -> None:
        contract = self.read("src/script/script_runtime.h")
        worker = self.read("src/script/script_task_worker_runtime.h")
        desktop = self.read("tools/native/win32_browser.cpp")
        esp32_component = self.read("ports/esp32s3-idf/components/jellyframe_app_runtime/CMakeLists.txt")
        cmake = self.read("CMakeLists.txt")

        self.assertIn("class ScriptRuntime", contract)
        self.assertIn("create_script_runtime", contract)
        self.assertNotIn("JerryScript", contract)
        self.assertNotIn("jerry_value_t", contract)
        self.assertIn('#include "script/script_runtime.h"', worker)
        self.assertNotIn("JerryScriptRuntime", worker)
        self.assertIn('#include "script/script_runtime.h"', desktop)
        self.assertNotIn("JerryScriptRuntime", desktop)
        self.assertIn("JELLYFRAME_SCRIPT_ENGINE", cmake)
        self.assertIn("jellyframe_script_backend_jerryscript.cmake", cmake)
        self.assertIn("JELLYFRAME_SCRIPT_ENGINE", esp32_component)
        self.assertIn("JELLYFRAME_SCRIPT_BACKEND_SOURCES", esp32_component)
        self.assertIn("currently provides only the jerryscript script backend", esp32_component)
        self.assertRegex(
            cmake,
            r"(?s)target_include_directories\(jellyframe_script.*?PRIVATE\s+\$\{JELLYFRAME_SCRIPT_BACKEND_INCLUDE_DIRS\}",
        )
        self.assertRegex(
            cmake,
            r"(?s)target_link_libraries\(jellyframe_script.*?PRIVATE\s+\$\{JELLYFRAME_SCRIPT_BACKEND_LIBRARIES\}",
        )

    def test_backend_symbols_do_not_leak_into_common_consumers(self) -> None:
        allowed = {
            self.source_root / "src/script/jerryscript_runtime.cpp",
            self.source_root / "src/script/jerryscript_runtime.h",
            self.source_root / "src/script/tests/script_runtime_tests.cpp",
        }
        sources = (
            self.source_files("src/app_runtime")
            + self.source_files("src/script")
            + self.source_files("tools/native")
        )
        forbidden = re.compile(r"jerryscript_runtime\.h|JerryScriptRuntime|jerry_[A-Za-z0-9_]+|jerry_value_t")
        leaks: list[str] = []
        for source in sources:
            if source in allowed:
                continue
            match = forbidden.search(source.read_text(encoding="utf-8-sig"))
            if match:
                leaks.append(f"{source.relative_to(self.source_root)}: {match.group(0)}")
        self.assertEqual([], leaks, "backend-private symbols leaked into a common consumer")

    def test_unknown_backend_fails_before_backend_discovery(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-script-engine-") as directory:
            command = [
                str(self.cmake),
                "-S", str(self.source_root),
                "-B", str(Path(directory) / "invalid-engine"),
            ]
            if self.generator:
                command.extend(["-G", self.generator])
            command.extend([
                "-DJELLYFRAME_BUILD_APP_RUNTIME=ON",
                "-DJELLYFRAME_BUILD_SCRIPTING=ON",
                "-DJELLYFRAME_SCRIPT_ENGINE=unsupported",
            ])
            result = subprocess.run(command, text=True, capture_output=True, check=False)
            output = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn("Unsupported JELLYFRAME_SCRIPT_ENGINE='unsupported'", output)
            self.assertNotIn("JERRYSCRIPT_INCLUDE_DIR", output)


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=[sys.argv[0]])
