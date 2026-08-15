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

    def test_reference_endpoint_preserves_staging_and_lifecycle_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-") as directory:
            root = Path(directory)
            store = root / "store"
            first_bundle = root / "first.jfapp"
            second_bundle = root / "second.jfapp"
            cancelled_bundle = root / "cancelled.jfapp"
            write_jfapp(first_bundle, app_id="org.example.lifecycle", version_code=1)
            write_jfapp(second_bundle, app_id="org.example.lifecycle", version_code=2, version_name="2.0.0")
            write_jfapp(cancelled_bundle, app_id="org.example.cancelled", version_code=1)

            discovery = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "discover", "--json",
            )
            self.assertEqual(discovery.returncode, 0, discovery.stderr)
            self.assertFalse(json.loads(discovery.stdout)["deviceAvailable"])

            paused = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "install",
                "--bundle", str(first_bundle), "--chunk-bytes", "64", "--pause-after-chunks", "1", "--json",
            )
            self.assertEqual(paused.returncode, 0, paused.stderr)
            paused_result = json.loads(paused.stdout)
            self.assertFalse(paused_result["complete"])
            transaction_id = paused_result["transactionId"]

            no_partial = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "list", "--json",
            )
            self.assertEqual(no_partial.returncode, 0, no_partial.stderr)
            self.assertEqual(len(json.loads(no_partial.stdout)["apps"]), 0)

            incomplete_commit = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "commit",
                "--transaction-id", str(transaction_id), "--json",
            )
            self.assertEqual(incomplete_commit.returncode, 1)
            self.assertEqual(json.loads(incomplete_commit.stdout)["resultCode"], "invalid-request")

            recovery = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "recovery", "--json",
            )
            self.assertEqual(recovery.returncode, 0, recovery.stderr)
            recovery_result = json.loads(recovery.stdout)
            self.assertEqual(recovery_result["pendingTransactionCount"], 1)
            self.assertEqual(recovery_result["lastFailure"]["reason"], "invalid-request")

            resumed = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "resume",
                "--transaction-id", str(transaction_id), "--bundle", str(first_bundle), "--chunk-bytes", "64", "--json",
            )
            self.assertEqual(resumed.returncode, 0, resumed.stderr)
            self.assertEqual(json.loads(resumed.stdout)["versionCode"], 1)

            launch = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "launch",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(launch.returncode, 0, launch.stderr)
            self.assertTrue(json.loads(launch.stdout)["active"])
            stop = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "stop", "--json",
            )
            self.assertEqual(stop.returncode, 0, stop.stderr)
            self.assertFalse(json.loads(stop.stdout)["active"])

            update = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "install",
                "--bundle", str(second_bundle), "--chunk-bytes", "64", "--json",
            )
            self.assertEqual(update.returncode, 0, update.stderr)
            self.assertEqual(json.loads(update.stdout)["versionCode"], 2)
            rollback = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "rollback",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(rollback.returncode, 0, rollback.stderr)
            self.assertEqual(json.loads(rollback.stdout)["versionCode"], 1)

            cancelled = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "install",
                "--bundle", str(cancelled_bundle), "--chunk-bytes", "64", "--pause-after-chunks", "1", "--json",
            )
            self.assertEqual(cancelled.returncode, 0, cancelled.stderr)
            cancelled_id = json.loads(cancelled.stdout)["transactionId"]
            cancel = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "cancel",
                "--transaction-id", str(cancelled_id), "--json",
            )
            self.assertEqual(cancel.returncode, 0, cancel.stderr)
            self.assertEqual(json.loads(cancel.stdout)["resultCode"], "cancelled")
            self.assertIsNone(app_registry.existing_app_entry(store, "org.example.cancelled"))

            logs = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "logs",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(logs.returncode, 0, logs.stderr)
            events = [entry["event"] for entry in json.loads(logs.stdout)["logs"]]
            self.assertIn("install-commit", events)
            self.assertIn("launch", events)
            self.assertIn("rollback", events)

            remove = self.run_cli(
                "device", "--transport", "reference", "--store", str(store), "remove",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(remove.returncode, 0, remove.stderr)
            self.assertIsNone(app_registry.existing_app_entry(store, "org.example.lifecycle"))


if __name__ == "__main__":
    unittest.main()
