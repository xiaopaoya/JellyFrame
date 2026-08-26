#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
TESTS_DIR = REPO_ROOT / "tests" / "tool_regression"
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(TESTS_DIR))

import app_registry  # noqa: E402
import device_reference  # noqa: E402
from app_registry_tests import write_jfapp  # noqa: E402


CLI = REPO_ROOT / "tools" / "jellyframe_cli.py"
WIRE_VECTOR_FILE = REPO_ROOT / "tests" / "fixtures" / "jfdp_v1_wire_vectors.txt"


def load_wire_vectors() -> dict[str, bytes]:
    vectors: dict[str, bytes] = {}
    for raw_line in WIRE_VECTOR_FILE.read_text(encoding="ascii").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, separator, encoded = line.partition("=")
        if not separator or not name or not encoded or len(encoded) % 2:
            raise AssertionError(f"invalid JFDP wire vector: {raw_line}")
        if name in vectors:
            raise AssertionError(f"duplicate JFDP wire vector: {name}")
        vectors[name] = bytes.fromhex(encoded)
    if not vectors:
        raise AssertionError("JFDP wire vector fixture is empty")
    return vectors


class DeviceReferenceCliTests(unittest.TestCase):
    def run_cli(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CLI), *arguments],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_jfdp_wire_vectors_match_reference_codecs(self):
        vectors = load_wire_vectors()
        app_id = "org.jellyframe.demo"
        actual = {
            "frame-discovery": device_reference.encode_jfdp_frame(
                "discovery", 0x01020304, 0x10203040
            ),
            "capabilities-reference": device_reference._jfdp_reference_capabilities(),
            "frame-capabilities-response": device_reference.encode_jfdp_frame(
                "discovery",
                0x01020304,
                0x10203040,
                device_reference._jfdp_reference_capabilities(),
                response=True,
            ),
            "image-identity": device_reference.encode_jfdp_image_identity_payload(
                image_id="org.jellyframe.ws147.developer",
                profile_id="rect-172x320",
                image_version="0.1.0-dev",
                render_core_version="0.6.1",
                source_revision="0123456789abcdef0123456789abcdef01234567",
                render_core_abi=1,
                feature_family_bits=(
                    device_reference.JFDP_RENDER_CORE_FEATURE_DOCUMENT |
                    device_reference.JFDP_RENDER_CORE_FEATURE_PAINT |
                    device_reference.JFDP_RENDER_CORE_FEATURE_ADVANCED_FORMS |
                    device_reference.JFDP_RENDER_CORE_FEATURE_CANVAS2D
                ),
            ),
            "frame-identity-response": device_reference.encode_jfdp_frame(
                "identity", 0x01020304, 0x10203040,
                device_reference.encode_jfdp_image_identity_payload(
                    image_id="org.jellyframe.ws147.developer",
                    profile_id="rect-172x320",
                    image_version="0.1.0-dev",
                    render_core_version="0.6.1",
                    source_revision="0123456789abcdef0123456789abcdef01234567",
                    render_core_abi=1,
                    feature_family_bits=(
                        device_reference.JFDP_RENDER_CORE_FEATURE_DOCUMENT |
                        device_reference.JFDP_RENDER_CORE_FEATURE_PAINT |
                        device_reference.JFDP_RENDER_CORE_FEATURE_ADVANCED_FORMS |
                        device_reference.JFDP_RENDER_CORE_FEATURE_CANVAS2D
                    ),
                ),
                response=True,
            ),
            "install-begin": device_reference.encode_jfdp_install_begin_payload(
                0x11223344, app_id, 0x12345, 0x89ABCDEF, True
            ),
            "frame-install-begin": device_reference.encode_jfdp_frame(
                "install-begin",
                0x0A0B0C0D,
                0x01020304,
                device_reference.encode_jfdp_install_begin_payload(
                    0x11223344, app_id, 0x12345, 0x89ABCDEF, True
                ),
            ),
            "install-chunk": device_reference.encode_jfdp_install_chunk_payload(
                0x11223344, 0x20, bytes((0x00, 0x7F, 0x80, 0xFF))
            ),
            "frame-install-chunk": device_reference.encode_jfdp_frame(
                "install-chunk",
                0x0A0B0C0D,
                0x01020305,
                device_reference.encode_jfdp_install_chunk_payload(
                    0x11223344, 0x20, bytes((0x00, 0x7F, 0x80, 0xFF))
                ),
            ),
            "transaction": device_reference.encode_jfdp_transaction_payload(0x11223344),
            "app-id": device_reference.encode_jfdp_app_id_payload(app_id),
            "logs": device_reference.encode_jfdp_logs_request_payload(app_id, 11),
            "app-logs": device_reference.encode_jfdp_app_logs_payload([
                {"appId": "org.example.alpha", "message": "runtime started",
                 "generation": 18, "timestampMs": 123456789, "level": "info"},
                {"appId": "org.example.beta", "message": "budget exceeded",
                 "generation": 19, "timestampMs": 123456999, "level": "error"},
            ], dropped_records=3),
            "operation-result": device_reference.encode_jfdp_operation_result(
                "accepted",
                flags=device_reference.JFDP_RESULT_COMPLETE,
                transaction_id=0x11223344,
                received_bytes=0x100,
                expected_bytes=0x12345,
            ),
            "frame-install-commit-response": device_reference.encode_jfdp_frame(
                "install-commit",
                0x0A0B0C0D,
                0x01020306,
                device_reference.encode_jfdp_operation_result(
                    "accepted",
                    flags=device_reference.JFDP_RESULT_COMPLETE,
                    transaction_id=0x11223344,
                    received_bytes=0x100,
                    expected_bytes=0x12345,
                ),
                response=True,
            ),
        }
        self.assertEqual(set(vectors), set(actual))
        for name, encoded in actual.items():
            self.assertEqual(encoded, vectors[name], name)

    def test_device_commands_require_an_explicit_provider_and_keep_reference_separate(self):
        help_result = self.run_cli("device-reference", "--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn("no physical device is contacted", help_result.stdout)

        device_result = self.run_cli("device", "--help")
        self.assertEqual(device_result.returncode, 0, device_result.stderr)
        self.assertIn("no serial or USB endpoint", device_result.stdout)
        self.assertIn("is inferred", device_result.stdout)
        missing_provider = self.run_cli("device", "discover")
        self.assertEqual(missing_provider.returncode, 2)
        self.assertIn("--provider", missing_provider.stderr)

    def test_reference_endpoint_runs_install_list_state_remove(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-") as directory:
            root = Path(directory)
            bundle = root / "sample.jfapp"
            store = root / "store"
            write_jfapp(bundle, app_id="org.example.reference", version_code=1,
                        min_render_core=app_registry.active_render_core_release_version())

            install = self.run_cli(
                "device-reference", "--store", str(store),
                "install", "--bundle", str(bundle), "--json",
            )
            self.assertEqual(install.returncode, 0, install.stderr)
            self.assertEqual(json.loads(install.stdout)["id"], "org.example.reference")

            listing = self.run_cli(
                "device-reference", "--store", str(store), "list", "--json",
            )
            self.assertEqual(listing.returncode, 0, listing.stderr)
            self.assertEqual(len(json.loads(listing.stdout)["apps"]), 1)

            state = self.run_cli(
                "device-reference", "--store", str(store), "state", "--json",
            )
            self.assertEqual(state.returncode, 0, state.stderr)
            self.assertEqual(json.loads(state.stdout)["summary"]["launchableCount"], 1)

            remove = self.run_cli(
                "device-reference", "--store", str(store),
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
            render_core = app_registry.active_render_core_release_version()
            write_jfapp(first_bundle, app_id="org.example.lifecycle", version_code=1,
                        min_render_core=render_core)
            write_jfapp(second_bundle, app_id="org.example.lifecycle", version_code=2,
                        version_name="2.0.0", min_render_core=render_core)
            write_jfapp(cancelled_bundle, app_id="org.example.cancelled", version_code=1,
                        min_render_core=render_core)

            discovery = self.run_cli(
                "device-reference", "--store", str(store), "discover", "--json",
            )
            self.assertEqual(discovery.returncode, 0, discovery.stderr)
            self.assertFalse(json.loads(discovery.stdout)["deviceAvailable"])

            paused = self.run_cli(
                "device-reference", "--store", str(store), "install",
                "--bundle", str(first_bundle), "--chunk-bytes", "64", "--pause-after-chunks", "1", "--json",
            )
            self.assertEqual(paused.returncode, 0, paused.stderr)
            paused_result = json.loads(paused.stdout)
            self.assertFalse(paused_result["complete"])
            transaction_id = paused_result["transactionId"]

            no_partial = self.run_cli(
                "device-reference", "--store", str(store), "list", "--json",
            )
            self.assertEqual(no_partial.returncode, 0, no_partial.stderr)
            self.assertEqual(len(json.loads(no_partial.stdout)["apps"]), 0)

            incomplete_commit = self.run_cli(
                "device-reference", "--store", str(store), "commit",
                "--transaction-id", str(transaction_id), "--json",
            )
            self.assertEqual(incomplete_commit.returncode, 1)
            self.assertEqual(json.loads(incomplete_commit.stdout)["resultCode"], "invalid-request")

            recovery = self.run_cli(
                "device-reference", "--store", str(store), "recovery", "--json",
            )
            self.assertEqual(recovery.returncode, 0, recovery.stderr)
            recovery_result = json.loads(recovery.stdout)
            self.assertEqual(recovery_result["pendingTransactionCount"], 1)
            self.assertEqual(recovery_result["lastFailure"]["reason"], "invalid-request")

            resumed = self.run_cli(
                "device-reference", "--store", str(store), "resume",
                "--transaction-id", str(transaction_id), "--bundle", str(first_bundle), "--chunk-bytes", "64", "--json",
            )
            self.assertEqual(resumed.returncode, 0, resumed.stderr)
            self.assertEqual(json.loads(resumed.stdout)["versionCode"], 1)

            launch = self.run_cli(
                "device-reference", "--store", str(store), "launch",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(launch.returncode, 0, launch.stderr)
            self.assertTrue(json.loads(launch.stdout)["active"])
            stop = self.run_cli(
                "device-reference", "--store", str(store), "stop", "--json",
            )
            self.assertEqual(stop.returncode, 0, stop.stderr)
            self.assertFalse(json.loads(stop.stdout)["active"])

            update = self.run_cli(
                "device-reference", "--store", str(store), "install",
                "--bundle", str(second_bundle), "--chunk-bytes", "64", "--json",
            )
            self.assertEqual(update.returncode, 0, update.stderr)
            self.assertEqual(json.loads(update.stdout)["versionCode"], 2)
            rollback = self.run_cli(
                "device-reference", "--store", str(store), "rollback",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(rollback.returncode, 0, rollback.stderr)
            self.assertEqual(json.loads(rollback.stdout)["versionCode"], 1)

            cancelled = self.run_cli(
                "device-reference", "--store", str(store), "install",
                "--bundle", str(cancelled_bundle), "--chunk-bytes", "64", "--pause-after-chunks", "1", "--json",
            )
            self.assertEqual(cancelled.returncode, 0, cancelled.stderr)
            cancelled_id = json.loads(cancelled.stdout)["transactionId"]
            cancel = self.run_cli(
                "device-reference", "--store", str(store), "cancel",
                "--transaction-id", str(cancelled_id), "--json",
            )
            self.assertEqual(cancel.returncode, 0, cancel.stderr)
            self.assertEqual(json.loads(cancel.stdout)["resultCode"], "cancelled")
            self.assertIsNone(app_registry.existing_app_entry(store, "org.example.cancelled"))

            logs = self.run_cli(
                "device-reference", "--store", str(store), "logs",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(logs.returncode, 0, logs.stderr)
            events = [entry["event"] for entry in json.loads(logs.stdout)["logs"]]
            self.assertIn("install-commit", events)
            self.assertIn("launch", events)
            self.assertIn("rollback", events)

            remove = self.run_cli(
                "device-reference", "--store", str(store), "remove",
                "--id", "org.example.lifecycle", "--json",
            )
            self.assertEqual(remove.returncode, 0, remove.stderr)
            self.assertIsNone(app_registry.existing_app_entry(store, "org.example.lifecycle"))

    def test_typed_jfdp_dispatcher_preserves_request_correlation_and_atomic_visibility(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-jfdp-") as directory:
            root = Path(directory)
            store = root / "store"
            bundle_path = root / "typed.jfapp"
            app_id = "org.example.typed"
            write_jfapp(bundle_path, app_id=app_id, version_code=1,
                        min_render_core=app_registry.active_render_core_release_version())
            bundle = bundle_path.read_bytes()
            transaction_id = 41
            session_id = 0x1001

            def dispatch(message_type: str, request_id: int, payload: bytes) -> dict:
                response = device_reference.dispatch_jfdp_frame(
                    store,
                    device_reference.encode_jfdp_frame(message_type, session_id, request_id, payload),
                )
                decoded = device_reference.decode_jfdp_frame(response)
                self.assertTrue(decoded["response"])
                self.assertEqual(decoded["type"], message_type)
                self.assertEqual(decoded["sessionId"], session_id)
                self.assertEqual(decoded["requestId"], request_id)
                return decoded

            discovery = dispatch("discovery", 1, b"")
            self.assertEqual(discovery["payload"][0], 1)
            self.assertEqual(discovery["payload"][4:8], b"\x00\x00\x00\x00")

            begin = dispatch(
                "install-begin",
                2,
                device_reference.encode_jfdp_install_begin_payload(
                    transaction_id,
                    app_id,
                    len(bundle),
                    zlib.crc32(bundle) & 0xffffffff,
                    False,
                ),
            )
            begin_result = device_reference.decode_jfdp_operation_result(begin["payload"])
            self.assertEqual(begin_result["resultCode"], "accepted")
            self.assertEqual(begin_result["transactionId"], transaction_id)
            self.assertIsNone(app_registry.existing_app_entry(store, app_id))

            offset = 0
            request_id = 3
            while offset < len(bundle):
                chunk = bundle[offset:offset + 256]
                response = dispatch(
                    "install-chunk",
                    request_id,
                    device_reference.encode_jfdp_install_chunk_payload(transaction_id, offset, chunk),
                )
                progress = device_reference.decode_jfdp_operation_result(response["payload"])
                self.assertEqual(progress["resultCode"], "accepted")
                offset += len(chunk)
                request_id += 1
            self.assertTrue(progress["flags"] & device_reference.JFDP_RESULT_COMPLETE)
            self.assertIsNone(app_registry.existing_app_entry(store, app_id))

            committed = dispatch(
                "install-commit",
                request_id,
                device_reference.encode_jfdp_transaction_payload(transaction_id),
            )
            commit_result = device_reference.decode_jfdp_operation_result(committed["payload"])
            self.assertEqual(commit_result["resultCode"], "ok")
            self.assertTrue(commit_result["flags"] & device_reference.JFDP_RESULT_COMPLETE)
            self.assertIsNotNone(app_registry.existing_app_entry(store, app_id))

            launched = dispatch("launch", request_id + 1, device_reference.encode_jfdp_app_id_payload(app_id))
            self.assertTrue(device_reference.decode_jfdp_operation_result(launched["payload"])["flags"] &
                            device_reference.JFDP_RESULT_ACTIVE)
            stopped = dispatch("stop", request_id + 2, device_reference.encode_jfdp_app_id_payload(app_id))
            self.assertTrue(device_reference.decode_jfdp_operation_result(stopped["payload"])["flags"] &
                            device_reference.JFDP_RESULT_LAUNCHER_ACTIVE)

            logs = dispatch("logs", request_id + 3, device_reference.encode_jfdp_logs_request_payload(app_id, 11))
            self.assertEqual(device_reference.decode_jfdp_operation_result(logs["payload"])["resultCode"], "ok")

            removed = dispatch("remove", request_id + 4, device_reference.encode_jfdp_app_id_payload(app_id))
            self.assertEqual(device_reference.decode_jfdp_operation_result(removed["payload"])["resultCode"], "ok")
            self.assertIsNone(app_registry.existing_app_entry(store, app_id))

    def test_typed_jfdp_dispatcher_returns_typed_error_result(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-jfdp-") as directory:
            store = Path(directory) / "store"
            request = device_reference.encode_jfdp_frame(
                "install-commit", 4, 7, device_reference.encode_jfdp_transaction_payload(999))
            response = device_reference.decode_jfdp_frame(device_reference.dispatch_jfdp_frame(store, request))
            result = device_reference.decode_jfdp_operation_result(response["payload"])
            self.assertEqual(result["resultCode"], "not-found")
            self.assertEqual(response["sessionId"], 4)
            self.assertEqual(response["requestId"], 7)

    def test_operation_result_rejects_reserved_flags(self):
        with self.assertRaisesRegex(device_reference.ReferenceDeviceError, "invalid JFDP operation result"):
            device_reference.encode_jfdp_operation_result("ok", flags=0x8000)

        payload = bytearray(device_reference.encode_jfdp_operation_result("ok"))
        payload[2] = 0x80
        with self.assertRaisesRegex(device_reference.ReferenceDeviceError, "invalid JFDP operation result"):
            device_reference.decode_jfdp_operation_result(bytes(payload))

    def test_typed_jfdp_rejects_a_bundle_whose_manifest_id_differs_from_begin(self):
        with tempfile.TemporaryDirectory(prefix="jellyframe-device-reference-jfdp-") as directory:
            root = Path(directory)
            store = root / "store"
            bundle_path = root / "mismatch.jfapp"
            bundle_app_id = "org.example.bundle"
            declared_app_id = "org.example.declared"
            write_jfapp(bundle_path, app_id=bundle_app_id, version_code=1,
                        min_render_core=app_registry.active_render_core_release_version())
            bundle = bundle_path.read_bytes()
            transaction_id = 8

            def dispatch(message_type: str, request_id: int, payload: bytes) -> dict:
                response = device_reference.dispatch_jfdp_frame(
                    store, device_reference.encode_jfdp_frame(message_type, 1, request_id, payload))
                return device_reference.decode_jfdp_frame(response)

            dispatch("install-begin", 1, device_reference.encode_jfdp_install_begin_payload(
                transaction_id, declared_app_id, len(bundle), zlib.crc32(bundle) & 0xffffffff, False))
            offset = 0
            request_id = 2
            while offset < len(bundle):
                chunk = bundle[offset:offset + 256]
                dispatch("install-chunk", request_id,
                         device_reference.encode_jfdp_install_chunk_payload(transaction_id, offset, chunk))
                offset += len(chunk)
                request_id += 1
            response = dispatch("install-commit", request_id,
                                device_reference.encode_jfdp_transaction_payload(transaction_id))
            result = device_reference.decode_jfdp_operation_result(response["payload"])
            self.assertEqual(result["resultCode"], "integrity-failed")
            self.assertIsNone(app_registry.existing_app_entry(store, bundle_app_id))
            self.assertFalse(device_reference.transaction_path(store, transaction_id).exists())


if __name__ == "__main__":
    unittest.main()
