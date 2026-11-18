#!/usr/bin/env python3
"""Configure-only coverage of exact qualification locks and isolated app inputs."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[2]
ARGS = None
sys.path.insert(0, str(REPO_ROOT / "tools"))
from bootstrap_render_core import read_lock  # noqa: E402
from jellyframe_versions import active_render_core_release_version  # noqa: E402


class QualificationLockTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="qualification-lock-", dir=ARGS.work_root)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.harness = self.root / "harness"
        self.harness.mkdir()
        provider = (REPO_ROOT / "cmake/jellyframe_render_core_provider.cmake").as_posix()
        (self.harness / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(LockProbe LANGUAGES NONE)\n'
            f'include("{provider}")\n', encoding="utf-8")
        self.production = read_lock(REPO_ROOT / "cmake/jellyframe_dependency_lock.cmake")
        self.candidate = {"version": "0.6.2", "abi": "9", "source_hash": "7" * 64}

    def lock(self, identity=None, *, missing=None):
        identity = identity or self.candidate
        fields = {"version": "VERSION", "abi": "ENGINE_ABI", "source_hash": "SOURCE_HASH"}
        path = self.root / "candidate.cmake"
        path.write_text("".join(
            f'set(JELLYFRAME_RENDER_CORE_LOCKED_{fields[key]} "{value}")\n'
            for key, value in identity.items() if key != missing), encoding="utf-8")
        return path

    def package(self, identity, *, declared_version=None):
        package = self.root / "package"
        package.mkdir(exist_ok=True)
        (package / "include").mkdir(exist_ok=True)
        (package / "profile.json").write_text(json.dumps({
            "packageVersion": identity["version"], "engineAbi": int(identity["abi"])
        }), encoding="utf-8")
        (package / "manifest.json").write_text(json.dumps({
            "sourceHash": identity["source_hash"], "sourceFileCount": 641
        }), encoding="utf-8")
        (package / "JellyFrameRenderCoreConfigVersion.cmake").write_text(
            f'set(PACKAGE_VERSION "{identity["version"]}")\n'
            'if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)\n'
            '  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n'
            '  set(PACKAGE_VERSION_EXACT TRUE)\nendif()\n', encoding="utf-8")
        (package / "JellyFrameRenderCoreConfig.cmake").write_text(
            f'set(JellyFrameRenderCore_PACKAGE_VERSION "{declared_version or identity["version"]}")\n'
            f'set(JellyFrameRenderCore_ENGINE_ABI "{identity["abi"]}")\n'
            f'set(JellyFrameRenderCore_SOURCE_HASH "{identity["source_hash"]}")\n'
            'set(JellyFrameRenderCore_SOURCE_FILE_COUNT "641")\n'
            f'set(JellyFrameRenderCore_PROFILE_FILE "{package.as_posix()}/profile.json")\n'
            f'set(JellyFrameRenderCore_SOURCE_MANIFEST_FILE "{package.as_posix()}/manifest.json")\n'
            'add_library(JellyFrame::jellyframe_render_core INTERFACE IMPORTED)\n'
            'set_target_properties(JellyFrame::jellyframe_render_core PROPERTIES\n'
            f'  INTERFACE_INCLUDE_DIRECTORIES "{package.as_posix()}/include"\n'
            '  INTERFACE_COMPILE_DEFINITIONS "JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED=1")\n',
            encoding="utf-8")
        return package

    def configure(self, package, lock=None, *, provider="package", extra=(), success=True):
        command = [ARGS.cmake, "-S", str(self.harness), "-B", str(self.root / "build"),
                   f"-DJELLYFRAME_RENDER_CORE_PROVIDER={provider}",
                   f"-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR={package}",
                   f"-DJELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE={lock or ''}", *extra]
        if ARGS.generator:
            command.extend(["-G", ARGS.generator])
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
            return json.loads((self.root / "build/generated/jellyframe_render_core_provenance.json").read_text())
        self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_default_lock_remains_production(self):
        result = self.configure(self.package(self.production))
        self.assertEqual(result["lockKind"], "production")
        self.assertEqual(result["lockedVersion"], self.production["version"])
        self.assertEqual(str(result["engineAbi"]), self.production["abi"])
        self.assertTrue(result["lockEnforced"])
        with patch.dict(os.environ):
            os.environ.pop("JELLYFRAME_TEST_RENDER_CORE_VERSION", None)
            self.assertEqual(active_render_core_release_version(), self.production["version"])

    def test_candidate_success_and_return_to_default(self):
        lock = self.lock()
        result = self.configure(self.package(self.candidate), lock)
        self.assertEqual(result["lockKind"], "qualification")
        self.assertEqual(Path(result["lockFile"]), lock)
        self.assertEqual(result["sourceHash"], self.candidate["source_hash"])
        self.assertEqual(result["sourceFileCount"], 641)
        self.assertTrue(result["lockEnforced"])
        result = self.configure(self.package(self.production))
        self.assertEqual(result["lockKind"], "production")
        self.assertEqual(result["lockedVersion"], self.production["version"])

    def test_candidate_requires_explicit_lock(self):
        output = self.configure(self.package(self.candidate), success=False)
        self.assertIn(self.production["version"], output)

    def test_wrong_version_abi_and_hash_are_refused(self):
        for key, value, diagnostic in (("version", "0.6.3", "version"),
                                       ("abi", "8", "locked ABI"),
                                       ("source_hash", "8" * 64, "locked source hash")):
            with self.subTest(field=key):
                identity = {**self.candidate, key: value}
                output = self.configure(self.package(identity), self.lock(), success=False)
                self.assertIn(diagnostic, output)

    def test_declared_version_cannot_disagree_with_config_version(self):
        output = self.configure(self.package(self.candidate, declared_version="0.6.3"),
                                self.lock(), success=False)
        self.assertIn("declares version", output)

    def test_package_only_and_missing_file_constraints(self):
        package = self.package(self.candidate)
        output = self.configure(package, self.lock(), provider="in-tree", success=False)
        self.assertIn("only valid with the package", " ".join(output.split()))
        output = self.configure(package, self.root / "missing.cmake", success=False)
        self.assertIn("not a file", output)

    def test_incomplete_lock_cannot_inherit_cached_fields(self):
        package = self.package(self.candidate)
        self.configure(package, self.lock())
        for key, field in (("version", "VERSION"), ("abi", "ENGINE_ABI"), ("source_hash", "SOURCE_HASH")):
            with self.subTest(field=key):
                output = self.configure(package, self.lock(missing=key),
                                        extra=[f"-DJELLYFRAME_RENDER_CORE_LOCKED_{field}={self.candidate[key]}"],
                                        success=False)
                self.assertIn("dependency lock has no valid", output)

    def test_staging_preserves_production_sources_and_replaces_stale_inputs(self):
        sources = [self.root / name for name in ("packages", "fixtures", "system")]
        original = json.dumps({"runtime": {"minRenderCore": self.production["version"], "minJellyFrame": "0.6.0"}})
        for source in sources:
            source.mkdir()
            (source / "jellyframe.app.json").write_text(original, encoding="utf-8")
        destination = self.root / "staged"
        destination.mkdir()
        (destination / "stale.txt").write_text("stale", encoding="utf-8")
        result = subprocess.run([
            sys.executable, str(REPO_ROOT / "tests/tool_regression/stage_candidate_app_packages.py"),
            *map(str, sources), str(destination), self.candidate["version"]
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((destination / "stale.txt").exists())
        for source in sources:
            self.assertEqual((source / "jellyframe.app.json").read_text(), original)
            staged = json.loads((destination / source.name / "jellyframe.app.json").read_text())
            self.assertEqual(staged["runtime"]["minRenderCore"], self.candidate["version"])
            self.assertEqual(staged["runtime"]["minJellyFrame"], "0.6.0")

    def test_configured_sample_tests_pair_apps_and_tool_version(self):
        if not ARGS.configured_build:
            self.skipTest("no configured Runtime build supplied")
        data = json.loads(subprocess.check_output([
            ARGS.ctest, "--test-dir", str(ARGS.configured_build), "--show-only=json-v1"
        ], text=True, encoding="utf-8"))
        tests = {row["name"]: row for row in data["tests"]}
        fixture = tests.get("jellyframe_stage_candidate_app_packages")
        if fixture is None:
            self.skipTest("sample regressions are not configured")
        stage_command = fixture["command"]
        apps, version = Path(stage_command[-2]), stage_command[-1]
        environment = f"JELLYFRAME_TEST_RENDER_CORE_VERSION={version}"
        names = ["jellyframe_cli_check_" + suffix for suffix in (
            "audio_smoke_package", "service_status_package", "font_policy_package",
            "wearable_launcher_package", "band_system_shell", "canvas_smoke_package",
            "canvas_gauges_package", "watch_weather_responsive")]
        names += ["jellyframe_cli_doctor_sample_packages", "jellyframe_cli_doctor_external_trial"]
        if "jellyframe_cli_template_trial_tests" not in tests:
            self.skipTest("native examples are not configured")
        for name in names:
            with self.subTest(test=name):
                test = tests[name]
                properties = {row["name"]: row["value"] for row in test["properties"]}
                self.assertIn(environment, properties.get("ENVIRONMENT", []))
                self.assertIn("jellyframe_candidate_app_packages", properties.get("FIXTURES_REQUIRED", []))
                command = test["command"]
                flag = "--root" if "--root" in command else "--samples-dir"
                self.assertTrue(Path(command[command.index(flag) + 1]).is_relative_to(apps))
        template = {row["name"]: row["value"] for row in tests["jellyframe_cli_template_trial_tests"]["properties"]}
        self.assertIn(environment, template.get("ENVIRONMENT", []))
        # Source contracts must continue to use production metadata.
        for name in ("jellyframe_tool_package_preflight_tests", "jellyframe_documentation_freshness"):
            properties = {row["name"]: row["value"] for row in tests[name]["properties"]}
            self.assertFalse(any(value.startswith("JELLYFRAME_TEST_RENDER_CORE_VERSION=")
                                 for value in properties.get("ENVIRONMENT", [])))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator")
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--configured-build", type=Path)
    parser.add_argument("--ctest", default="ctest")
    ARGS = parser.parse_args()
    if ARGS.work_root:
        ARGS.work_root.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0]])
