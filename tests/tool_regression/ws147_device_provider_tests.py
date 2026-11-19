#!/usr/bin/env python3
"""No-device regression coverage for the explicit WS147 Device OS provider."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe_device.py"
sys.path.insert(0, str(ROOT / "tools"))
from device_provider_contract import parse_provider_jsonl, parse_provider_result  # noqa: E402
from jellyframe_versions import active_render_core_release_version  # noqa: E402


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
    def test_lifecycle_capability_fixture_declares_and_executes_each_operation(self) -> None:
        discovery = invoke("--output", "json", "--request-id", "host-capabilities", "--fixture", "lifecycle-ok",
                           "discover")
        self.assertEqual(discovery.returncode, 0, discovery.stderr.decode())
        operations = parse_provider_result(discovery.stdout)["devices"][0]["capabilities"]["supportedOperations"]
        self.assertEqual(operations, ["install", "cancel", "launch", "stop", "remove", "rollback", "logs", "recovery"])
        for operation in operations:
            with self.subTest(operation=operation):
                arguments = ["--output", "jsonl" if operation in {"install", "logs"} else "json",
                             "--request-id", f"host-{operation}", "--fixture", "lifecycle-ok",
                             "--selector", "fixture-ws147", operation]
                if operation == "install":
                    arguments.extend(["--bundle", "C:/fixture.jfapp"])
                elif operation == "cancel":
                    arguments.extend(["--transaction-id", "1"])
                elif operation in {"launch", "stop", "remove", "rollback", "logs"}:
                    arguments.extend(["--id", "org.jellyframe.fixture"])
                completed = invoke(*arguments)
                self.assertEqual(completed.returncode, 0, completed.stderr.decode())
                terminal = (parse_provider_jsonl(completed.stdout)[-1]
                            if operation in {"install", "logs"} else parse_provider_result(completed.stdout))
                self.assertEqual(terminal["operation"], operation)
                self.assertEqual(terminal["device"]["endpointId"], "fixture-ws147")

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
        with tempfile.TemporaryDirectory() as temporary:
            provider = ROOT / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe-device.cmd"
            if os.name != "nt":
                provider = Path(temporary) / "jellyframe-device"
                provider.write_text(
                    f"#!{sys.executable}\n"
                    "import os, sys\n"
                    f"os.execv(sys.executable, [sys.executable, {str(PROVIDER)!r}, *sys.argv[1:]])\n",
                    encoding="utf-8",
                )
                provider.chmod(0o755)

            cancelled = cli("--provider", str(provider.resolve()), "cancel", "--selector", "fixture-ws147",
                            "--transaction-id", "9", fixture="confirmed-cancel")
            self.assertEqual(cancelled.returncode, 0, cancelled.stderr.decode())
            self.assertTrue(json.loads(cancelled.stdout)["cancellation"]["confirmed"])

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
        with tempfile.TemporaryDirectory(prefix="jellyframe-live-session-") as directory:
            with mock.patch.object(provider.tempfile, "gettempdir", return_value=directory):
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

    def test_install_prepares_local_cancel_session_before_remote_begin(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_install_order", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)

        requests: list[int] = []

        class RecordingWire:
            def __init__(self, _config: object) -> None:
                pass

            def open(self) -> None:
                pass

            def request(self, message_type: int, *_args: object, **_kwargs: object) -> bytes:
                requests.append(message_type)
                return b"stub"

            def close(self) -> None:
                pass

        with tempfile.TemporaryDirectory(prefix="jellyframe-install-session-order-") as directory:
            bundle = Path(directory) / "candidate.jfapp"
            bundle.write_bytes(b"test bundle")
            endpoint = "fixture-install-session-order"
            args = argparse.Namespace(operation="install", selector=endpoint, request_id="install-order",
                                      bundle=bundle, allow_downgrade=False)
            config = provider.ProviderConfig(endpoint, "COM19", 115200, Path("manifest.json"), {"renderCore": {"abi": 1}})
            with (
                mock.patch.object(provider, "Wire", RecordingWire),
                mock.patch.object(provider, "decode_capabilities", return_value={}),
                mock.patch.object(provider, "decode_identity", return_value={}),
                mock.patch.object(provider, "device_from_capabilities", return_value=provider.fixture_device()),
                mock.patch.object(provider, "read_target_package", return_value=(b"test bundle", {"appId": "org.jellyframe.fixture"})),
                mock.patch.object(provider, "LiveInstallSession", side_effect=OSError("local temp unavailable")),
            ):
                with self.assertRaisesRegex(OSError, "local temp unavailable"):
                    provider.run_physical(args, config)

        self.assertNotIn(provider.INSTALL_BEGIN, requests)

    def test_slow_bundle_lifecycle_can_complete_without_extending_other_control_deadlines(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_launch", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)

        class SlowLifecycleWire:
            def __init__(self, _config: object) -> None:
                pass

            def open(self) -> None:
                pass

            def close(self) -> None:
                pass

            def request(self, message_type: int, _payload: bytes = b"", timeout_seconds: float = 3.0) -> bytes:
                if message_type in {provider.DISCOVERY, provider.IDENTITY}:
                    return b"stub"
                if timeout_seconds < 8.2:
                    raise provider.ProviderError("transport-unavailable", "simulated response deadline")
                return bytes((1, 0, 3, 0)) + bytes(12)

        device = provider.fixture_device()
        endpoint = device["endpointId"]
        config = provider.ProviderConfig(endpoint, "COM19", 115200, Path("manifest.json"), {})
        args = argparse.Namespace(operation="launch", selector=endpoint, request_id="slow-launch",
                                  app_id="org.jellyframe.fixture")
        with (
            mock.patch.object(provider, "Wire", SlowLifecycleWire),
            mock.patch.object(provider, "decode_capabilities", return_value={}),
            mock.patch.object(provider, "decode_identity", return_value={}),
            mock.patch.object(provider, "device_from_capabilities", return_value=device),
        ):
            for operation in ("launch", "rollback"):
                with self.subTest(operation=operation):
                    args.operation = operation
                    status, events = provider.run_physical(args, config)
                    self.assertEqual((status, events[0]["resultCode"]), (0, "ok"))
            for operation in ("stop", "remove"):
                with self.subTest(operation=operation):
                    args.operation = operation
                    with self.assertRaisesRegex(provider.ProviderError, "simulated response deadline"):
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

    def test_identity_mismatch_names_the_attested_field_and_recovery_choices(self) -> None:
        spec = importlib.util.spec_from_file_location("ws147_device_provider_identity", PROVIDER)
        assert spec is not None and spec.loader is not None
        provider = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = provider
        spec.loader.exec_module(provider)
        manifest = {
            "imageId": "org.jellyframe.ws147.developer", "imageVersion": "0.6.0-a2",
            "runtimeVersion": "0.6.0-dev", "renderCore": {"version": "0.6.1", "abi": 1},
            "source": {"revision": "9e32faced67473c17b50f0268ad7080b9595482a"},
            "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
            "profile": {"id": "rect-172x320", "featureFamilies": ["core.document"]},
            "storage": {"maxBundleBytes": 327680},
        }
        capabilities = {"boardId": "ws147", "runtimeVersion": "0.6.0-dev", "width": 172,
                        "height": 320, "maxBundleBytes": 327680}
        identity = {"imageId": "org.jellyframe.ws147.developer", "profileId": "rect-172x320",
                    "imageVersion": "0.6.0-a2", "renderCoreVersion": "0.6.1",
                    "sourceRevision": "846118199b5719d11255240d2c8a8f7ce62d9d6a",
                    "renderCoreAbi": 1, "featureFamilies": ["core.document"]}
        config = provider.ProviderConfig("fixture", "COM19", 115200, Path("manifest.json"), manifest)
        with self.assertRaisesRegex(provider.ProviderError, "identity mismatch at sourceRevision") as raised:
            provider.device_from_capabilities(config, capabilities, identity)
        self.assertIn("9e32faced", str(raised.exception))
        self.assertIn("846118199", str(raised.exception))
        self.assertIn("or flash the Developer Image", str(raised.exception))

        contextual = ["core.document", "core.paint", "text.contextual-jfsp2", "text.contextual-layout"]
        manifest["profile"]["featureFamilies"] = contextual
        manifest["renderCore"]["abi"] = 9
        identity.update(sourceRevision=manifest["source"]["revision"], renderCoreAbi=10,
                        featureFamilies=contextual)
        with self.assertRaisesRegex(provider.ProviderError, "identity mismatch at renderCoreAbi"):
            provider.device_from_capabilities(config, capabilities, identity)

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
                "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": active_render_core_release_version(), "script": "none"},
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
