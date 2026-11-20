import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from generate_package_trust import FIELD, generate


class PackageTrustTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.input = Path(directory.name) / "public.json"
        self.output = Path(directory.name) / "output"
        self.fixture = json.loads((HERE / "configured_public_trust.json").read_text(encoding="ascii"))

    def write(self, document):
        self.input.write_text(json.dumps(document), encoding="utf-8")

    def test_regeneration_clears_old_trust_and_is_deterministic(self):
        self.write(self.fixture)
        generate(self.input, self.output)
        header = self.output / "jfsg_trusted_public_keys.inc"
        receipt = self.output / "jfsg_package_trust_receipt.json"
        original_header = header.read_bytes()
        original_receipt = receipt.read_bytes()
        self.assertEqual(json.loads(original_receipt)["headerSha256"],
                         hashlib.sha256(original_header).hexdigest())
        self.assertEqual(json.loads(original_receipt)["keyIds"], [self.fixture["keys"][0]["keyId"]])
        self.input.write_text(json.dumps(self.fixture, indent=2, sort_keys=True), encoding="ascii")
        generate(self.input, self.output)
        self.assertEqual(header.read_bytes(), original_header)
        self.assertEqual(receipt.read_bytes(), original_receipt)
        generate(None, self.output)
        self.assertIn(b"std::array<PublicKey, 0>", header.read_bytes())
        self.assertEqual(json.loads(receipt.read_bytes())["keyIds"], [])

    def test_invalid_keys(self):
        key = self.fixture["keys"][0]
        cases = (
            {"x": key["x"][:-1]},
            {"y": "00" * 32},
            {"x": f"{FIELD:064x}"},
            {"keyId": "G" * 32},
            {"keyId": "0" * 32},
            {"d": "must-not-be-an-input"},
            {
                "x": "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296",
                "y": "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
            },
        )
        for change in cases:
            with self.subTest(change=change):
                document = copy.deepcopy(self.fixture)
                document["keys"][0].update(change)
                self.write(document)
                with self.assertRaises(ValueError):
                    generate(self.input, self.output)
                self.assertFalse(self.output.exists())

    def test_duplicate_and_unbounded_inputs(self):
        key = self.fixture["keys"][0]
        for keys in ([], [key, key], [key, {**key, "keyId": "ff" * 16}], [key] * 9):
            with self.subTest(entries=len(keys)):
                self.write({**self.fixture, "keys": keys})
                with self.assertRaises(ValueError):
                    generate(self.input, self.output)
        for raw in (b" " * 4097, b'{"keys":[],"keys":[]}'):
            self.input.write_bytes(raw)
            with self.assertRaises(ValueError):
                generate(self.input, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
