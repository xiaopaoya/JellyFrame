import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import app_registry  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
