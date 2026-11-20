#!/usr/bin/env python3
"""Generate the ESP32-S3 JFSG verifier table from reviewed public keys."""

import argparse
import hashlib
import json
from pathlib import Path
import re


FORMAT = "jellyframe.jfsg-public-trust"
FIELD = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
CURVE_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
# The first two points have public private scalars in the JFSG test corpus.
# Reject either sign of the point, since the negated scalar is also public.
TEST_COORDINATES_X = {
    0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    0x7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC47669978,
}


def unique_members(pairs):
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError("duplicate JSON member in public trust manifest")
    return result


def hex_field(value, length):
    if not isinstance(value, str) or not re.fullmatch(rf"[0-9a-f]{{{length}}}", value):
        raise ValueError("public key identifiers and coordinates must be fixed-width lowercase hex")
    return value


def read_keys(path):
    with path.open("rb") as source:
        data = source.read(4097)
    if len(data) > 4096:
        raise ValueError("public trust manifest exceeds 4096 bytes")
    manifest = json.loads(data.decode("utf-8"), object_pairs_hook=unique_members)
    if (not isinstance(manifest, dict) or set(manifest) != {"format", "formatVersion", "keys"} or
            manifest["format"] != FORMAT or type(manifest["formatVersion"]) is not int or
            manifest["formatVersion"] != 1):
        raise ValueError("expected a version 1 public trust manifest")
    keys = manifest["keys"]
    if not isinstance(keys, list) or not 1 <= len(keys) <= 8:
        raise ValueError("an explicit public trust manifest must name 1 to 8 keys")
    ids = set()
    points = set()
    for key in keys:
        if not isinstance(key, dict) or set(key) != {"keyId", "x", "y"}:
            raise ValueError("each key must contain only keyId and the public x and y coordinates")
        key_id = hex_field(key["keyId"], 32)
        if key_id == "0" * 32:
            raise ValueError("public key identifier must be nonzero")
        x = int(hex_field(key["x"], 64), 16)
        y = int(hex_field(key["y"], 64), 16)
        if x >= FIELD or y >= FIELD or (y * y - x * x * x + 3 * x - CURVE_B) % FIELD:
            raise ValueError("public key is not a point on P-256")
        if x in TEST_COORDINATES_X:
            raise ValueError("published test scalars cannot be used for firmware trust")
        if key_id in ids or (x, y) in points:
            raise ValueError("public key identifiers and points must be unique")
        ids.add(key_id)
        points.add((x, y))
    return sorted(keys, key=lambda key: key["keyId"])


def cpp_bytes(hexadecimal):
    return "{" + ", ".join(f"0x{value:02x}" for value in bytes.fromhex(hexadecimal)) + "}"


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def generate(manifest_path, output):
    keys = read_keys(manifest_path) if manifest_path is not None else []
    declarations = [f"constexpr std::array<PublicKey, {len(keys)}> kProductionKeys{{{{"]
    for key in keys:
        values = ", ".join(cpp_bytes(key[field]) for field in ("keyId", "x", "y"))
        declarations.append(f"    PublicKey{{{values}}},")
    declarations.append("}};")
    header = ("\n".join(declarations) + "\n").encode("ascii")
    canonical = json.dumps({"format": FORMAT, "formatVersion": 1, "keys": keys},
                           sort_keys=True, separators=(",", ":")).encode("ascii")
    receipt = {
        "format": "jellyframe.jfsg-public-trust-build",
        "formatVersion": 1,
        "trustSha256": hashlib.sha256(canonical).hexdigest(),
        "headerSha256": hashlib.sha256(header).hexdigest(),
        "keyIds": [key["keyId"] for key in keys],
    }
    output.mkdir(parents=True, exist_ok=True)
    write_if_changed(output / "jfsg_trusted_public_keys.inc", header)
    write_if_changed(output / "jfsg_package_trust_receipt.json",
                     (json.dumps(receipt, indent=2) + "\n").encode("ascii"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.manifest, args.output_dir)
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        parser.exit(1, f"JFSG public trust configuration failed: {error}\n")


if __name__ == "__main__":
    main()
