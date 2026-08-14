#!/usr/bin/env python3
"""Verify the standalone Render Core source archive is reproducible and usable."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tarfile
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


class RenderCoreSourceArchiveTests(unittest.TestCase):
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

    def cmake_configure(self, source_dir: Path, build_dir: Path, definitions: list[str]) -> None:
        command = [str(self.cmake), "-S", str(source_dir), "-B", str(build_dir)]
        if self.generator:
            command.extend(["-G", self.generator])
        command.extend(definitions)
        run(command, cwd=self.source_root)

    def test_archive_is_reproducible_and_consumable(self) -> None:
        packager = self.source_root / "project_tools" / "package_render_core_source.py"
        with tempfile.TemporaryDirectory(prefix="jellyframe-render-core-archive-") as directory:
            root = Path(directory)
            first_output = root / "first"
            second_output = root / "second"
            run([sys.executable, str(packager), "--source-root", str(self.source_root),
                 "--output-dir", str(first_output)], cwd=self.source_root)
            run([sys.executable, str(packager), "--source-root", str(self.source_root),
                 "--output-dir", str(second_output)], cwd=self.source_root)

            archives = sorted(first_output.glob("jellyframe-render-core-*.tar.gz"))
            self.assertEqual(len(archives), 1)
            archive = archives[0]
            matching_archive = second_output / archive.name
            self.assertEqual(hashlib.sha256(archive.read_bytes()).digest(),
                             hashlib.sha256(matching_archive.read_bytes()).digest())
            expected_checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertEqual(
                archive.with_suffix(archive.suffix + ".sha256").read_text(encoding="ascii"),
                f"{expected_checksum}  {archive.name}\n",
            )

            with tarfile.open(archive, "r:gz") as bundle:
                members = bundle.getnames()
                self.assertEqual(members, sorted(members))
                root_name = archive.name.removesuffix(".tar.gz")
                self.assertIn(f"{root_name}/CMakeLists.txt", members)
                self.assertIn(f"{root_name}/README.md", members)
                self.assertIn(f"{root_name}/cmake/render_core_build.cmake", members)
                self.assertIn(f"{root_name}/cmake/render_core_feature_registry.csv", members)
                self.assertIn(f"{root_name}/src/render_core/tests/render_core_tests.cpp", members)
                bundle.extractall(root / "extract")

            archive_source = root / "extract" / root_name
            core_build = root / "core-build"
            install_dir = root / "install"
            self.cmake_configure(
                archive_source,
                core_build,
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_BUILD_TESTS=ON",
                    "-DJELLYFRAME_INSTALL_RENDER_CORE=ON",
                ],
            )
            source_manifest = json.loads(
                (core_build / "generated" / "jellyframe_render_core_source_manifest.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertTrue(source_manifest["files"])
            for entry in source_manifest["files"]:
                self.assertIn(f"{root_name}/{entry['path']}", members)

            minimal_build = root / "core-minimal-build"
            self.cmake_configure(
                archive_source,
                minimal_build,
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_BUILD_TESTS=OFF",
                    "-DJELLYFRAME_ENABLE_CANVAS2D=OFF",
                    "-DJELLYFRAME_ENABLE_MODERN_PAINT=OFF",
                    "-DJELLYFRAME_ENABLE_FLEX_GRID=OFF",
                    "-DJELLYFRAME_ENABLE_ADVANCED_FORMS=OFF",
                ],
            )
            minimal_profile = json.loads(
                (minimal_build / "generated" / "jellyframe_render_core_profile.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(minimal_profile["profileId"],
                             "render-core-minimal-no-forms-advanced")
            self.assertEqual(minimal_profile["features"], ["core.document", "core.paint"])
            minimal_manifest = json.loads(
                (minimal_build / "generated" /
                 "jellyframe_render_core_source_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(minimal_manifest["sourceHash"], source_manifest["sourceHash"])
            run([str(self.cmake), "--build", str(core_build), "--config", "Release", "--parallel"],
                cwd=self.source_root)
            run(["ctest", "--test-dir", str(core_build), "-C", "Release", "--output-on-failure"],
                cwd=self.source_root)
            run([str(self.cmake), "--install", str(core_build), "--config", "Release",
                 "--prefix", str(install_dir)],
                cwd=self.source_root)
            installed_manifest = json.loads(
                (install_dir / "share" / "jellyframe-render-core" /
                 "jellyframe_render_core_source_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(installed_manifest, source_manifest)

            runtime_build = root / "runtime-package-consumer"
            self.cmake_configure(
                self.source_root,
                runtime_build,
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_RENDER_CORE_PROVIDER=package",
                    f"-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR={install_dir}",
                    "-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF",
                    "-DJELLYFRAME_BUILD_SCRIPTING=OFF",
                    "-DJELLYFRAME_BUILD_EXAMPLES=OFF",
                    "-DJELLYFRAME_BUILD_BENCHMARKS=OFF",
                    "-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF",
                    "-DJELLYFRAME_BUILD_TESTS=ON",
                ],
            )
            self.assertTrue(
                (runtime_build / "generated" / "jellyframe_render_core_source_manifest.json").is_file()
            )
            run(
                [str(self.cmake), "--build", str(runtime_build), "--config", "Release",
                 "--target", "jellyframe_app_runtime_tests", "--parallel"],
                cwd=self.source_root,
            )
            run(
                ["ctest", "--test-dir", str(runtime_build), "-C", "Release",
                 "-R", "^jellyframe_app_runtime_tests$", "--output-on-failure"],
                cwd=self.source_root,
            )

            source_override_build = root / "runtime-source-override"
            self.cmake_configure(
                self.source_root,
                source_override_build,
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_RENDER_CORE_PROVIDER=in-tree",
                    f"-DJELLYFRAME_RENDER_CORE_SOURCE_DIR={archive_source}",
                    "-DJELLYFRAME_BUILD_SCRIPTING=OFF",
                    "-DJELLYFRAME_BUILD_EXAMPLES=OFF",
                    "-DJELLYFRAME_BUILD_BENCHMARKS=OFF",
                    "-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF",
                    "-DJELLYFRAME_BUILD_TESTS=ON",
                ],
            )
            override_provenance = json.loads(
                (source_override_build / "generated" /
                 "jellyframe_render_core_provenance.json").read_text(encoding="utf-8")
            )
            self.assertEqual(override_provenance["provider"], "source-override")
            self.assertEqual(override_provenance["sourceHash"], source_manifest["sourceHash"])
            run(
                [str(self.cmake), "--build", str(source_override_build), "--config", "Release",
                 "--target", "jellyframe_app_runtime_tests", "--parallel"],
                cwd=self.source_root,
            )
            run(
                ["ctest", "--test-dir", str(source_override_build), "-C", "Release",
                 "-R", "^jellyframe_app_runtime_tests$", "--output-on-failure"],
                cwd=self.source_root,
            )


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=["render_core_source_archive_tests.py"])
