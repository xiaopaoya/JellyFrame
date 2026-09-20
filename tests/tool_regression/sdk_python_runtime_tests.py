"""Offline checks for fail-closed embedded interpreter acquisition."""

import tempfile
import unittest
import sys
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "project_tools"))
from sdk_python_runtime import acquire, digest


class RuntimeAcquisitionTests(unittest.TestCase):
    def test_cached_hash_is_verified_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            asset = cache / "runtime.zip"
            asset.write_bytes(b"verified fixture")
            entry = {"name": asset.name, "sha256": digest(asset), "url": "https://invalid.example/runtime"}
            with patch("urllib.request.urlopen", side_effect=AssertionError("unexpected network")):
                self.assertEqual(acquire(entry, cache), asset)
                asset.write_bytes(b"corrupt")
                with self.assertRaisesRegex(ValueError, "SHA-256"):
                    acquire(entry, cache)


if __name__ == "__main__":
    unittest.main()
