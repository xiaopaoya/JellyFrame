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


class DeviceProviderContractTests(unittest.TestCase):
    def test_accepts_bounded_discovery(self):
        parsed = contract.parse_provider_result(json.dumps(result(devices=[device()])))
        self.assertEqual(parsed["devices"][0]["profileId"], "rect-172x320")

    def test_rejects_unknown_fields_and_duplicate_members(self):
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(extra=True)))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result('{"format":"a","format":"b"}')

    def test_rejects_untrusted_identity_and_oversized_logs(self):
        invalid = device()
        invalid["protocol"] = "JFDP/2"
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(devices=[invalid])))
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(logs=[{}] * 257)))

    def test_rejects_ambiguous_feature_families(self):
        invalid = device()
        invalid["capabilities"]["featureFamilies"] = ["core.document", "core.document"]
        with self.assertRaises(contract.ProviderContractError):
            contract.parse_provider_result(json.dumps(result(devices=[invalid])))

    def test_accepts_ordered_jsonl_progress_log_and_result(self):
        stream = "\n".join((
            json.dumps(event("progress", 1, progress={"completedBytes": 0, "totalBytes": 1500})),
            json.dumps(event("log", 2, log={"level": "info", "appId": "org.example.app", "message": "installing"})),
            json.dumps(event("result", 3, resultCode="ok", device=device())),
        ))
        parsed = contract.parse_provider_jsonl(stream)
        self.assertEqual([item["kind"] for item in parsed], ["progress", "log", "result"])

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
        late = event("log", 3, log={"level": "info", "appId": "org.example.app", "message": "late"})
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
