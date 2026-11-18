"""The separately compiled production adapter must not expose test injection."""
import subprocess
import sys

symbols = subprocess.check_output([sys.argv[1], '-C', *sys.argv[2:]], text=True)
assert any(' T ' in line and 'jellyframe_esp32s3::verify_device_package_signature(' in line
           for line in symbols.splitlines())
assert 'verify_device_package_signature_with_test_key' not in symbols
assert 'verify_host_fixture_signature' not in symbols
print('production callback present; both test routers absent')
