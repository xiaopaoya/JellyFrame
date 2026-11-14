#!/usr/bin/env python3
"""Regression coverage for the standalone WS147 Developer Image release."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGER = ROOT / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "package_ws147_device_provider.py"
sys.path.insert(0, str(ROOT / "tools"))
import device_image_manifest  # noqa: E402


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Ws147DeviceProviderReleaseTests(unittest.TestCase):
    def test_release_is_self_contained_reproducible_and_fully_hashed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-release-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            output = root / "release"
            firmware.write_bytes(b"JellyFrame fixture firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)

            revision = "a" * 40
            version = "0.6.2-ws147.test"
            completed = subprocess.run([
                sys.executable, str(PACKAGER),
                "--firmware", str(firmware),
                "--factory-image", str(factory),
                "--source-revision", revision,
                "--image-version", version,
                "--output", str(output),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            result = json.loads(completed.stdout)
            release = Path(result["release"])
            archive_path = Path(result["archive"])
            self.assertTrue(release.is_dir())
            self.assertTrue(archive_path.is_file())

            release_files = {
                path.relative_to(release).as_posix(): path
                for path in release.rglob("*") if path.is_file()
            }
            self.assertNotIn("provider/jellyframe-device.config.json", release_files)
            self.assertFalse(any("__pycache__" in name or name.endswith(".pyc") for name in release_files))

            checksum_path = release / "SHA256SUMS.txt"
            listed: dict[str, str] = {}
            for line in checksum_path.read_text(encoding="ascii").splitlines():
                digest, separator, relative = line.partition("  ")
                self.assertEqual(separator, "  ")
                self.assertRegex(digest, r"^[0-9a-f]{64}$")
                self.assertNotIn(relative, listed)
                listed[relative] = digest
            self.assertEqual(set(listed), set(release_files) - {"SHA256SUMS.txt"})
            for relative, expected in listed.items():
                self.assertEqual(sha256(release_files[relative]), expected, relative)

            manifest_path = release / "developer-image" / "ws147-developer-image.manifest.json"
            manifest = device_image_manifest.parse_device_image_manifest(manifest_path.read_bytes())
            self.assertEqual(manifest["imageVersion"], version)
            self.assertEqual(manifest["source"]["revision"], revision)
            self.assertEqual(manifest["source"]["firmwareSha256"], sha256(firmware))
            self.assertEqual(manifest["recovery"]["factoryImageSha256"], sha256(factory))
            self.assertEqual(manifest["renderCore"], {"version": "0.6.2", "abi": 1})

            prefix = f"{release.name}/"
            with zipfile.ZipFile(archive_path) as archive:
                entries = {entry.filename: entry for entry in archive.infolist() if not entry.is_dir()}
                self.assertEqual(set(entries), {prefix + relative for relative in release_files})
                for relative, source_path in release_files.items():
                    digest = hashlib.sha256()
                    with archive.open(entries[prefix + relative]) as source:
                        for chunk in iter(lambda: source.read(1024 * 1024), b""):
                            digest.update(chunk)
                    self.assertEqual(digest.hexdigest(), sha256(source_path), relative)

    def test_release_imports_abi4_identity_from_authoritative_build_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-abi4-release-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            build_manifest = root / "build-manifest.json"
            firmware.write_bytes(b"ABI4 fixture firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            build_manifest.write_text(json.dumps({
                "sources": {"renderCore": {
                    "packageVersion": "0.9.0-dev", "engineAbi": 4,
                    "sourceHash": "e" * 64, "sourceFileCount": 163,
                }},
                "idfVersion": "5.3.1",
                "toolchain": "xtensa-esp-elf 13.2.0",
                "buildProfile": "ws147-core-text",
            }), encoding="utf-8")
            completed = subprocess.run([
                sys.executable, str(PACKAGER),
                "--firmware", str(firmware),
                "--factory-image", str(factory),
                "--source-revision", "b" * 40,
                "--image-version", "0.6.2-ws147.abi4-test",
                "--build-manifest", str(build_manifest),
                "--output", str(root / "release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest = device_image_manifest.parse_device_image_manifest(
                (release / "developer-image" / "ws147-developer-image.manifest.json").read_bytes())
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": 4})
            self.assertEqual(manifest["buildProvenance"], {
                "renderCoreSourceIdentity": "e" * 64,
                "renderCoreSourceInputCount": 163,
                "idfVersion": "5.3.1",
                "toolchain": "xtensa-esp-elf 13.2.0",
                "buildProfile": "ws147-core-text",
            })

    def test_explicit_abi4_metadata_is_supported_and_conflicts_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-explicit-abi4-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            firmware.write_bytes(b"ABI4 fixture firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            common = [
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), "--source-revision", "c" * 40,
                "--image-version", "0.6.2-ws147.abi4-explicit",
                "--render-core-version", "0.9.0-dev", "--render-core-abi", "4",
            ]
            completed = subprocess.run([*common, "--output", str(root / "explicit")], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest = device_image_manifest.parse_device_image_manifest(
                (release / "developer-image" / "ws147-developer-image.manifest.json").read_bytes())
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": 4})

            build_manifest = root / "build-manifest.json"
            build_manifest.write_text(json.dumps({
                "sources": {"renderCore": {"packageVersion": "0.9.0-dev", "engineAbi": 4}}
            }), encoding="utf-8")
            conflict = subprocess.run([
                *common, "--render-core-version", "0.6.2", "--render-core-abi", "1",
                "--build-manifest", str(build_manifest), "--output", str(root / "conflict"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(conflict.returncode, 0)
            self.assertIn(b"conflicts with build manifest", conflict.stderr)

            partial = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), "--source-revision", "d" * 40,
                "--image-version", "0.6.2-ws147.partial", "--render-core-abi", "4",
                "--output", str(root / "partial"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(partial.returncode, 0)
            self.assertIn(b"must be supplied together", partial.stderr)

    def test_build_manifest_without_optional_provenance_does_not_claim_it(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-provenance-optional-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            build_manifest = root / "build-manifest.json"
            firmware.write_bytes(b"firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            build_manifest.write_text(json.dumps({
                "sources": {"renderCore": {"packageVersion": "0.9.0-dev", "engineAbi": 4}}
            }), encoding="utf-8")
            completed = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), "--source-revision", "f" * 40,
                "--image-version", "0.6.2-ws147.partial-provenance",
                "--build-manifest", str(build_manifest), "--output", str(root / "release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest = device_image_manifest.parse_device_image_manifest(
                (release / "developer-image" / "ws147-developer-image.manifest.json").read_bytes())
            self.assertNotIn("buildProvenance", manifest)

    def test_build_dir_imports_and_cross_checks_idf_provenance(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-build-dir-") as directory:
            root = Path(directory)
            build = root / "build"
            generated = build / "esp-idf" / "jellyframe_render_core" / "generated"
            generated.mkdir(parents=True)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            firmware.write_bytes(b"firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            identity = "a" * 64
            (generated / "jellyframe_render_core_port_provenance.txt").write_text(
                f"package=0.9.0-dev\nabi=4\nprofile=render-core-default\nsource={identity}\nfiles=163\n",
                encoding="utf-8")
            (generated / "jellyframe_render_core_source_manifest.json").write_text(json.dumps({
                "sourceHash": identity, "sourceFileCount": 163, "files": [],
            }), encoding="utf-8")
            (generated / "jellyframe_render_core_profile.json").write_text(json.dumps({
                "packageVersion": "0.9.0-dev", "engineAbi": 4, "profileId": "render-core-default",
            }), encoding="utf-8")
            (build / "project_description.json").write_text(json.dumps({
                "build_dir": str(build), "git_revision": "deadbeef", "target": "esp32s3",
                "idf_path": str(root / "idf"), "app_bin": "jellyframe_esp32s3_bench.bin",
                "c_compiler": r"C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\bin\xtensa-esp32s3-elf-gcc.exe",
            }), encoding="utf-8")
            idf_version_file = root / "idf" / "tools" / "cmake" / "version.cmake"
            idf_version_file.parent.mkdir(parents=True)
            idf_version_file.write_text(
                "set(IDF_VERSION_MAJOR 5)\nset(IDF_VERSION_MINOR 3)\nset(IDF_VERSION_PATCH 1)\n",
                encoding="utf-8")
            app_bin = build / "jellyframe_esp32s3_bench.bin"
            app_bin.write_bytes(firmware.read_bytes())
            (build / "CMakeCache.txt").write_text(
                f"IDF_TARGET:STRING=esp32s3\nSDKCONFIG_DEFAULTS:UNINITIALIZED={root / 'sdkconfig.defaults'};{root / 'sdkconfig.ws147_core_text.defaults'}\n",
                encoding="utf-8")
            sdkconfig = build / "sdkconfig"
            sdkconfig.write_text('CONFIG_IDF_TARGET="esp32s3"\n', encoding="utf-8")
            common = [
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), "--source-revision", "a" * 40,
                "--image-version", "0.6.2-ws147.build-dir-test", "--build-dir", str(build),
            ]
            completed = subprocess.run([*common, "--output", str(root / "release")], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest_path = release / "developer-image" / "ws147-developer-image.manifest.json"
            manifest = device_image_manifest.parse_device_image_manifest(manifest_path.read_bytes())
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": 4})
            self.assertEqual(manifest["buildProvenance"], {
                "renderCoreSourceIdentity": identity,
                "renderCoreSourceInputCount": 163,
                "idfVersion": "5.3.1",
                "idfRevision": "deadbeef",
                "toolchain": "esp-13.2.0_20240530",
                "target": "esp32s3",
                "buildProfile": "ws147_core_text;render-core-default",
                "sdkconfigSha256": sha256(sdkconfig),
            })
            bundled_parser = release / "provider" / "lib" / "device_image_manifest.py"
            self.assertTrue(bundled_parser.is_file())

            mismatched_firmware = root / "mismatched-firmware.bin"
            mismatched_firmware.write_bytes(b"different firmware\n")
            rejected_firmware = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(mismatched_firmware),
                "--factory-image", str(factory), "--source-revision", "a" * 40,
                "--image-version", "0.6.2-ws147.mismatched-build-bin", "--build-dir", str(build),
                "--output", str(root / "mismatched-firmware-release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(rejected_firmware.returncode, 0)
            self.assertIn(b"does not match the app BIN", rejected_firmware.stderr)

            (generated / "jellyframe_render_core_source_manifest.json").write_text(json.dumps({
                "sourceHash": "b" * 64, "sourceFileCount": 163, "files": [],
            }), encoding="utf-8")
            rejected = subprocess.run([*common, "--output", str(root / "rejected")], cwd=ROOT,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b"identities disagree", rejected.stderr)

            (generated / "jellyframe_render_core_source_manifest.json").write_text("{}", encoding="utf-8")
            missing = subprocess.run([*common, "--output", str(root / "missing")], cwd=ROOT,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn(b"missing required identity fields", missing.stderr)


if __name__ == "__main__":
    unittest.main()
