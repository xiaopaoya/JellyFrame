#!/usr/bin/env python3
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import device_provider_contract as contract  # noqa: E402


def device() -> dict:
    return {"endpointId": "usb-ws147-001", "boardId": "ws147", "profileId": "rect-172x320", "imageVersion": "0.1.0-dev", "runtimeVersion": "0.6.0-dev", "protocol": "JFDP/1", "connected": True, "capabilities": {"display": {"width": 172, "height": 320, "shape": "rect"}, "featureFamilies": ["core.document"], "maxBundleBytes": 1048576, "availableStorageBytes": 524288}}


def result(**extra: object) -> dict:
    base = {"format": contract.FORMAT, "formatVersion": contract.FORMAT_VERSION, "kind": "result", "operation": "discover", "requestId": "host-42", "resultCode": "ok", "provider": {"id": "jellyframe-device", "version": "0.1.0-dev"}}
    base.update(extra)
    return base


def event(kind: str, sequence: int, **extra: object) -> dict:
    base = {"format": contract.FORMAT, "formatVersion": contract.FORMAT_VERSION,
            "kind": kind, "operation": "install", "requestId": "host-42", "sequence": sequence,
            "provider": {"id": "jellyframe-device", "version": "0.1.0-dev"}}
    base.update(extra)
    return base


def app_entry() -> dict:
    return {"appId": "org.example.app", "versionName": "1.2.3", "versionCode": 7,
            "bundleBytes": 2048, "state": "installed", "rollbackAvailable": True}


def recovery() -> dict:
    return {"appId": "org.example.app", "registryGeneration": 42, "recoverySequence": 9,
            "reason": "app-runtime-failure", "launcherActive": True, "appDisabled": True,
            "rollbackAvailable": True}


def identity() -> dict:
    return {"imageId": "org.jellyframe.ws147.developer", "profileId": "rect-172x320",
            "imageVersion": "0.1.0-dev", "renderCoreVersion": "0.6.1",
            "sourceRevision": "0123456789abcdef0123456789abcdef01234567", "renderCoreAbi": 1,
            "featureFamilies": ["core.document", "core.paint", "forms.advanced"]}


class DeviceProviderContractTests(unittest.TestCase):
    def test_accepts_bounded_discovery(self):
        parsed = contract.parse_provider_result(json.dumps(result(devices=[device()])))
        self.assertEqual(parsed["devices"][0]["profileId"], "rect-172x320")

    def test_supported_operations_is_optional_and_strict_when_present(self):
        self.assertEqual(contract.parse_provider_result(json.dumps(result(devices=[device()])))["devices"][0]["endpointId"],
                         "usb-ws147-001")
        capable = device()
        capable["capabilities"]["supportedOperations"] = ["install", "launch", "logs"]
        parsed = contract.parse_provider_result(json.dumps(result(devices=[capable])))
        self.assertEqual(parsed["devices"][0]["capabilities"]["supportedOperations"], ["install", "launch", "logs"])
        for operations in (["discover"], ["install", "install"], ["unknown"], "install", [[]],
                           ["launch", "install"]):
            invalid = device()
            invalid["capabilities"]["supportedOperations"] = operations
            with self.assertRaises(contract.ProviderContractError):
                contract.parse_provider_result(json.dumps(result(devices=[invalid])))

    def test_rejects_unknown_fields_and_duplicate_members(self):
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(extra=True)))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result('{"format":"a","format":"b"}')

    def test_rejects_untrusted_identity_and_invalid_log_summaries(self):
        invalid = device()
        invalid["protocol"] = "JFDP/2"
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(devices=[invalid])))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="logs")))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(
                result(operation="logs", logSummary={"returnedRecords": 1, "droppedRecords": -1})))

    def test_rejects_ambiguous_feature_families(self):
        invalid = device()
        invalid["capabilities"]["featureFamilies"] = ["core.document", "core.document"]
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(devices=[invalid])))

    def test_accepts_only_explicit_cancellation_confirmation(self):
        accepted = result(operation="cancel", device=device(), cancellation={"confirmed": True})
        self.assertTrue(contract.parse_provider_result(json.dumps(accepted))["cancellation"]["confirmed"])
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(
                result(operation="cancel", device=device(), cancellation={"confirmed": "yes"})))

    def test_accepts_typed_app_list_and_recovery_results(self):
        app_list = result(operation="list", device=device(), apps=[app_entry()], registryGeneration=42)
        parsed_list = contract.parse_provider_result(json.dumps(app_list))
        self.assertEqual(parsed_list["apps"][0]["versionCode"], 7)
        recovery_result = result(operation="recovery", device=device(), recovery=recovery())
        self.assertEqual(contract.parse_provider_result(json.dumps(recovery_result))["recovery"]["reason"],
                         "app-runtime-failure")

    def test_accepts_only_attested_selected_device_identity(self):
        info = result(operation="info", device=device(), identity=identity())
        self.assertEqual(contract.parse_provider_result(json.dumps(info))["identity"]["renderCoreAbi"], 1)
        incomplete = result(operation="info", device=device())
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(incomplete))
        mismatched = identity()
        mismatched["profileId"] = "round-300"
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="info", device=device(), identity=mismatched)))
        malformed = identity()
        malformed["sourceRevision"] = "A" * 40
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="info", device=device(), identity=malformed)))

    def test_rejects_untyped_or_incomplete_app_list_and_recovery_results(self):
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="list")))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="list", apps=[app_entry()])))
        invalid_app = app_entry()
        invalid_app["state"] = "running"
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(
                result(operation="list", apps=[invalid_app], registryGeneration=1)))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(
                result(operation="list", apps=[app_entry(), app_entry()], registryGeneration=1)))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="recovery")))
        invalid_recovery = recovery()
        invalid_recovery["launcherActive"] = "yes"
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="recovery", recovery=invalid_recovery)))
        device_recovery = recovery()
        device_recovery["appId"] = ""
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(operation="recovery", recovery=device_recovery)))

    def test_requires_an_attested_device_for_successful_selected_operations(self):
        with self.assertRaisesRegex(contract.ProviderContractError, "missing device"):
            contract.parse_provider_result(json.dumps(result(operation="launch")))
        self.assertEqual(
            contract.parse_provider_result(json.dumps(
                result(operation="launch", resultCode="transport-unavailable")))["resultCode"],
            "transport-unavailable",
        )

    def test_rejects_missing_device_on_declared_operation_terminal(self):
        capable = device()
        capable["capabilities"]["supportedOperations"] = ["install"]
        discovery = contract.parse_provider_result(json.dumps(result(devices=[capable])))
        self.assertEqual(discovery["devices"][0]["capabilities"]["supportedOperations"], ["install"])
        with self.assertRaisesRegex(contract.ProviderContractError, "missing device"):
            contract.parse_provider_result(json.dumps(result(operation="install")))

    def test_accepts_ordered_install_progress_and_result(self):
        stream = "\n".join((
            json.dumps(event("progress", 1, progress={"completedBytes": 0, "totalBytes": 1500})),
            json.dumps(event("result", 2, resultCode="ok", device=device())),
        ))
        parsed = contract.parse_provider_jsonl(stream)
        self.assertEqual([item["kind"] for item in parsed], ["progress", "result"])

    def test_accepts_ordered_typed_log_events_and_summary(self):
        stream = "\n".join((
            json.dumps(event("log", 1, operation="logs", log={
                "level": "info", "appId": "org.example.app", "generation": 42,
                "timestampMs": "18446744073709551615", "message": "started"})),
            json.dumps(event("result", 2, operation="logs", resultCode="ok",
                             device=device(), logSummary={"returnedRecords": 1, "droppedRecords": 3})),
        ))
        parsed = contract.parse_provider_jsonl(stream)
        self.assertEqual(parsed[0]["log"]["timestampMs"], "18446744073709551615")
        self.assertEqual(parsed[-1]["logSummary"]["droppedRecords"], 3)

    def test_rejects_malformed_or_mismatched_typed_log_events(self):
        malformed = event("log", 1, operation="logs", log={
            "level": "info", "appId": "org.example.app", "message": "started"})
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((
                json.dumps(malformed),
                json.dumps(event("result", 2, operation="logs", resultCode="ok",
                                 device=device(), logSummary={"returnedRecords": 1, "droppedRecords": 0})),
            )))
        invalid_timestamp = event("log", 1, operation="logs", log={
            "level": "info", "appId": "org.example.app", "generation": 1,
            "timestampMs": 123, "message": "started"})
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((
                json.dumps(invalid_timestamp),
                json.dumps(event("result", 2, operation="logs", resultCode="ok",
                                 device=device(), logSummary={"returnedRecords": 1, "droppedRecords": 0})),
            )))
        oversized = event("log", 1, operation="logs", log={
            "level": "info", "appId": "org.example.app", "generation": 1,
            "timestampMs": "1", "message": "x" * 256})
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((
                json.dumps(oversized),
                json.dumps(event("result", 2, operation="logs", resultCode="ok",
                                 device=device(), logSummary={"returnedRecords": 1, "droppedRecords": 0})),
            )))
        mismatched_count = "\n".join((
            json.dumps(event("log", 1, operation="logs", log={
                "level": "info", "appId": "org.example.app", "generation": 1,
                "timestampMs": "1", "message": "started"})),
            json.dumps(event("result", 2, operation="logs", resultCode="ok",
                             device=device(), logSummary={"returnedRecords": 0, "droppedRecords": 0})),
        ))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl(mismatched_count)
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((
                json.dumps(event("log", 1, log={
                    "level": "info", "appId": "org.example.app", "generation": 1,
                    "timestampMs": "1", "message": "wrong operation"})),
                json.dumps(event("result", 2, resultCode="ok")),
            )))

    def test_rejects_jsonl_without_terminal_or_with_identity_drift(self):
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl(json.dumps(event("progress", 1, progress={"completedBytes": 0, "totalBytes": 1})))
        changed = event("result", 2, resultCode="ok")
        changed["requestId"] = "host-43"
        stream = "\n".join((
            json.dumps(event("progress", 1, progress={"completedBytes": 0, "totalBytes": 1})),
            json.dumps(changed),
        ))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl(stream)

    def test_rejects_jsonl_out_of_order_or_early_terminal(self):
        terminal = event("result", 2, resultCode="ok")
        late = event("log", 3, operation="logs", log={
            "level": "info", "appId": "org.example.app", "generation": 1,
            "timestampMs": "1", "message": "late"})
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((json.dumps(terminal), json.dumps(late))))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_jsonl("\n".join((
                json.dumps(event("progress", 2, progress={"completedBytes": 0, "totalBytes": 1})),
                json.dumps(event("result", 1, resultCode="ok")),
            )))
        invalid = device()
        invalid["capabilities"]["featureFamilies"] = ["Core Document"]
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(devices=[invalid])))


if __name__ == "__main__":
    unittest.main()
