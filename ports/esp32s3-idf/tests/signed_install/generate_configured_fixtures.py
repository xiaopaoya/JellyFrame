"""Create configured-store fixtures without writing either private key."""

import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from cryptography.hazmat.primitives.asymmetric import ec

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / "tests/tool_regression"))
from signed_package_tests import fixture
import sign_app_package


def write_manifest(path, signers):
    keys = []
    for key_id, key in signers:
        point = key.public_key().public_numbers()
        keys.append({"keyId": key_id.hex(), "x": f"{point.x:064x}", "y": f"{point.y:064x}"})
    manifest = {
        "format": "jellyframe.jfsg-public-trust",
        "formatVersion": 1,
        "keys": keys,
    }
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")


def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    trusted = ec.generate_private_key(ec.SECP256R1())
    next_key = ec.generate_private_key(ec.SECP256R1())
    key_id = b"configured-store"
    rotated_id = b"configured-next!"
    with TemporaryDirectory(dir=destination) as scratch:
        prior = fixture(Path(scratch), version=1)
        update = fixture(Path(scratch), version=2)
        rotation_update = fixture(Path(scratch), shaped=True, version=2)
    for name, bundle, key, signer_id in (
        ("trusted-v1", prior, trusted, key_id),
        ("trusted-v2", update, trusted, key_id),
        ("untrusted-v2", update, next_key, key_id),
        ("rotation-old-v2", rotation_update, trusted, key_id),
        ("rotation-new-v2", rotation_update, next_key, rotated_id),
    ):
        (destination / f"{name}.jfsg").write_bytes(sign_app_package.sign_bundle(bundle, key, signer_id))
    old = (key_id, trusted)
    new = (rotated_id, next_key)
    write_manifest(destination / "public-trust.json", [old])
    write_manifest(destination / "overlap-trust.json", [old, new])
    write_manifest(destination / "rotated-trust.json", [new])


if __name__ == "__main__":
    main()
