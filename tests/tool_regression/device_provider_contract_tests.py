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


if __name__ == "__main__":
    unittest.main()
