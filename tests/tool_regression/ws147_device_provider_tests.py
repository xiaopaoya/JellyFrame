#!/usr/bin/env python3
"""No-device regression coverage for the explicit WS147 Device OS provider."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe_device.py"
sys.path.insert(0, str(ROOT / "tools"))
from device_provider_contract import parse_provider_jsonl, parse_provider_result  # noqa: E402


def invoke(*arguments: str) -> subprocess.CompletedProcess[bytes]:
    environment = dict(__import__("os").environ)
    environment["JELLYFRAME_DEVICE_TEST_MODE"] = "1"
    return subprocess.run([sys.executable, str(PROVIDER), *arguments], cwd=ROOT, env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def cli(*arguments: str, fixture: str) -> subprocess.CompletedProcess[bytes]:
    environment = dict(__import__("os").environ)
    environment["JELLYFRAME_DEVICE_TEST_MODE"] = "1"
    environment["JELLYFRAME_DEVICE_TEST_FIXTURE"] = fixture
    return subprocess.run([sys.executable, str(ROOT / "tools" / "jellyframe_cli.py"), "device", *arguments],
                          cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


class Ws147DeviceProviderTests(unittest.TestCase):
    def test_no_device_and_transport_unavailable_have_contract_exit_codes(self) -> None:
        no_device = invoke("--output", "json", "--request-id", "host-1", "--fixture", "no-device", "discover")
        self.assertEqual(no_device.returncode, 0, no_device.stderr.decode())
        self.assertEqual(parse_provider_result(no_device.stdout)["devices"], [])

        unavailable = invoke("--output", "json", "--request-id", "host-2", "--fixture", "transport-unavailable",
                             "--selector", "fixture-ws147", "info")
        self.assertEqual(unavailable.returncode, 3, unavailable.stderr.decode())
        self.assertEqual(parse_provider_result(unavailable.stdout)["resultCode"], "transport-unavailable")

    def test_fixture_failures_and_cancellation_are_typed(self) -> None:
        storage_full = invoke("--output", "jsonl", "--request-id", "host-3", "--fixture", "storage-full",
                              "--selector", "fixture-ws147", "install", "--bundle", "C:/not-used.jfapp")
        self.assertEqual(storage_full.returncode, 1)
        self.assertEqual(parse_provider_jsonl(storage_full.stdout)[-1]["resultCode"], "storage-full")

        cancelled = invoke("--output", "json", "--request-id", "host-4", "--fixture", "confirmed-cancel",
                           "--selector", "fixture-ws147", "cancel", "--transaction-id", "1")
        self.assertEqual(cancelled.returncode, 0)
        result = parse_provider_result(cancelled.stdout)
        self.assertEqual(result["resultCode"], "ok")
        self.assertTrue(result["cancellation"]["confirmed"])

        unconfirmed = invoke("--output", "json", "--request-id", "host-5", "--fixture", "unconfirmed-cancel",
                             "--selector", "fixture-ws147", "cancel", "--transaction-id", "1")
        self.assertEqual(unconfirmed.returncode, 1)
        self.assertFalse(parse_provider_result(unconfirmed.stdout)["cancellation"]["confirmed"])

    def test_bounded_log_stream_and_manifest_mismatch_fixture(self) -> None:
        logs = invoke("--output", "jsonl", "--request-id", "host-6", "--fixture", "bounded-logs",
                      "--selector", "fixture-ws147", "logs", "--id", "org.jellyframe.fixture", "--limit", "2")
        self.assertEqual(logs.returncode, 0)
        events = parse_provider_jsonl(logs.stdout)
        self.assertEqual(len([event for event in events if event["kind"] == "log"]), 2)

        mismatch = invoke("--output", "json", "--request-id", "host-7", "--fixture", "image-mismatch", "discover")
        self.assertEqual(mismatch.returncode, 0)
        self.assertEqual(parse_provider_result(mismatch.stdout)["devices"][0]["profileId"], "wrong-profile")

    def test_runtime_cli_accepts_confirmed_cancel_and_rejects_manifest_mismatch(self) -> None:
        provider = str((ROOT / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe-device.cmd").resolve())
        cancelled = cli("--provider", provider, "cancel", "--selector", "fixture-ws147", "--transaction-id", "9",
                        fixture="confirmed-cancel")
        self.assertEqual(cancelled.returncode, 0, cancelled.stderr.decode())
        self.assertTrue(json.loads(cancelled.stdout)["cancellation"]["confirmed"])

        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "manifest.json"
            manifest.write_text(json.dumps({
                "format": "jellyframe.device-image", "formatVersion": 0,
                "imageId": "org.jellyframe.ws147.fixture", "imageVersion": "0.1.0-dev",
                "runtimeVersion": "0.6.0-dev", "renderCore": {"version": "0.6.1", "abi": 1},
                "source": {"revision": "0" * 40, "firmwareSha256": "0" * 64},
                "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
                "profile": {"id": "rect-172x320", "featureFamilies": ["core.document"]},
                "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
                "storage": {"maxBundleBytes": 327680},
                "recovery": {"procedureId": "fixture", "factoryImageSha256": "0" * 64},
            }), encoding="utf-8")
            mismatch = cli("--provider", provider, "--manifest", str(manifest), "discover", fixture="image-mismatch")
            self.assertEqual(mismatch.returncode, 4)
            self.assertIn(b"does not match", mismatch.stderr)

    def test_non_install_operation_results_do_not_emit_zero_transaction_id(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_result", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        self.assertEqual(provider.operation_result_fields({"transaction": {"id": 0}}), {})
        self.assertEqual(provider.operation_result_fields({"transaction": {"id": 7}})["transaction"]["id"], 7)

    def test_live_install_control_confirms_only_owner_reported_abort(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_live", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        endpoint = "fixture-live-cancel"
        device = provider.fixture_device()
        session = provider.LiveInstallSession(endpoint, 77, device)
        try:
            result: dict[str, object] = {}

            def request() -> None:
                result.update(provider.request_live_cancel(endpoint, 77) or {})

            import threading
            thread = threading.Thread(target=request)
            thread.start()
            self.assertTrue(session.cancel_requested.wait(1))
            session.finish(True, "cancelled")
            thread.join(2)
            self.assertEqual(result, {"confirmed": True, "resultCode": "cancelled", "device": device})
        finally:
            session.close()
        self.assertIsNone(provider.request_live_cancel(endpoint, 77))

    def test_live_cancel_rejects_an_unselected_endpoint_before_opening_transport(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_selector", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        args = argparse.Namespace(operation="cancel", selector="other-endpoint", transaction_id=1)
        config = provider.ProviderConfig("fixture-live-cancel", "COM19", 115200, Path("manifest.json"), {})
        with self.assertRaisesRegex(provider.ProviderError, "selector does not identify"):
            provider.run_physical(args, config)

    def test_identity_and_typed_logs_decoders_reject_unbounded_shapes(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_typed", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        identity = (bytes((1, 3, 3, 3, 5, 40, 0, 0)) + (1).to_bytes(4, "little") + (3).to_bytes(4, "little") +
                    b"imgprover0.6.1" + b"0" * 40)
        decoded = provider.decode_identity(identity)
        self.assertEqual(decoded["featureFamilies"], ["core.document", "core.paint"])
        with self.assertRaises(provider.ProviderError):
            provider.decode_identity(identity[:-1])
        payload = (bytes((1, 1, 0, 0)) + (0).to_bytes(4, "little") +
                   bytes((3, 4, 1, 0)) + (2).to_bytes(4, "little") + (99).to_bytes(8, "little") +
                   b"app" + b"log!")
        dropped, records = provider.decode_logs(payload)
        self.assertEqual((dropped, records[0]["timestampMs"]), (0, "99"))
        with self.assertRaises(provider.ProviderError):
            provider.decode_logs(payload + b"x")

    def test_jfapp_identity_comes_from_bundle_not_package_report(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "app"
            root.mkdir()
            (root / "jellyframe.app.json").write_text(json.dumps({
                "format": "jellyframe.app", "formatVersion": 0, "id": "org.example.installed",
                "name": "Installed", "version": {"name": "1.0.0", "code": 1}, "entry": "/index.html",
                "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.6.1", "script": "none"},
                "viewport": {"designWidth": 172, "designHeight": 320, "shape": "rect"},
                "budgets": {"maxResourceBytes": 1024, "maxDomNodes": 8, "maxCssRules": 4,
                            "maxDisplayCommands": 8, "maxTimers": 0, "maxEventListeners": 0},
                "capabilities": [], "targets": {"ws147": {"viewport": {"width": 172, "height": 320, "shape": "rect"},
                                                          "fontProfile": "tiny-plus-symbols", "output": "jfapp"}},
            }), encoding="utf-8")
            (root / "index.html").write_text("<main>installed</main>", encoding="utf-8")
            bundle = Path(temporary) / "installed.jfapp"
            report = Path(temporary) / "report.json"
            packaged = subprocess.run([sys.executable, str(ROOT / "tools" / "package_app.py"), "--root", str(root),
                                       "--output-bundle", str(bundle), "--report", str(report)], cwd=ROOT,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            self.assertEqual(packaged.returncode, 0, packaged.stderr.decode())
            report.unlink()
            self.assertEqual(provider.read_bundle_identity(bundle.resolve()), "org.example.installed")


if __name__ == "__main__":
    unittest.main()
