#!/usr/bin/env python3
"""Regression coverage for the standalone WS147 Developer Image release."""

from __future__ import annotations

import hashlib
import json
import struct
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


def write_build_evidence(root: Path, firmware: Path, *, revision: str, image_version: str,
                         core_version: str, core_abi: int,
                         families: list[str] | None = None,
                         profiles: dict[str, dict] | None = None) -> tuple[Path, Path]:
    core_families = families or [
        "core.document", "core.paint", "forms.advanced", "css.flex-grid",
        "css.modern-paint", "graphics.canvas2d",
    ]
    evidence_dir = root / "build-evidence"
    evidence_dir.mkdir(exist_ok=True)
    evidenced_firmware = evidence_dir / "firmware.bin"
    evidenced_firmware.write_bytes(firmware.read_bytes())
    worker_profile = {
        "imageVersion": image_version,
        "features": ["device-image-lifecycle-acceptance", "script-task-runtime"],
        "bin": {"path": "firmware.bin", "sha256": sha256(evidenced_firmware)},
    }
    build_profiles = {"installedWorker": worker_profile}
    if profiles:
        build_profiles.update(profiles)
    build_manifest = evidence_dir / "build-manifest.json"
    build_manifest.write_text(json.dumps({
        "schemaVersion": 1,
        "target": "esp32s3",
        "board": "Waveshare ESP32-S3-Touch-LCD-1.47",
        "runtime": {"buildCommit": revision},
        "renderCore": {
            "version": core_version, "engineAbi": core_abi,
            "sourceHashSha256": "a" * 64, "sourceFileCount": 163,
        },
        "espIdf": {"version": "5.3.1"},
        "toolchain": {"xtensaCompiler": "GNU 13.2.0 (esp-13.2.0_20240530)"},
        "profiles": build_profiles,
    }), encoding="utf-8")
    core_profile = evidence_dir / "jellyframe_render_core_profile.json"
    core_profile.write_text(json.dumps({
        "schemaVersion": 1,
        "packageVersion": core_version,
        "engineAbi": core_abi,
        "profileId": "render-core-default",
        "features": core_families,
    }), encoding="utf-8")
    return build_manifest, core_profile


def identity_args(build_manifest: Path, core_profile: Path,
                  build_profile: str = "installedWorker") -> list[str]:
    return ["--build-manifest", str(build_manifest), "--render-core-profile", str(core_profile),
            "--build-profile", build_profile]


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
            build_manifest, core_profile = write_build_evidence(
                root, firmware, revision=revision, image_version=version,
                core_version="0.6.2", core_abi=1)
            completed = subprocess.run([
                sys.executable, str(PACKAGER),
                "--firmware", str(firmware),
                "--factory-image", str(factory),
                *identity_args(build_manifest, core_profile),
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
            self.assertIn("provider/lib/device_install_package.py", release_files)
            self.assertIn("provider/lib/device_render_core_capabilities.py", release_files)
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
            self.assertNotIn("requiresSignedBundle", manifest["storage"])

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
            revision = "b" * 40
            version = "0.6.2-ws147.abi4-test"
            build_manifest, core_profile = write_build_evidence(
                root, firmware, revision=revision, image_version=version,
                core_version="0.9.0-dev", core_abi=4)
            completed = subprocess.run([
                sys.executable, str(PACKAGER),
                "--firmware", str(firmware),
                "--factory-image", str(factory),
                *identity_args(build_manifest, core_profile),
                "--requires-signed-bundle",
                "--output", str(root / "release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest = device_image_manifest.parse_device_image_manifest(
                (release / "developer-image" / "ws147-developer-image.manifest.json").read_bytes())
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": 4})
            self.assertIs(manifest["storage"]["requiresSignedBundle"], True)
            self.assertEqual(manifest["imageVersion"], version)
            self.assertEqual(manifest["source"]["revision"], revision)
            self.assertEqual(manifest["source"]["firmwareSha256"], sha256(firmware))
            self.assertEqual(manifest["profile"]["featureFamilies"], json.loads(
                core_profile.read_text(encoding="utf-8"))["features"])
            self.assertEqual(manifest["buildProvenance"], {
                "renderCoreSourceIdentity": "a" * 64,
                "renderCoreSourceInputCount": 163,
                "idfVersion": "5.3.1",
                "toolchain": "GNU 13.2.0 (esp-13.2.0_20240530)",
            })

    def test_build_profile_selects_the_matching_manifest_identity(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-build-profile-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            firmware.write_bytes(b"ABI4 fixture firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            native_bin = root / "build-evidence" / "native.bin"
            native_bin.parent.mkdir()
            native_bin.write_bytes(b"native profile firmware\n")
            native_features = ["band-shell-ui", "core-text"]
            build_manifest, core_profile = write_build_evidence(
                root, firmware, revision="c" * 40, image_version="0.6.2-ws147.worker",
                core_version="0.9.0-dev", core_abi=4,
                profiles={"native": {
                    "imageVersion": "0.6.2-ws147.native",
                    "features": native_features,
                    "bin": {"path": "native.bin", "sha256": sha256(native_bin)},
                }})
            selected = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(native_bin),
                "--factory-image", str(factory), *identity_args(build_manifest, core_profile, "native"),
                "--output", str(root / "native-release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(selected.returncode, 0, selected.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(selected.stdout)["release"])
            manifest = device_image_manifest.parse_device_image_manifest(
                (release / "developer-image" / "ws147-developer-image.manifest.json").read_bytes())
            self.assertEqual(manifest["imageVersion"], "0.6.2-ws147.native")
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": 4})
            self.assertEqual(manifest["source"]["firmwareSha256"], sha256(native_bin))

            unsupported_profile = root / "unsupported-core-profile.json"
            unsupported_profile.write_text(json.dumps({
                "schemaVersion": 1, "packageVersion": "0.9.0-dev", "engineAbi": 4,
                "features": ["core.document", "core.paint", "css.unencodable"],
            }), encoding="utf-8")
            rejected = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(native_bin),
                "--factory-image", str(factory), *identity_args(build_manifest, unsupported_profile, "native"),
                "--output", str(root / "unsupported"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b"cannot encode", rejected.stderr)

    def test_incomplete_build_manifest_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-incomplete-evidence-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory-16mb.bin"
            build_manifest = root / "build-manifest.json"
            core_profile = root / "jellyframe_render_core_profile.json"
            firmware.write_bytes(b"firmware\n")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            build_manifest.write_text(json.dumps({
                "sources": {"renderCore": {"packageVersion": "0.9.0-dev", "engineAbi": 4}}
            }), encoding="utf-8")
            core_profile.write_text(json.dumps({
                "schemaVersion": 1, "packageVersion": "0.9.0-dev", "engineAbi": 4,
                "features": ["core.document", "core.paint"],
            }), encoding="utf-8")
            completed = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), *identity_args(build_manifest, core_profile),
                "--output", str(root / "release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn(b"missing required identity fields", completed.stderr)

    def test_invalid_output_provenance_is_rejected_before_writing(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jellyframe-ws147-invalid-provenance-") as directory:
            root = Path(directory)
            firmware = root / "firmware.bin"
            factory = root / "factory.bin"
            firmware.write_bytes(b"firmware")
            with factory.open("wb") as stream:
                stream.truncate(16 * 1024 * 1024)
            build_manifest, profile = write_build_evidence(
                root, firmware, revision="a" * 40, image_version="0.6.2-test",
                core_version="0.9.0", core_abi=4)
            document = json.loads(build_manifest.read_text(encoding="utf-8"))
            document["toolchain"]["xtensaCompiler"] = "x" * 257
            build_manifest.write_text(json.dumps(document), encoding="utf-8")
            output = root / "output"
            completed = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), *identity_args(build_manifest, profile),
                "--output", str(output),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn(b"invalid Developer Image manifest", completed.stderr)
            self.assertFalse(output.exists())

    def test_build_dir_imports_and_cross_checks_idf_provenance(self) -> None:
        self._exercise_build_dir(4)

    def test_contextual_build_dir_release_and_isolated_import(self) -> None:
        for abi in (9, 10):
            with self.subTest(abi=abi):
                self._exercise_build_dir(abi, custom_sdkconfig=True)

    def test_package_trust_receipt_is_archived_and_must_match_generated_table(self) -> None:
        self._exercise_build_dir(11, custom_sdkconfig=True, trust_receipt=True)

    def _exercise_build_dir(self, abi: int, *, custom_sdkconfig: bool = False,
                            trust_receipt: bool = False) -> None:
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
                f"package=0.9.0-dev\nabi={abi}\nprofile=render-core-default\nsource={identity}\nfiles=163\n",
                encoding="utf-8")
            (generated / "jellyframe_render_core_source_manifest.json").write_text(json.dumps({
                "sourceHash": identity, "sourceFileCount": 163, "files": [],
            }), encoding="utf-8")
            core_features = [
                "core.document", "core.paint", "forms.advanced", "css.flex-grid",
                "css.modern-paint", "graphics.canvas2d", "text.shaping",
            ]
            if abi >= 9:
                core_features.remove('text.shaping')
                core_features += ['text.contextual-jfsp2','text.contextual-layout',
                                  'css.length-math','css.counters','css.borders','css.tables','css.positioned-sizing']
            generated_profile = generated / "jellyframe_render_core_profile.json"
            generated_profile.write_text(json.dumps({
                "schemaVersion": 1, "packageVersion": "0.9.0-dev", "engineAbi": abi,
                "profileId": "render-core-default", "features": core_features,
            }), encoding="utf-8")
            description_path = build / "project_description.json"
            description = {
                "build_dir": str(build), "git_revision": "a" * 40, "target": "esp32s3",
                "idf_path": str(root / "idf"), "app_bin": "jellyframe_esp32s3_bench.bin",
                "c_compiler": r"C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\bin\xtensa-esp32s3-elf-gcc.exe",
            }
            sdkconfig = build / ("sdkconfig.board" if custom_sdkconfig else "sdkconfig")
            if custom_sdkconfig:
                description["config_file"] = str(sdkconfig)
                (build / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32s3"\n'
                                                 'CONFIG_PARTITION_TABLE_OFFSET=0x8000\n'
                                                 'CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=320\n',
                                                 encoding="utf-8")
            description_path.write_text(json.dumps(description), encoding="utf-8")
            idf_version_file = root / "idf" / "tools" / "cmake" / "version.cmake"
            idf_version_file.parent.mkdir(parents=True)
            idf_version_file.write_text(
                "set(IDF_VERSION_MAJOR 5)\nset(IDF_VERSION_MINOR 3)\nset(IDF_VERSION_PATCH 1)\n",
                encoding="utf-8")
            app_bin = build / "jellyframe_esp32s3_bench.bin"
            app_bin.write_bytes(firmware.read_bytes())
            (build / "CMakeCache.txt").write_text(
                f"IDF_TARGET:STRING=esp32s3\nSDKCONFIG_DEFAULTS:UNINITIALIZED={root / 'sdkconfig.defaults'};{root / 'sdkconfig.ws147_core_text.defaults'}\n"
                + (f"SDKCONFIG:UNINITIALIZED={sdkconfig}\n" if custom_sdkconfig else ""),
                encoding="utf-8")
            sdkconfig.write_text('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n'
                                 'CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=1024\n', encoding="utf-8")
            config_header = build / "config/sdkconfig.h"
            config_header.parent.mkdir()
            config_header.write_text('#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n'
                                     '#define CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB 1024\n')
            table = (struct.pack("<HBBII16sI", 0x50aa, 0, 0, 0x10000, 8 * 1024 * 1024, b"factory", 0) +
                     struct.pack("<HBBII16sI", 0x50aa, 1, 2, 0xc10000, 3 * 1024 * 1024, b"storage", 0))
            table += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(table).digest() + b"\xff" * 32
            partition = build / "partition_table" / "partition-table.bin"
            partition.parent.mkdir()
            partition.write_bytes(table)
            with factory.open("r+b") as image:
                image.seek(0x8000)
                image.write(table)
                image.seek(0x10000)
                image.write(firmware.read_bytes())
            build_manifest, _ = write_build_evidence(
                root, firmware, revision="a" * 40, image_version="0.6.2-ws147.build-dir-test",
                core_version="0.9.0-dev", core_abi=abi, families=core_features)
            common = [
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), *identity_args(build_manifest, generated_profile),
                "--build-dir", str(build),
            ]
            trust = build / "esp-idf/main/jfsg-package-trust"
            if trust_receipt:
                port = PACKAGER.parents[2]
                subprocess.run([
                    sys.executable, str(port / "tools/generate_package_trust.py"),
                    "--manifest", str(port / "tests/package_signature/configured_public_trust.json"),
                    "--output-dir", str(trust),
                ], cwd=ROOT, check=True)
                generated_receipt = json.loads((trust / "jfsg_package_trust_receipt.json").read_text(encoding="utf-8"))
            completed = subprocess.run([*common, "--output", str(root / "release")], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", errors="replace"))
            release = Path(json.loads(completed.stdout)["release"])
            manifest_path = release / "developer-image" / "ws147-developer-image.manifest.json"
            manifest = device_image_manifest.parse_device_image_manifest(manifest_path.read_bytes())
            self.assertEqual(manifest["renderCore"], {"version": "0.9.0-dev", "abi": abi})
            self.assertEqual(manifest['profile']['featureFamilies'],core_features)
            self.assertEqual(manifest["buildProvenance"], {
                "renderCoreSourceIdentity": identity,
                "renderCoreSourceInputCount": 163,
                "idfVersion": "5.3.1",
                "idfRevision": "a" * 40,
                "toolchain": "esp-13.2.0_20240530",
                "target": "esp32s3",
                "buildProfile": "ws147_core_text;render-core-default",
                "sdkconfigSha256": sha256(sdkconfig),
                "partitionTableSha256": sha256(partition),
                "configuredMaxBundleBytes": 1048576,
                "storagePartitionOffset": 0xc10000, "storagePartitionBytes": 3145728,
            })
            self.assertEqual(manifest["storage"]["maxBundleBytes"], 1044480)
            receipt_path = release / "developer-image/jfsg-package-trust.receipt.json"
            if trust_receipt:
                self.assertEqual(json.loads(receipt_path.read_text(encoding="utf-8")), {
                    "imageVersion": manifest["imageVersion"],
                    "firmwareSha256": sha256(firmware),
                    "trustBuild": generated_receipt,
                })
                checksums = (release / "SHA256SUMS.txt").read_text(encoding="ascii")
                self.assertIn(f"{sha256(receipt_path)}  developer-image/{receipt_path.name}\n", checksums)
                header = trust / "jfsg_trusted_public_keys.inc"
                original = header.read_bytes()
                header.write_bytes(original + b"\n")
                tampered = subprocess.run([*common, "--output", str(root / "bad-trust")],
                                          cwd=ROOT, capture_output=True, check=False)
                self.assertNotEqual(tampered.returncode, 0)
                self.assertIn(b"package trust receipt and generated table hash differ", tampered.stderr)
                self.assertFalse((root / "bad-trust").exists())
                header.write_bytes(original)
            else:
                self.assertFalse(receipt_path.exists())
            if custom_sdkconfig:
                description["config_file"] = str(build / "sdkconfig")
                description_path.write_text(json.dumps(description), encoding="utf-8")
                conflict_output = root / "config-conflict"
                conflict = subprocess.run([*common, "--output", str(conflict_output)], cwd=ROOT,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                self.assertNotEqual(conflict.returncode, 0)
                self.assertIn(b"disagree on sdkconfig", conflict.stderr)
                self.assertFalse(conflict_output.exists())
                description["config_file"] = str(sdkconfig)
                description_path.write_text(json.dumps(description), encoding="utf-8")
            bundled_parser = release / "provider" / "lib" / "device_image_manifest.py"
            self.assertTrue(bundled_parser.is_file())

            # A shaping profile cannot silently take the legacy 320 KiB path.
            missing_capacity = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(firmware),
                "--factory-image", str(factory), *identity_args(build_manifest, generated_profile),
                "--output", str(root / "missing-capacity"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(missing_capacity.returncode, 0)
            self.assertIn(b"requires --build-dir capacity evidence", missing_capacity.stderr)
            self.assertFalse((root / "missing-capacity").exists())

            # The release's inspection module imports using stdlib only, from
            # an isolated interpreter with no repository on sys.path.
            isolated = subprocess.run([
                sys.executable, "-I", "-c",
                "import sys;sys.path.insert(0,sys.argv[1]);import device_install_package;"
                "assert device_install_package.MAX_PACKAGE_BYTES == 4194304",
                str(release / "provider" / "lib"),
            ], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(isolated.returncode, 0, isolated.stderr.decode())

            mismatched_firmware = root / "mismatched-firmware.bin"
            mismatched_firmware.write_bytes(b"different firmware\n")
            rejected_firmware = subprocess.run([
                sys.executable, str(PACKAGER), "--firmware", str(mismatched_firmware),
                "--factory-image", str(factory), *identity_args(build_manifest, generated_profile),
                "--build-dir", str(build),
                "--output", str(root / "mismatched-firmware-release"),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(rejected_firmware.returncode, 0)
            self.assertIn(b"does not match the installedWorker binary", rejected_firmware.stderr)

            evidence = json.loads(build_manifest.read_text(encoding="utf-8"))
            for section, key, value, diagnostic in (
                ("espIdf", "version", "v5.4.0", b"IDF version conflicts"),
                ("toolchain", "xtensaCompiler", "esp-14.2.0_20260101", b"versioned toolchain conflicts"),
            ):
                with self.subTest(section=section):
                    original = evidence[section][key]
                    evidence[section][key] = value
                    build_manifest.write_text(json.dumps(evidence), encoding="utf-8")
                    conflict = subprocess.run([*common, "--output", str(root / "provenance-conflict")], cwd=ROOT,
                                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    self.assertNotEqual(conflict.returncode, 0)
                    self.assertIn(diagnostic, conflict.stderr)
                    self.assertFalse((root / "provenance-conflict").exists())
                    evidence[section][key] = original
            evidence["renderCore"]["sourceHashSha256"] = "c" * 64
            build_manifest.write_text(json.dumps(evidence), encoding="utf-8")
            conflict = subprocess.run([*common, "--output", str(root / "source-conflict")], cwd=ROOT,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertNotEqual(conflict.returncode, 0)
            self.assertIn(b"Core source identity conflicts", conflict.stderr)
            self.assertFalse((root / "source-conflict").exists())
            evidence["renderCore"]["sourceHashSha256"] = identity
            build_manifest.write_text(json.dumps(evidence), encoding="utf-8")

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
