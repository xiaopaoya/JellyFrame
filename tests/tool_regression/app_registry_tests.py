import contextlib
import io
import json
import sys
import tempfile
import unittest
import struct
import zlib
from types import SimpleNamespace
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import app_registry  # noqa: E402
import jellyframe_cli  # noqa: E402


def write_jfapp(
    path: Path,
    app_id: str = "org.example.weather",
    version_code: int = 1,
    version_name: str = "1.0.0",
    entry: str = "/index.html",
    script: str = "classic",
) -> None:
    summary = json.dumps({
        "id": app_id,
        "name": "Weather",
        "versionName": version_name,
        "versionCode": version_code,
        "entry": entry,
        "script": script,
    }, separators=(",", ":")).encode("utf-8")
    summary_offset = app_registry.JFAPP_HEADER_SIZE
    summary_size = len(summary)
    index_offset = summary_offset + summary_size
    strings_offset = index_offset
    payload_offset = strings_offset
    header = struct.pack(
        app_registry.JFAPP_HEADER_FORMAT,
        app_registry.JFAPP_MAGIC,
        app_registry.JFAPP_HEADER_SIZE,
        0,
        0,
        summary_offset,
        summary_size,
        index_offset,
        0,
        strings_offset,
        0,
        payload_offset,
        0,
        0,
        0,
    )
    bundle = bytearray(header + summary)
    crc = zlib.crc32(bundle) & 0xffffffff
    struct.pack_into("<I", bundle, 48, crc)
    path.write_bytes(bundle)


def write_registry(store: Path, app_id: str = "org.example.weather") -> None:
    app_registry.atomic_write_json(
        app_registry.registry_path(store),
        {
            "format": app_registry.REGISTRY_FORMAT,
            "formatVersion": app_registry.REGISTRY_VERSION,
            "apps": [
                {
                    "id": app_id,
                    "name": "Weather",
                    "status": app_registry.APP_STATUS_INSTALLED,
                    "enabled": True,
                    "versionName": "1.0.0",
                    "versionCode": 1,
                    "bundleFile": "weather.jfapp",
                    "bundleSize": 12,
                    "resourceCount": 1,
                }
            ],
        },
    )
    bundle = app_registry.bundles_dir(store) / "weather.jfapp"
    bundle.parent.mkdir(parents=True, exist_ok=True)
    bundle.write_bytes(b"bundle")


class AppRegistryTests(unittest.TestCase):
    def test_remove_deletes_app_private_data_by_default(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            write_registry(store)
            data_path = app_registry.app_data_dir(store, "org.example.weather")
            data_path.mkdir(parents=True)
            (data_path / "state.json").write_text("{}", encoding="utf-8")

            removed = app_registry.remove_app(store, "org.example.weather")

            self.assertTrue(removed["dataDeleted"])
            self.assertFalse(removed["dataRetained"])
            self.assertFalse(data_path.exists())
            self.assertEqual(app_registry.load_registry(store)["apps"], [])

    def test_remove_can_keep_app_private_data_explicitly(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            write_registry(store)
            data_path = app_registry.app_data_dir(store, "org.example.weather")
            data_path.mkdir(parents=True)
            (data_path / "state.json").write_text("{}", encoding="utf-8")

            removed = app_registry.remove_app(store, "org.example.weather", delete_data=False)

            self.assertFalse(removed["dataDeleted"])
            self.assertTrue(removed["dataRetained"])
            self.assertTrue(data_path.exists())

    def test_delete_data_keeps_installed_app_entry(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            write_registry(store)
            data_path = app_registry.app_data_dir(store, "org.example.weather")
            data_path.mkdir(parents=True)
            (data_path / "state.json").write_text("{}", encoding="utf-8")

            self.assertTrue(app_registry.delete_app_data(store, "org.example.weather"))

            registry = app_registry.load_registry(store)
            self.assertEqual(len(registry["apps"]), 1)
            self.assertFalse(data_path.exists())

    def test_cli_delete_data_reports_json(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            write_registry(store)
            data_path = app_registry.app_data_dir(store, "org.example.weather")
            data_path.mkdir(parents=True)

            with self.subTest("delete-data"):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    result = app_registry.main([
                        "delete-data",
                        "--store",
                        str(store),
                        "--id",
                        "org.example.weather",
                        "--json",
                    ])

            self.assertEqual(result, 0)
            self.assertFalse(data_path.exists())

    def test_install_update_keeps_previous_bundle_as_rollback(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            first = store / "weather-v1.jfapp"
            second = store / "weather-v2.jfapp"
            write_jfapp(first, version_code=1, version_name="1.0.0")
            write_jfapp(second, version_code=2, version_name="2.0.0")

            installed = app_registry.install_bundle(store, first, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            updated = app_registry.install_bundle(store, second, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            self.assertEqual(installed["versionCode"], 1)
            self.assertEqual(updated["versionCode"], 2)
            self.assertEqual(updated["status"], app_registry.APP_STATUS_INSTALLED)
            self.assertTrue(updated["enabled"])
            self.assertIn("rollback", updated)
            self.assertEqual(updated["rollback"]["versionCode"], 1)
            self.assertTrue((app_registry.bundles_dir(store) / updated["bundleFile"]).is_file())
            self.assertTrue((app_registry.bundles_dir(store) / updated["rollback"]["bundleFile"]).is_file())

    def test_install_report_marks_update_and_rollback_availability(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            first = store / "weather-v1.jfapp"
            second = store / "weather-v2.jfapp"
            report = store / "install.report.json"
            write_jfapp(first, version_code=1, version_name="1.0.0")
            write_jfapp(second, version_code=2, version_name="2.0.0")
            app_registry.install_bundle(store, first, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            with contextlib.redirect_stdout(io.StringIO()):
                result = app_registry.main([
                    "install",
                    "--store",
                    str(store),
                    "--bundle",
                    str(second),
                    "--report",
                    str(report),
                ])

            self.assertEqual(result, 0)
            data = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(data["format"], "jellyframe.install.transaction")
            self.assertEqual(data["action"], "update")
            self.assertEqual(data["previous"]["versionCode"], 1)
            self.assertEqual(data["app"]["versionCode"], 2)
            self.assertTrue(data["rollback"]["available"])
            self.assertEqual(data["rollback"]["versionCode"], 1)
            self.assertFalse(data["dataPolicy"]["appPrivateDataTouched"])

    def test_jellyframe_cli_bundle_install_writes_transaction_report(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory) / "store"
            bundle = Path(directory) / "weather-v1.jfapp"
            report = Path(directory) / "cli-install.report.json"
            write_jfapp(bundle, version_code=1, version_name="1.0.0")

            with contextlib.redirect_stdout(io.StringIO()):
                result = jellyframe_cli.cmd_install(SimpleNamespace(
                    root=None,
                    bundle=bundle,
                    store=store,
                    report=report,
                    allow_downgrade=False,
                ))

            self.assertEqual(result, 0)
            data = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(data["format"], "jellyframe.install.transaction")
            self.assertEqual(data["source"]["kind"], "bundle")
            self.assertEqual(data["action"], "install")
            self.assertEqual(data["app"]["id"], "org.example.weather")

    def test_downgrade_is_blocked_unless_explicitly_allowed(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            first = store / "weather-v1.jfapp"
            second = store / "weather-v2.jfapp"
            report = store / "downgrade.report.json"
            write_jfapp(first, version_code=1, version_name="1.0.0")
            write_jfapp(second, version_code=2, version_name="2.0.0")
            app_registry.install_bundle(store, second, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            with self.assertRaises(SystemExit):
                app_registry.main([
                    "install",
                    "--store",
                    str(store),
                    "--bundle",
                    str(first),
                    "--report",
                    str(report),
                ])

            data = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(data["result"], "failed")
            self.assertEqual(data["failure"]["reason"], "downgrade-blocked")
            self.assertEqual(app_registry.find_app(store, "org.example.weather")["versionCode"], 2)

            with contextlib.redirect_stdout(io.StringIO()):
                result = app_registry.main([
                    "install",
                    "--store",
                    str(store),
                    "--bundle",
                    str(first),
                    "--allow-downgrade",
                ])

            self.assertEqual(result, 0)
            self.assertEqual(app_registry.find_app(store, "org.example.weather")["versionCode"], 1)

    def test_disable_and_enable_app_updates_launch_state(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            bundle = store / "weather-v1.jfapp"
            write_jfapp(bundle, version_code=1, version_name="1.0.0")
            app_registry.install_bundle(store, bundle, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            disabled = app_registry.set_app_enabled(store, "org.example.weather", False)
            self.assertFalse(disabled["enabled"])
            self.assertEqual(disabled["status"], app_registry.APP_STATUS_DISABLED)

            updated_bundle = store / "weather-v2.jfapp"
            write_jfapp(updated_bundle, version_code=2, version_name="2.0.0")
            updated = app_registry.install_bundle(
                store,
                updated_bundle,
                app_registry.DEFAULT_MAX_APPS,
                app_registry.DEFAULT_MAX_BUNDLE_BYTES,
            )
            self.assertFalse(updated["enabled"])
            self.assertEqual(updated["status"], app_registry.APP_STATUS_DISABLED)

            enabled = app_registry.set_app_enabled(store, "org.example.weather", True)
            self.assertTrue(enabled["enabled"])
            self.assertEqual(enabled["status"], app_registry.APP_STATUS_INSTALLED)

    def test_rollback_swaps_current_and_previous_bundle(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            first = store / "weather-v1.jfapp"
            second = store / "weather-v2.jfapp"
            write_jfapp(first, version_code=1, version_name="1.0.0", entry="/v1.html", script="none")
            write_jfapp(second, version_code=2, version_name="2.0.0", entry="/v2.html", script="classic")
            app_registry.install_bundle(store, first, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            app_registry.install_bundle(store, second, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            rolled_back = app_registry.rollback_app(store, "org.example.weather")

            self.assertEqual(rolled_back["versionCode"], 1)
            self.assertEqual(rolled_back["entry"], "/v1.html")
            self.assertEqual(rolled_back["script"], "none")
            self.assertEqual(rolled_back["rollback"]["versionCode"], 2)
            self.assertEqual(rolled_back["rollback"]["entry"], "/v2.html")
            self.assertEqual(rolled_back["rollback"]["script"], "classic")
            self.assertEqual(app_registry.find_app(store, "org.example.weather")["versionCode"], 1)

    def test_remove_deletes_current_and_rollback_bundles(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            first = store / "weather-v1.jfapp"
            second = store / "weather-v2.jfapp"
            write_jfapp(first, version_code=1, version_name="1.0.0")
            write_jfapp(second, version_code=2, version_name="2.0.0")
            app_registry.install_bundle(store, first, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            updated = app_registry.install_bundle(store, second, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            current_path = app_registry.bundles_dir(store) / updated["bundleFile"]
            rollback_path = app_registry.bundles_dir(store) / updated["rollback"]["bundleFile"]

            app_registry.remove_app(store, "org.example.weather")

            self.assertFalse(current_path.exists())
            self.assertFalse(rollback_path.exists())


if __name__ == "__main__":
    unittest.main()
