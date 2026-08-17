#!/usr/bin/env python3
"""Verify the standalone Render Core source archive is reproducible and usable."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest
from shutil import copy2, copytree
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


def run_failure(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    if result.returncode == 0:
        raise AssertionError(
            f"command unexpectedly succeeded: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def load_packager(path: Path):
    spec = importlib.util.spec_from_file_location("render_core_packager", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load Render Core packager: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rewrite_text_as_crlf(path: Path) -> None:
    content = path.read_bytes()
    normalized = content.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    path.write_bytes(normalized.replace(b"\n", b"\r\n"))


def packager_path(source_root: Path) -> Path:
    for relative in (
        Path("project_tools") / "package_render_core_source.py",
        Path("tools") / "package_render_core_source.py",
    ):
        candidate = source_root / relative
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Render Core source packager is missing under: {source_root}")


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

    def runtime_consumer_checkout(self, root: Path, source_hash: str) -> Path:
        """Stage a consumer fixture whose lock accepts this test archive only."""
        checkout = root / "runtime-consumer-source"
        run(["git", "clone", "--no-local", "--no-hardlinks", str(self.source_root), str(checkout)],
            cwd=self.source_root)
        lock_path = checkout / "cmake" / "jellyframe_dependency_lock.cmake"
        lock = lock_path.read_text(encoding="utf-8")
        updated_lock, substitutions = re.subn(
            r'(set\(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH\s+)'
            r'("[0-9a-f]{64}")',
            rf'\1"{source_hash}"',
            lock,
            count=1,
            flags=re.VERBOSE,
        )
        self.assertEqual(substitutions, 1, "runtime dependency lock has one source hash")
        lock_path.write_text(updated_lock, encoding="utf-8")
        return checkout

    def test_archive_is_stable_across_text_line_endings(self) -> None:
        source_packager = packager_path(self.source_root)
        packager = load_packager(source_packager)
        with tempfile.TemporaryDirectory(prefix="jellyframe-render-core-line-endings-") as directory:
            root = Path(directory)
            canonical_output = root / "canonical"
            crlf_source = root / "crlf-source"
            crlf_output = root / "crlf"

            crlf_source.mkdir()
            copy2(self.source_root / "LICENSE", crlf_source / "LICENSE")
            copytree(self.source_root / "cmake", crlf_source / "cmake")
            copytree(self.source_root / "src" / "render_core", crlf_source / "src" / "render_core")
            entry_paths = [
                packager.standalone_entry_file(
                    self.source_root,
                    self.source_root / "cmake" / "render_core_standalone_root.cmake",
                    self.source_root / "CMakeLists.txt",
                ),
                packager.standalone_entry_file(
                    self.source_root,
                    self.source_root / "cmake" / "render_core_standalone_presets.json.in",
                    self.source_root / "CMakePresets.json",
                ),
                packager.standalone_entry_file(
                    self.source_root,
                    self.source_root / "src" / "render_core" / "STANDALONE_README.md",
                    self.source_root / "README.md",
                ),
            ]
            staged_entry_paths = []
            for source_path in entry_paths:
                staged_path = crlf_source / source_path.relative_to(self.source_root)
                staged_path.parent.mkdir(parents=True, exist_ok=True)
                copy2(source_path, staged_path)
                staged_entry_paths.append(staged_path)
            text_paths = [
                crlf_source / "LICENSE",
                *packager.source_files(crlf_source),
                *staged_entry_paths,
            ]
            for path in text_paths:
                if (path.name in packager.TEXT_ARCHIVE_FILENAMES or
                        path.suffix.lower() in packager.TEXT_ARCHIVE_SUFFIXES):
                    rewrite_text_as_crlf(path)

            run([sys.executable, str(source_packager), "--source-root", str(self.source_root),
                 "--output-dir", str(canonical_output)], cwd=self.source_root)
            run([sys.executable, str(source_packager), "--source-root", str(crlf_source),
                 "--output-dir", str(crlf_output)], cwd=self.source_root)
            canonical_archive = next(canonical_output.glob("jellyframe-render-core-*.tar.gz"))
            crlf_archive = crlf_output / canonical_archive.name
            self.assertEqual(canonical_archive.read_bytes(), crlf_archive.read_bytes())

    def test_archive_ignores_untracked_source_files(self) -> None:
        packager = packager_path(self.source_root)
        with tempfile.TemporaryDirectory(prefix="jellyframe-render-core-tracked-inputs-") as directory:
            root = Path(directory)
            canonical_output = root / "canonical"
            checkout = root / "checkout"
            checkout_output = root / "checkout-output"
            run(["git", "clone", "--no-local", "--no-hardlinks", str(self.source_root), str(checkout)],
                cwd=self.source_root)
            sentinel = checkout / "src" / "render_core" / "untracked_archive_sentinel.txt"
            sentinel.write_text("must not enter an archive\n", encoding="utf-8")

            run([sys.executable, str(packager), "--source-root", str(self.source_root),
                 "--output-dir", str(canonical_output)], cwd=self.source_root)
            run([sys.executable, str(packager), "--source-root", str(checkout),
                 "--output-dir", str(checkout_output)], cwd=self.source_root)
            canonical_archive = next(canonical_output.glob("jellyframe-render-core-*.tar.gz"))
            checkout_archive = checkout_output / canonical_archive.name
            self.assertEqual(canonical_archive.read_bytes(), checkout_archive.read_bytes())
            with tarfile.open(checkout_archive, "r:gz") as archive:
                self.assertNotIn(
                    f"{checkout_archive.name.removesuffix('.tar.gz')}/src/render_core/"
                    "untracked_archive_sentinel.txt",
                    archive.getnames(),
                )

    def test_archive_is_reproducible_and_consumable(self) -> None:
        packager = packager_path(self.source_root)
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
                self.assertIn(f"{root_name}/CMakePresets.json", members)
                self.assertIn(f"{root_name}/README.md", members)
                self.assertIn(f"{root_name}/cmake/render_core_build.cmake", members)
                self.assertIn(f"{root_name}/cmake/render_core_feature_registry.csv", members)
                self.assertIn(f"{root_name}/src/render_core/tests/render_core_tests.cpp", members)
                # Python 3.14 changes the default extraction filter. The
                # archive was produced by this test, but selecting the data
                # filter now keeps the test warning-free and preserves the
                # intended regular-file extraction semantics on newer hosts.
                extraction_options: dict[str, str] = {}
                if sys.version_info >= (3, 12):
                    extraction_options["filter"] = "data"
                bundle.extractall(root / "extract", **extraction_options)

            archive_source = root / "extract" / root_name
            install_dir = root / "install"
            run([str(self.cmake), "--preset", "default"], cwd=archive_source)
            core_build = archive_source / "build" / "default"
            source_manifest = json.loads(
                (core_build / "generated" / "jellyframe_render_core_source_manifest.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertTrue(source_manifest["files"])
            for entry in source_manifest["files"]:
                self.assertIn(f"{root_name}/{entry['path']}", members)

            run([str(self.cmake), "--preset", "minimal"], cwd=archive_source)
            minimal_build = archive_source / "build" / "minimal"
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
            run([str(self.cmake), "--build", "--preset", "default", "--parallel"],
                cwd=archive_source)
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

            # The Core repository intentionally does not contain App Runtime.
            # Its independent CI proves the archive's standalone lifecycle;
            # JellyFrame's monorepo CI additionally proves this package is a
            # compatible Runtime dependency.
            if not (self.source_root / "src" / "app_runtime").is_dir():
                return

            runtime_consumer_source = self.runtime_consumer_checkout(root, source_manifest["sourceHash"])

            # A downstream Core host must consume only the installed CMake
            # package and installed render_core headers. This keeps the
            # package boundary independently verifiable instead of proving it
            # only through the Runtime's source-tree integration.
            consumer_source = root / "render-core-package-consumer-source"
            consumer_source.mkdir()
            consumer_source.joinpath("CMakeLists.txt").write_text(
                "\n".join(
                    [
                        "cmake_minimum_required(VERSION 3.16)",
                        "project(RenderCorePackageConsumer LANGUAGES CXX)",
                        "find_package(JellyFrameRenderCore 0.6.0 EXACT CONFIG REQUIRED",
                        f"  PATHS \"{install_dir.as_posix()}\" NO_DEFAULT_PATH)",
                        "add_executable(render_core_package_consumer main.cpp)",
                        "target_link_libraries(render_core_package_consumer",
                        "  PRIVATE JellyFrame::jellyframe_render_core)",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            consumer_source.joinpath("main.cpp").write_text(
                "\n".join(
                    [
                        "#include <string>",
                        "#include \"render_core/html_parser.h\"",
                        "int main() {",
                        "  jellyframe::HtmlParser parser;",
                        "  const auto document = parser.parse(std::string(\"<body>package consumer</body>\"));",
                        "  return document ? 0 : 1;",
                        "}",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            consumer_build = root / "render-core-package-consumer-build"
            self.cmake_configure(
                consumer_source,
                consumer_build,
                ["-DCMAKE_BUILD_TYPE=Release"],
            )
            run(
                [str(self.cmake), "--build", str(consumer_build), "--config", "Release", "--parallel"],
                cwd=self.source_root,
            )
            consumer_executable = consumer_build / (
                "render_core_package_consumer.exe" if sys.platform.startswith("win")
                else "render_core_package_consumer"
            )
            run([str(consumer_executable)], cwd=self.source_root)

            runtime_build = root / "runtime-package-consumer"
            self.cmake_configure(
                runtime_consumer_source,
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
            package_provenance = json.loads(
                (runtime_build / "generated" /
                 "jellyframe_render_core_provenance.json").read_text(encoding="utf-8")
            )
            self.assertEqual(package_provenance["sourceHash"], source_manifest["sourceHash"])
            self.assertEqual(package_provenance["lockedSourceHash"], source_manifest["sourceHash"])
            self.assertTrue(package_provenance["lockEnforced"])
            run(
                [str(self.cmake), "--build", str(runtime_build), "--config", "Release",
                 "--target", "jellyframe_app_runtime_tests", "--parallel"],
                cwd=runtime_consumer_source,
            )
            run(
                ["ctest", "--test-dir", str(runtime_build), "-C", "Release",
                 "-R", "^jellyframe_app_runtime_tests$", "--output-on-failure"],
                cwd=runtime_consumer_source,
            )

            source_override_build = root / "runtime-source-override"
            self.cmake_configure(
                runtime_consumer_source,
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
                cwd=runtime_consumer_source,
            )
            run(
                ["ctest", "--test-dir", str(source_override_build), "-C", "Release",
                 "-R", "^jellyframe_app_runtime_tests$", "--output-on-failure"],
                cwd=runtime_consumer_source,
            )

            # A package with the same version and ABI but another declared source
            # identity must fail before Runtime targets can be generated.
            mismatched_install_dir = root / "install-mismatched-source"
            copytree(install_dir, mismatched_install_dir)
            mismatched_hash = "a" * 64
            package_config = next(mismatched_install_dir.glob(
                "**/JellyFrameRenderCoreConfig.cmake"))
            installed_source_manifest = (mismatched_install_dir / "share" /
                                         "jellyframe-render-core" /
                                         "jellyframe_render_core_source_manifest.json")
            for file_path in (package_config, installed_source_manifest):
                content = file_path.read_text(encoding="utf-8")
                self.assertIn(source_manifest["sourceHash"], content)
                file_path.write_text(content.replace(source_manifest["sourceHash"], mismatched_hash),
                                     encoding="utf-8")
            mismatch_build = root / "runtime-package-mismatched-source"
            mismatch_command = [
                str(self.cmake), "-S", str(runtime_consumer_source), "-B", str(mismatch_build),
            ]
            if self.generator:
                mismatch_command.extend(["-G", self.generator])
            mismatch_command.extend(
                [
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DJELLYFRAME_RENDER_CORE_PROVIDER=package",
                    f"-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR={mismatched_install_dir}",
                    "-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF",
                    "-DJELLYFRAME_BUILD_SCRIPTING=OFF",
                    "-DJELLYFRAME_BUILD_EXAMPLES=OFF",
                    "-DJELLYFRAME_BUILD_BENCHMARKS=OFF",
                    "-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF",
                    "-DJELLYFRAME_BUILD_TESTS=OFF",
                ]
            )
            result = run_failure(
                mismatch_command,
                cwd=runtime_consumer_source,
            )
            self.assertRegex(
                result.stdout + result.stderr,
                r"source hash[\s\S]*does not[\s\S]*match locked source hash",
            )


if __name__ == "__main__":
    TEST_ARGS = parse_args()
    unittest.main(argv=["render_core_source_archive_tests.py"])
