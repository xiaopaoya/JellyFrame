#!/usr/bin/env python3
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import device_image_manifest as contract  # noqa: E402
import device_provider_contract as provider  # noqa: E402


def manifest() -> dict:
    return {
        "format": contract.FORMAT,
        "formatVersion": contract.FORMAT_VERSION,
        "imageId": "org.jellyframe.ws147.developer",
        "imageVersion": "0.1.0-dev",
        "runtimeVersion": "0.6.0-dev",
        "renderCore": {"version": "0.6.0", "abi": 1},
        "source": {"revision": "a" * 40, "firmwareSha256": "b" * 64},
        "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
        "profile": {"id": "rect-172x320", "featureFamilies": ["core.document", "css.background"]},
        "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
        "storage": {"maxBundleBytes": 327680},
        "recovery": {"procedureId": "ws147-usb-recovery-v1", "factoryImageSha256": "c" * 64},
    }


def device() -> dict:
    return {
        "endpointId": "usb-ws147-001", "boardId": "ws147", "profileId": "rect-172x320",
        "imageVersion": "0.1.0-dev", "runtimeVersion": "0.6.0-dev", "protocol": "JFDP/1", "connected": True,
        "capabilities": {"display": {"width": 172, "height": 320, "shape": "rect"},
                         "featureFamilies": ["css.background", "core.document"],
                         "maxBundleBytes": 327680, "availableStorageBytes": 65536},
    }


class DeviceImageManifestTests(unittest.TestCase):
    def test_accepts_bounded_manifest_and_matching_provider(self):
        parsed = contract.parse_device_image_manifest(json.dumps(manifest()))
        checked_device = provider.parse_provider_result(json.dumps({
            "format": provider.FORMAT, "formatVersion": provider.FORMAT_VERSION, "kind": "result",
            "operation": "discover", "requestId": "host-42", "resultCode": "ok",
            "provider": {"id": "jellyframe-device", "version": "0.1.0-dev"}, "devices": [device()],
        }))["devices"][0]
        contract.validate_provider_device(parsed, checked_device)

    def test_rejects_duplicate_and_incomplete_manifests(self):
        with self.assertRaises(contract.DeviceImageManifestError):
            contract.parse_device_image_manifest('{"format":"a","format":"b"}')
        invalid = manifest()
        invalid["source"]["firmwareSha256"] = "invalid"
        with self.assertRaises(contract.DeviceImageManifestError):
            contract.parse_device_image_manifest(json.dumps(invalid))

    def test_rejects_provider_identity_or_capability_drift(self):
        parsed = contract.parse_device_image_manifest(json.dumps(manifest()))
        changed = device()
        changed["imageVersion"] = "0.1.1-dev"
        with self.assertRaises(contract.DeviceImageManifestError):
            contract.validate_provider_device(parsed, changed)
        changed = device()
        changed["capabilities"]["featureFamilies"] = ["core.document"]
        with self.assertRaises(contract.DeviceImageManifestError):
            contract.validate_provider_device(parsed, changed)


if __name__ == "__main__":
    unittest.main()
