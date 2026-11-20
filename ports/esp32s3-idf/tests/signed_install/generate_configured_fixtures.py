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


def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    trusted = ec.generate_private_key(ec.SECP256R1())
    untrusted = ec.generate_private_key(ec.SECP256R1())
    key_id = b"configured-store"
    point = trusted.public_key().public_numbers()
    manifest = {
        "format": "jellyframe.jfsg-public-trust",
        "formatVersion": 1,
        "keys": [{"keyId": key_id.hex(), "x": f"{point.x:064x}", "y": f"{point.y:064x}"}],
    }
    with TemporaryDirectory(dir=destination) as scratch:
        prior = fixture(Path(scratch), version=1)
        update = fixture(Path(scratch), version=2)
    for name, bundle, key in (
        ("trusted-v1", prior, trusted),
        ("trusted-v2", update, trusted),
        ("untrusted-v2", update, untrusted),
    ):
        (destination / f"{name}.jfsg").write_bytes(sign_app_package.sign_bundle(bundle, key, key_id))
    (destination / "public-trust.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
