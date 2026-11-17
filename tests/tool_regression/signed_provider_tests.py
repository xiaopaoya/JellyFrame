"""No-device signed transport and binary-partition capacity qualification."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).parent))
import device_install_package as inspection
from device_provider_contract import parse_provider_jsonl
try:
    from signed_package_tests import fixture, signer, ec
except ImportError as error:
    if error.name != 'cryptography':
        raise
    fixture = signer = ec = None


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


PROVIDER_DIR = ROOT / 'ports/esp32s3-idf/tools/device_provider'
provider = module('signed_transport_provider', PROVIDER_DIR / 'jellyframe_device.py')
packager = module('signed_transport_release', PROVIDER_DIR / 'package_ws147_device_provider.py')


def signed(root, shaped=True, capacity_bytes=0):
    raw = fixture(root, shaped, capacity_bytes=capacity_bytes)
    return raw, signer.sign_bundle(raw, ec.derive_private_key(1, ec.SECP256R1()), bytes(range(16)))


@unittest.skipIf(ec is None, "optional cryptography package unavailable")
class PackageInspectionTests(unittest.TestCase):
    def test_signed_both_formats_and_legacy_scalar(self):
        with tempfile.TemporaryDirectory() as temp:
            for shaped in (False, True):
                raw, envelope = signed(Path(temp), shaped)
                facts = inspection.inspect_install_bytes(envelope, len(envelope), allow_shaped=shaped)
                self.assertEqual(facts, {'appId': 'org.jellyframe.signed-test', 'formatVersion': int(shaped),
                                        'signedEnvelope': True, 'signatureVerified': False, 'bytes': len(envelope)})
                if shaped:
                    for data, permitted in ((raw, True), (envelope, False)):
                        with self.assertRaisesRegex(ValueError, 'shaped install requires'):
                            inspection.inspect_install_bytes(data, len(data), allow_shaped=permitted)
                else:
                    self.assertFalse(inspection.inspect_install_bytes(raw, len(raw))['signedEnvelope'])

    def test_capacity_extensions_and_truncation(self):
        with tempfile.TemporaryDirectory() as temp:
            _, encoded = signed(Path(temp))
            for cap in (0, True, len(encoded) - 1, inspection.MAX_PACKAGE_BYTES + 1):
                with self.subTest(cap=cap), self.assertRaises(ValueError):
                    inspection.inspect_install_bytes(encoded, cap, allow_shaped=True)
            for length in (0, 7, 63, 127, len(encoded) - 1):
                with self.subTest(length=length), self.assertRaises(ValueError):
                    inspection.inspect_install_bytes(encoded[:length], len(encoded), allow_shaped=True)
            for suffix, data in (('.jfapp', encoded), ('.jfsg', encoded[-100:]), ('.bin', encoded)):
                path = Path(temp) / ('bad' + suffix)
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    inspection.read_install_package(path, len(encoded), allow_shaped=True)

    def test_manifest_header_digest_and_inner_corruption_refuse(self):
        with tempfile.TemporaryDirectory() as temp:
            _, encoded = signed(Path(temp))
            size = struct.unpack_from('<I', encoded, 16)[0]
            for at in (8, 10, 12, 14, 16, 20, 22, 40, 64, 70, 128 + size, len(encoded) - 1):
                changed = bytearray(encoded)
                changed[at] ^= 1
                with self.subTest(at=at), self.assertRaises(ValueError):
                    inspection.inspect_install_bytes(bytes(changed), len(changed), allow_shaped=True)
            # A signature mutation is deliberately NOT authenticated on the host.
            changed = bytearray(encoded)
            changed[64 + size] ^= 1
            self.assertFalse(inspection.inspect_install_bytes(bytes(changed), len(changed), allow_shaped=True)['signatureVerified'])

    def test_recomputed_outer_crc_cannot_hide_resource_corruption(self):
        with tempfile.TemporaryDirectory() as temp:
            raw, _ = signed(Path(temp), False)
            changed = bytearray(raw)
            changed[struct.unpack_from("<I", raw, 40)[0]] ^= 1
            struct.pack_into('<I', changed, 48, 0)
            struct.pack_into('<I', changed, 48, zlib.crc32(changed) & 0xffffffff)
            with self.assertRaisesRegex(ValueError, 'resource hash/checksum'):
                inspection.inspect_install_bytes(bytes(changed), len(changed))

    def test_empty_shaped_asset_refuses_before_transport(self):
        with tempfile.TemporaryDirectory() as temp:
            raw, _ = signed(Path(temp))
            changed = bytearray(raw)
            index = struct.unpack_from('<I', raw, 24)[0]
            # Sorted first resource is the shaped catalog, not the HTML entry.
            struct.pack_into('<II', changed, index + 16, 0, 0)
            struct.pack_into('<I', changed, 48, 0)
            struct.pack_into('<I', changed, 48, zlib.crc32(changed) & 0xffffffff)
            with self.assertRaisesRegex(ValueError, 'missing or duplicate shaped resource'):
                inspection.inspect_install_bytes(bytes(changed), len(changed), allow_shaped=True)

    def test_shaped_invalid_metadata_or_missing_resources_refuse(self):
        with tempfile.TemporaryDirectory() as temp:
            raw, _ = signed(Path(temp))
            at, length = struct.unpack_from('<II', raw, 16)
            summary = raw[at:at + length]
            for old, new in ((b'"weight":400', b'"weight":0  '),
                             (b'/font.jffont2', b'/nope.jffont2'),
                             (b'"versionCode":1', b'"versionCode":true')):
                self.assertIn(old, summary)
                changed_summary = summary.replace(old, new)
                # Reassemble using the packager so section bounds and CRC stay valid.
                header = list(inspection.HEADER.unpack_from(raw))
                delta = len(changed_summary) - len(summary)
                changed = bytearray(raw[:at] + changed_summary + raw[at + length:])
                header[5] = len(changed_summary)
                for field in (6, 8, 10):
                    header[field] += delta
                header[12] = 0
                inspection.HEADER.pack_into(changed, 0, *header)
                struct.pack_into('<I', changed, 48, zlib.crc32(changed) & 0xffffffff)
                with self.subTest(old=old), self.assertRaises(ValueError):
                    inspection.inspect_install_bytes(bytes(changed), len(changed), allow_shaped=True)


@unittest.skipIf(ec is None, "optional cryptography package unavailable")
class PhysicalTransportTests(unittest.TestCase):
    def exercise(self, raw, root, *, cap=1044480, shaping=True, replace=False, fail_commit=False):
        path = root / 'install.jfsg'
        path.write_bytes(raw)
        calls, reconstructed = [], bytearray()
        device = provider.fixture_device()
        device['capabilities']['maxBundleBytes'] = cap
        if shaping:
            device['capabilities']['featureFamilies'].append('text.shaping')
        state = {'transaction': 0, 'expected': 0}

        class Wire:
            def __init__(self, config): pass
            def open(self): pass
            def close(self): pass
            def request(self, kind, payload=b'', *args):
                calls.append(kind)
                if kind in (provider.DISCOVERY, provider.IDENTITY): return b'stub'
                code, flags = 1, 2
                if kind == provider.INSTALL_BEGIN:
                    version, downgrade, count, reserved, tx, expected, crc = struct.unpack_from('<BBBBIII', payload)
                    state.update(transaction=tx, expected=expected)
                    self_test.assertEqual(payload[16:].decode(), 'org.jellyframe.signed-test')
                    self_test.assertEqual((version, reserved, expected, crc), (1, 0, len(raw), zlib.crc32(raw) & 0xffffffff))
                    if replace: path.write_bytes(b'replaced after admission')
                elif kind == provider.INSTALL_CHUNK:
                    version, reserved, count, tx, offset = struct.unpack_from('<BBHII', payload)
                    self_test.assertEqual((version, reserved, tx, offset, count), (1, 0, state['transaction'], len(reconstructed), len(payload) - 12))
                    reconstructed.extend(payload[12:])
                elif kind == provider.INSTALL_COMMIT:
                    code, flags = (11, 0) if fail_commit else (0, 1)
                return struct.pack('<BBHIII', 1, code, flags, state['transaction'], len(reconstructed), state['expected'])

        self_test = self
        session = mock.Mock(cancel_requested=threading.Event())
        args = argparse.Namespace(operation='install', selector='fixture', request_id='signed-install', bundle=path, allow_downgrade=False)
        config = provider.ProviderConfig('fixture', 'COM19', 115200, root / 'manifest.json', {})
        with (mock.patch.object(provider, 'Wire', Wire),
              mock.patch.object(provider, 'decode_capabilities', return_value={}),
              mock.patch.object(provider, 'decode_identity', return_value={}),
              mock.patch.object(provider, 'device_from_capabilities', return_value=device),
              mock.patch.object(provider, 'LiveInstallSession', return_value=session)):
            try:
                result = provider.run_physical(args, config)
            except provider.ProviderError:
                self.assertNotIn(provider.INSTALL_BEGIN, calls)
                raise
        return result, bytes(reconstructed), calls

    def test_large_signed_exact_bytes_once_read_and_bounded_jsonl(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, raw = signed(root, capacity_bytes=900 * 1024)
            (code, events), received, calls = self.exercise(raw, root, replace=True)
            self.assertEqual((code, received), (0, raw))
            self.assertEqual(calls[-1], provider.INSTALL_COMMIT)
            text = '\n'.join(json.dumps(e) for e in events)
            self.assertLess(len(text.encode()), 256 * 1024)
            parsed = parse_provider_jsonl(text)
            self.assertEqual(parsed[-1]['resultCode'], 'ok')

    def test_target_capability_or_capacity_refusal_precedes_begin(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, raw = signed(root, capacity_bytes=350 * 1024)
            for cap, shaping in ((327680, True), (1044480, False)):
                with self.subTest(cap=cap), self.assertRaises(provider.ProviderError):
                    self.exercise(raw, root, cap=cap, shaping=shaping)

    def test_device_signature_refusal_remains_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, raw = signed(root)
            (code, events), received, _ = self.exercise(raw, root, fail_commit=True)
            self.assertEqual(received, raw)
            self.assertNotEqual(code, 0)
            self.assertEqual(events[-1]['resultCode'], 'integrity-failed')

    def test_feature_wire_bit_and_unknown_bit(self):
        encoded = (bytes((1, 3, 3, 3, 5, 40, 0, 0)) + struct.pack('<II', 4, 3 | 64) +
                   b'imgprover0.9.0' + b'a' * 40)
        self.assertIn('text.shaping', provider.decode_identity(encoded)['featureFamilies'])
        changed = bytearray(encoded)
        struct.pack_into('<I', changed, 12, 3 | 128)
        with self.assertRaises(provider.ProviderError): provider.decode_identity(changed)


class InstallCapacityTests(unittest.TestCase):
    def prepare(self, root, size=3 * 1024 * 1024, cap=1024):
        build = root / 'build'
        build.mkdir(exist_ok=True)
        (build / 'sdkconfig').write_text(f'CONFIG_PARTITION_TABLE_OFFSET=0x8000\nCONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB={cap}\n')
        config = build / 'config/sdkconfig.h'
        config.parent.mkdir(exist_ok=True)
        config.write_text(f'#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n#define CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB {cap}\n')
        table = (struct.pack('<HBBII16sI', 0x50aa, 0, 0, 0x10000, 8 * 1024 * 1024, b'factory', 0) +
                 struct.pack('<HBBII16sI', 0x50aa, 1, 2, 0xc10000, size, b'storage', 0))
        table += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(table).digest() + b'\xff' * 32
        path = build / 'partition_table/partition-table.bin'
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(table)
        app = root / 'app.bin'
        app.write_bytes(b'exact application')
        factory = root / 'factory.bin'
        with factory.open('wb') as out:
            out.truncate(16 * 1024 * 1024)
            out.seek(0x8000); out.write(table)
            out.seek(0x10000); out.write(app.read_bytes())
        return build, app, factory, path

    def test_config_and_physical_capacity_minimum(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for size, cap, expected in ((3145728, 1024, 1044480), (1048576, 1024, 344064), (3145728, 320, 327680)):
                build, app, factory, path = self.prepare(root, size, cap)
                actual, evidence = packager._install_capacity(build, app, factory)
                self.assertEqual(actual, expected)
                self.assertEqual(evidence['partitionTableSha256'], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_corrupt_table_config_or_recovery_image_refuses(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for case in ('md5', 'truncated', 'factory-table', 'factory-app', 'cap', 'missing-config', 'overlap', 'missing-md5', 'conflicting-config', 'stale-header'):
                build, app, factory, path = self.prepare(root)
                data = bytearray(path.read_bytes())
                if case == 'md5': data[80] ^= 1
                elif case == 'truncated': data.pop()
                elif case.startswith('factory-'):
                    with factory.open('r+b') as image:
                        image.seek(0x8000 if case == 'factory-table' else 0x10000); image.write(b'bad')
                elif case == 'cap': (build / 'sdkconfig').write_text('CONFIG_PARTITION_TABLE_OFFSET=0x8000\nCONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=4096\n')
                elif case == 'missing-config': (build / 'sdkconfig').write_text('')
                elif case == 'stale-header': (build / 'config/sdkconfig.h').write_text('#define CONFIG_PARTITION_TABLE_OFFSET 0x8000\n#define CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB 320\n')
                elif case == 'conflicting-config':
                    with (build / 'sdkconfig').open('a') as config: config.write('CONFIG_PARTITION_TABLE_OFFSET=0x9000\n')
                elif case == 'overlap':
                    struct.pack_into('<I', data, 32 + 4, 0x10000)
                    data[80:96] = hashlib.md5(data[:64]).digest()
                elif case == 'missing-md5': data = data[:64]
                path.write_bytes(data)
                with self.subTest(case=case), self.assertRaisesRegex(SystemExit, 'capacity evidence'):
                    packager._install_capacity(build, app, factory)


if __name__ == '__main__':
    unittest.main()
