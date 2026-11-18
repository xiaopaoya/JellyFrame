#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import package_app
import sign_app_package as signer
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils


def fixture(root, shaped=False, version=1, capacity_bytes=0, pipeline_kind="jfsp1-block-v11"):
    manifest = package_app.read_manifest(ROOT / "samples/apps/packages/jelly_font_policy")
    manifest["id"] = "org.jellyframe.signed-test"
    manifest["version"] = {"name": f"1.0.{version}", "code": version}
    manifest["fonts"] = []
    if shaped:
        manifest["runtime"]["script"] = "classic"
        manifest["textPipeline"] = {
            "kind": pipeline_kind, "font": "/font.jffont2", "catalog": "/catalog",
            "companion": "/companion", "tables": "/tables", "eligibility": "/eligibility",
            "sourceIdentity": "1" * 64, "profileIdentity": "2" * 64,
            "glyphCount": 30900, "resourceId": 555, "rasterSize": 16, "weight": 400, "selection": 0,
        }
    manifest = package_app.validate_manifest(manifest)
    source = root / "source.txt"
    source.write_bytes(b"<body>signed app</body>")
    paths = [manifest["entry"]]
    if shaped:
        paths += [manifest["textPipeline"][key] for key in ("font", "catalog", "companion", "tables", "eligibility")]
    resources = [{"path": path, "file": source, "kind": "jellyframe::HostResourceKind::Other"} for path in paths]
    if capacity_bytes:
        capacity = root / "capacity.bin"
        capacity.write_bytes(bytes((i % 251 for i in range(capacity_bytes))))
        resources.append({"path": "/capacity.bin", "file": capacity,
                          "kind": "jellyframe::HostResourceKind::Other"})
    output = root / f"app-{version}-{'shaped' if shaped else 'scalar'}.jfapp"
    package_app.write_jfapp_bundle(output, manifest, resources)
    return output.read_bytes()


class SignedPackageTests(unittest.TestCase):
    def test_exact_layout_and_signature_for_both_formats(self):
        key = ec.derive_private_key(1, ec.SECP256R1())  # Public test scalar only.
        for pipeline_kind in (None, "jfsp1-block-v11", "jfsp2-block-v12"):
            shaped = pipeline_kind is not None
            with self.subTest(pipeline=pipeline_kind), tempfile.TemporaryDirectory() as directory:
                bundle = fixture(Path(directory), shaped, pipeline_kind=pipeline_kind)
                facts, resources = signer.bundle_facts(bundle)
                encoded = signer.sign_bundle(bundle, key, bytes(range(16)))
                self.assertEqual(encoded[:8], b"JFSG\0\0\0\0")
                self.assertEqual(struct.unpack_from("<HHHH", encoded, 8), (1, 64, 1, 0))
                length = struct.unpack_from("<I", encoded, 16)[0]
                manifest = encoded[64:64 + length]
                signature = encoded[64 + length:128 + length]
                self.assertEqual(encoded[128 + length:], bundle)
                der = utils.encode_dss_signature(int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big"))
                message = signer.DOMAIN + encoded[:64] + manifest
                key.public_key().verify(der, message, ec.ECDSA(hashes.SHA256()))
                from cryptography.exceptions import InvalidSignature
                with self.assertRaises(InvalidSignature):
                    key.public_key().verify(der, signer.DOMAIN[:-1] + encoded[:64] + manifest, ec.ECDSA(hashes.SHA256()))
                for path, digest in resources:
                    self.assertIn(struct.pack("<H", len(path)) + path + digest, manifest)
                self.assertIn(hashlib.sha256(bundle).digest(), manifest)
                self.assertEqual(facts["formatVersion"], int(shaped))

    def test_resigning_cannot_hide_invalid_resource_crc_or_path_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = fixture(Path(directory))
            index = struct.unpack_from("<I", bundle, 24)[0]
            payload = struct.unpack_from("<I", bundle, 40)[0]
            for offset in (index, index + 20, payload):
                changed = bytearray(bundle)
                changed[offset] ^= 1
                struct.pack_into("<I", changed, 48, 0)
                struct.pack_into("<I", changed, 48, zlib.crc32(changed) & 0xffffffff)
                with self.subTest(offset=offset), self.assertRaises(ValueError):
                    signer.bundle_facts(bytes(changed))

    def test_key_and_identifier_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = fixture(Path(directory))
            key = ec.derive_private_key(1, ec.SECP256R1())
            for bad in (b"", bytes(16), bytes(15), bytes(17)):
                with self.assertRaises(ValueError): signer.sign_bundle(bundle, key, bad)
            with self.assertRaises(ValueError):
                signer.sign_bundle(bundle, ec.derive_private_key(1, ec.SECP384R1()), bytes(range(16)))

    def test_duplicate_or_reordered_resource_index_is_not_signed(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = fixture(Path(directory), shaped=True)
            index = struct.unpack_from("<I", bundle, 24)[0]
            for duplicate in (False, True):
                changed = bytearray(bundle)
                first, second = bytes(changed[index:index+28]), bytes(changed[index+28:index+56])
                changed[index:index+28] = second
                changed[index+28:index+56] = second if duplicate else first
                struct.pack_into("<I", changed, 48, 0)
                struct.pack_into("<I", changed, 48, zlib.crc32(changed) & 0xffffffff)
                with self.subTest(duplicate=duplicate), self.assertRaisesRegex(ValueError, "duplicate|sorted"):
                    signer.bundle_facts(bytes(changed))

    def test_cli_outputs_no_private_material_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = fixture(root)
            source = root / "input.jfapp"
            source.write_bytes(bundle)
            key = ec.derive_private_key(1, ec.SECP256R1())
            pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
            key_file = root / "public-test-key.pem"
            key_file.write_bytes(pem)
            output = root / "test.jfsg"
            command = [sys.executable, str(ROOT / "tools/sign_app_package.py"), "--bundle", str(source),
                       "--private-key", str(key_file), "--key-id", bytes(range(16)).hex(), "--output", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["sha256"], hashlib.sha256(output.read_bytes()).hexdigest())
            before = output.read_bytes()
            self.assertNotIn(b"PRIVATE KEY", before)
            self.assertNotIn("PRIVATE KEY", result.stdout + result.stderr)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), before)
            self.assertEqual(key_file.read_bytes(), pem)
            self.assertEqual(source.read_bytes(), bundle)


if __name__ == "__main__":
    unittest.main()
