#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
TESTS_DIR = REPO_ROOT / "tests" / "tool_regression"
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(TESTS_DIR))

import app_registry  # noqa: E402
from app_registry_tests import write_jfapp  # noqa: E402


CLI = REPO_ROOT / "tools" / "jellyframe_cli.py"


class DeviceReferenceCliTests(unittest.TestCase):
    def run_cli(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CLI), *arguments],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_requires_explicit_reference_transport(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-") as directory:
            result = self.run_cli("device", "--store", directory, "info")
            self.assertEqual(result.returncode, 2)
            self.assertIn("--transport reference", result.stderr)

    def test_reference_endpoint_runs_install_list_state_remove(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-") as directory:
            root = Path(directory)
            bundle = root / "sample.jfapp"
            store = root / "store"
            write_jfapp(bundle, app_id="org.example.reference", version_code=1)

            install = self.run_cli(
                "device", "--transport", "reference", "--store", str(store),
                "install", "--bundle", str(bundle), "--json",
            )
            self.assertEqual(install.returncode, 0, install.stderr)
            self.assertEqual(json.loads(install.stdout)["id"], "org.example.reference")

            listing = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "list", "--json",
            )
            self.assertEqual(listing.returncode, 0, listing.stderr)
            self.assertEqual(len(json.loads(listing.stdout)["apps"]), 1)

            state = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "state", "--json",
            )
            self.assertEqual(state.returncode, 0, state.stderr)
            self.assertEqual(json.loads(state.stdout)["summary"]["launchableCount"], 1)

            remove = self.run_cli(
                "device", "--transport", "reference", "--store", str(store),
                "remove", "--id", "org.example.reference", "--json",
            )
            self.assertEqual(remove.returncode, 0, remove.stderr)
            self.assertEqual(json.loads(remove.stdout)["id"], "org.example.reference")
            self.assertIsNone(app_registry.existing_app_entry(store, "org.example.reference"))


if __name__ == "__main__":
    unittest.main()
