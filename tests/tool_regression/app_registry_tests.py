import contextlib
import hashlib
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
    min_jellyframe: str = "0.6.0",
    capabilities: list[str] | None = None,
    network_allowed: bool = False,
    summary_text: str | None = None,
) -> None:
    capabilities = [] if capabilities is None else capabilities
    summary = (summary_text.encode("utf-8") if summary_text is not None else json.dumps({
        "id": app_id,
        "name": "Weather",
        "role": "app",
        "versionName": version_name,
        "versionCode": version_code,
        "entry": entry,
        "minJellyFrame": min_jellyframe,
        "script": script,
        "viewport": {"designWidth": 172, "designHeight": 320},
        "budgets": {"maxResourceBytes": 65536},
        "fonts": [],
        "targets": {"test": {"viewport": {"width": 172, "height": 320}, "output": "jfapp"}},
        "permissions": [],
        "capabilities": capabilities,
        "computeJobsAllowed": False,
        "videoFrameAllowed": False,
        "networkAllowed": network_allowed,
        "storageKvAllowed": False,
        "canvas2dAllowed": False,
        "audioPlaybackAllowed": False,
        "sensorAccelerometerAllowed": False,
        "sensorGyroscopeAllowed": False,
        "sensorHeartRateAllowed": False,
        "sensorAmbientLightAllowed": False,
        "locationPositionAllowed": False,
        "systemBatteryAllowed": False,
        "systemWeatherAllowed": False,
        "systemActivityAllowed": False,
        "backgroundServices": {},
    }, separators=(",", ":")).encode("utf-8"))
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


def write_jfapp_with_invalid_resource_entry(path: Path) -> None:
    summary = json.dumps({
        "id": "org.example.weather",
        "name": "Weather",
        "role": "app",
        "versionName": "1.0.0",
        "versionCode": 1,
        "entry": "/index.html",
        "minJellyFrame": "0.6.0",
        "script": "classic",
        "viewport": {"designWidth": 172, "designHeight": 320},
        "budgets": {"maxResourceBytes": 65536},
        "fonts": [],
        "targets": {"test": {"viewport": {"width": 172, "height": 320}, "output": "jfapp"}},
        "permissions": [],
        "capabilities": [],
        "computeJobsAllowed": False,
        "videoFrameAllowed": False,
        "networkAllowed": False,
        "storageKvAllowed": False,
        "canvas2dAllowed": False,
        "audioPlaybackAllowed": False,
        "sensorAccelerometerAllowed": False,
        "sensorGyroscopeAllowed": False,
        "sensorHeartRateAllowed": False,
        "sensorAmbientLightAllowed": False,
        "locationPositionAllowed": False,
        "systemBatteryAllowed": False,
        "systemWeatherAllowed": False,
        "systemActivityAllowed": False,
        "backgroundServices": {},
    }, separators=(",", ":")).encode("utf-8")
    summary_offset = app_registry.JFAPP_HEADER_SIZE
    index_offset = summary_offset + len(summary)
    strings_offset = index_offset + app_registry.JFAPP_ENTRY_SIZE
    header = struct.pack(
        app_registry.JFAPP_HEADER_FORMAT,
        app_registry.JFAPP_MAGIC,
        app_registry.JFAPP_HEADER_SIZE,
        0,
        0,
        summary_offset,
        len(summary),
        index_offset,
        1,
        strings_offset,
        0,
        strings_offset,
        0,
        0,
        0,
    )
    entry = struct.pack(app_registry.JFAPP_ENTRY_FORMAT, 0, 0, 1, 0, 0, 0, 0, 0)
    bundle = bytearray(header + summary + entry)
    struct.pack_into("<I", bundle, 48, zlib.crc32(bundle) & 0xffffffff)
    path.write_bytes(bundle)


def write_install_candidate(
    path: Path,
    bundle: Path,
    signature_status: str = "trusted",
    user_approval: bool = True,
    sha256: str | None = None,
) -> None:
    digest = sha256 if sha256 is not None else hashlib.sha256(bundle.read_bytes()).hexdigest()
    data = {
        "format": app_registry.INSTALL_CANDIDATE_FORMAT,
        "formatVersion": app_registry.INSTALL_CANDIDATE_VERSION,
        "bundle": {
            "path": bundle.name,
            "sha256": digest,
            "size": bundle.stat().st_size,
        },
        "signature": {
            "status": signature_status,
            "scheme": "host-test",
            "publisher": "JellyFrame Tests",
        },
        "userApproval": user_approval,
        "download": {
            "source": "https://example.invalid/app.jfapp",
            "transport": "host-owned",
        },
    }
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


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
    def test_install_rejects_historical_pre_release_runtime_line_before_registry_mutation(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-historical-runtime-") as directory:
            root = Path(directory)
            bundle = root / "historical-runtime.jfapp"
            write_jfapp(bundle, min_jellyframe="0.5.0")

            with self.assertRaisesRegex(
                SystemExit,
                "must target the active pre-1.0 runtime line 0.6.0",
            ):
                app_registry.install_bundle(
                    root / "store",
                    bundle,
                    app_registry.DEFAULT_MAX_APPS,
                    app_registry.DEFAULT_MAX_BUNDLE_BYTES,
                )
            self.assertFalse((root / "store" / "registry.json").exists())

    def test_install_rejects_duplicate_summary_member_before_registry_mutation(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-duplicate-summary-") as directory:
            root = Path(directory)
            bundle = root / "duplicate.jfapp"
            write_jfapp(bundle, summary_text='{"id":"org.example.duplicate","capabilities":["network.fetch"],"capabilities":[]}')

            with self.assertRaisesRegex(SystemExit, "duplicate JSON object member: capabilities"):
                app_registry.install_bundle(root / "store",
                                            bundle,
                                            app_registry.DEFAULT_MAX_APPS,
                                            app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            self.assertFalse((root / "store" / "registry.json").exists())

    def test_install_rejects_capability_projection_drift_before_registry_mutation(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-summary-drift-") as directory:
            root = Path(directory)
            bundle = root / "drift.jfapp"
            write_jfapp(bundle, capabilities=["network.fetch"], network_allowed=False)

            with self.assertRaisesRegex(SystemExit, "networkAllowed does not match permissions/capabilities"):
                app_registry.install_bundle(root / "store",
                                            bundle,
                                            app_registry.DEFAULT_MAX_APPS,
                                            app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            self.assertFalse((root / "store" / "registry.json").exists())

    def test_install_rejects_invalid_resource_index_before_registry_mutation(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory) / "store"
            bundle = Path(directory) / "invalid-resource-index.jfapp"
            write_jfapp_with_invalid_resource_entry(bundle)

            with self.assertRaisesRegex(SystemExit, "resource index entry 0 path"):
                app_registry.install_bundle(
                    store,
                    bundle,
                    app_registry.DEFAULT_MAX_APPS,
                    app_registry.DEFAULT_MAX_BUNDLE_BYTES,
                )

            self.assertFalse(app_registry.registry_path(store).exists())

    def test_registry_rejects_bundle_path_escape_before_mutating_store(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory) / "store"
            outside = Path(directory) / "outside.jfapp"
            outside.write_bytes(b"must-not-delete")
            write_registry(store)
            registry = app_registry.load_registry(store)
            registry["apps"][0]["bundleFile"] = "../outside.jfapp"
            app_registry.atomic_write_json(app_registry.registry_path(store), registry)

            with self.assertRaisesRegex(SystemExit, "bundle file"):
                app_registry.remove_app(store, "org.example.weather")

            self.assertEqual(outside.read_bytes(), b"must-not-delete")
            self.assertEqual(len(json.loads(app_registry.registry_path(store).read_text(encoding="utf-8"))["apps"]), 1)

    def test_registry_rejects_symlinked_bundle(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory) / "store"
            outside = Path(directory) / "outside.jfapp"
            outside.write_bytes(b"must-not-read")
            write_registry(store)
            bundle = app_registry.bundles_dir(store) / "weather.jfapp"
            bundle.unlink()
            try:
                bundle.symlink_to(outside)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {error}")

            with self.assertRaisesRegex(SystemExit, "bundle file (must not be a symlink|escapes the bundle store)"):
                app_registry.app_bundle_path(store, "org.example.weather")

    def test_install_candidate_requires_valid_hash_and_accepts_uppercase_hash(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-candidate-") as directory:
            root = Path(directory)
            store = root / "store"
            bundle = root / "weather.jfapp"
            candidate = root / "candidate.json"
            write_jfapp(bundle)

            write_install_candidate(candidate, bundle, sha256="")
            with self.assertRaisesRegex(SystemExit, "64-character hexadecimal"):
                app_registry.validate_install_candidate(store, candidate, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            write_install_candidate(candidate, bundle, sha256="not-a-hash")
            with self.assertRaisesRegex(SystemExit, "64-character hexadecimal"):
                app_registry.validate_install_candidate(store, candidate, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            write_install_candidate(candidate, bundle, sha256=hashlib.sha256(bundle.read_bytes()).hexdigest().upper())
            _path, _bytes, info, _previous, _candidate = app_registry.validate_install_candidate(
                store, candidate, app_registry.DEFAULT_MAX_BUNDLE_BYTES
            )
            self.assertEqual(info["sha256"], hashlib.sha256(bundle.read_bytes()).hexdigest())

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

    def test_install_candidate_requires_trusted_signature_and_approval(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-candidate-") as directory:
            root = Path(directory)
            store = root / "store"
            bundle = root / "weather-v1.jfapp"
            candidate = root / "candidate.json"
            report = root / "candidate.report.json"
            write_jfapp(bundle, version_code=1, version_name="1.0.0")
            write_install_candidate(candidate, bundle, signature_status="untrusted")

            with self.assertRaises(SystemExit):
                app_registry.main([
                    "install-candidate",
                    "--store",
                    str(store),
                    "--candidate",
                    str(candidate),
                    "--report",
                    str(report),
                ])

            failed = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(failed["result"], "failed")
            self.assertEqual(failed["failure"]["reason"], "signature-not-trusted")
            self.assertFalse(app_registry.registry_path(store).exists())

            with contextlib.redirect_stdout(io.StringIO()):
                result = app_registry.main([
                    "install-candidate",
                    "--store",
                    str(store),
                    "--candidate",
                    str(candidate),
                    "--allow-untrusted-signature",
                    "--report",
                    str(report),
                ])

            self.assertEqual(result, 0)
            installed = app_registry.find_app(store, "org.example.weather")
            self.assertEqual(installed["versionCode"], 1)
            ok = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(ok["source"]["kind"], "install-candidate")
            self.assertEqual(ok["candidate"]["signatureStatus"], "untrusted")

    def test_jellyframe_cli_candidate_install_writes_transaction_report(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-candidate-") as directory:
            root = Path(directory)
            store = root / "store"
            bundle = root / "weather-v1.jfapp"
            candidate = root / "candidate.json"
            report = root / "cli-candidate.report.json"
            write_jfapp(bundle, version_code=1, version_name="1.0.0")
            write_install_candidate(candidate, bundle)

            with contextlib.redirect_stdout(io.StringIO()):
                result = jellyframe_cli.cmd_install(SimpleNamespace(
                    root=None,
                    bundle=None,
                    candidate=candidate,
                    store=store,
                    report=report,
                    allow_downgrade=False,
                    allow_untrusted_signature=False,
                ))

            self.assertEqual(result, 0)
            data = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(data["format"], "jellyframe.install.transaction")
            self.assertEqual(data["source"]["kind"], "install-candidate")
            self.assertEqual(data["candidate"]["signatureStatus"], "trusted")
            self.assertEqual(app_registry.find_app(store, "org.example.weather")["versionCode"], 1)

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
            self.assertNotIn("failure", enabled)

    def test_mark_failed_records_reason_and_enable_clears_failure(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            bundle = store / "weather-v1.jfapp"
            write_jfapp(bundle, version_code=1, version_name="1.0.0")
            app_registry.install_bundle(store, bundle, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)

            failed = app_registry.mark_app_failed(store, "org.example.weather", "load-failed", "entry missing")
            self.assertFalse(failed["enabled"])
            self.assertEqual(failed["status"], app_registry.APP_STATUS_FAILED)
            self.assertEqual(failed["failure"]["reason"], "load-failed")
            self.assertEqual(failed["failure"]["message"], "entry missing")

            with contextlib.redirect_stdout(io.StringIO()):
                result = app_registry.main([
                    "enable",
                    "--store",
                    str(store),
                    "--id",
                    "org.example.weather",
                ])

            self.assertEqual(result, 0)
            enabled = app_registry.find_app(store, "org.example.weather")
            self.assertTrue(enabled["enabled"])
            self.assertEqual(enabled["status"], app_registry.APP_STATUS_INSTALLED)
            self.assertNotIn("failure", enabled)

    def test_state_report_exposes_launcher_friendly_derived_fields(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-registry-") as directory:
            store = Path(directory)
            weather_v1 = store / "weather-v1.jfapp"
            weather_v2 = store / "weather-v2.jfapp"
            bad = store / "bad-v1.jfapp"
            report = store / "state.report.json"
            write_jfapp(weather_v1, version_code=1, version_name="1.0.0")
            write_jfapp(weather_v2, version_code=2, version_name="2.0.0")
            write_jfapp(bad, app_id="org.example.bad", version_code=1, version_name="1.0.0")
            app_registry.install_bundle(store, weather_v1, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            app_registry.install_bundle(store, weather_v2, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            app_registry.install_bundle(store, bad, app_registry.DEFAULT_MAX_APPS, app_registry.DEFAULT_MAX_BUNDLE_BYTES)
            app_registry.mark_app_failed(store, "org.example.bad", "load-failed", "missing entry")

            with contextlib.redirect_stdout(io.StringIO()):
                result = app_registry.main([
                    "state",
                    "--store",
                    str(store),
                    "--output",
                    str(report),
                ])

            self.assertEqual(result, 0)
            state = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(state["format"], app_registry.APP_MANAGER_STATE_FORMAT)
            self.assertEqual(state["summary"]["appCount"], 2)
            self.assertEqual(state["summary"]["launchableCount"], 1)
            self.assertEqual(state["summary"]["failedCount"], 1)
            self.assertEqual(state["summary"]["rollbackReadyCount"], 1)
            apps = {entry["id"]: entry for entry in state["apps"]}
            self.assertTrue(apps["org.example.weather"]["launchable"])
            self.assertTrue(apps["org.example.weather"]["rollbackReady"])
            self.assertFalse(apps["org.example.bad"]["launchable"])
            self.assertEqual(apps["org.example.bad"]["failure"]["reason"], "load-failed")

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
