"""Generate real installable containers with public test key scalar1 only."""
import hashlib
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / 'tests/tool_regression'))
from signed_package_tests import fixture
from cryptography.hazmat.primitives.asymmetric import ec
import sign_app_package

destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
key = ec.derive_private_key(1, ec.SECP256R1())
for shaped, version, padding in ((False, 1, 0), (True, 2, 0), (True, 3, 900 * 1024)):
    bundle = fixture(destination, shaped, version, padding)
    package = sign_app_package.sign_bundle(bundle, key, bytes(range(0x10, 0x20)))
    (destination / f'v{version}.jfsg').write_bytes(package)
manifest = [{'path': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest(), 'bytes': p.stat().st_size}
            for p in sorted(destination.glob('*.jfsg'))]
(destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
