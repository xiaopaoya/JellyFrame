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


if __name__ == "__main__":
    unittest.main()
