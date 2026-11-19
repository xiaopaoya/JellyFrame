#!/usr/bin/env python3
"""Check installed qualification summaries and exact candidate selection."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'project_tools'))
from qualify_contextual_installed_packages import (
    DEFAULT_CORE_ABI, check_qualification_provenance, inspect_package,
    parse_ctest_passed_count, positive_core_abi, test_assertions_enabled,
)


assert parse_ctest_passed_count('0 tests failed out of 41') == 41
assert parse_ctest_passed_count('100% tests passed, 0 tests failed out of 41') == 41
assert parse_ctest_passed_count('100% tests passed (0 tests failed out of 41)') == 41
assert parse_ctest_passed_count('100% tests passed out of 41') == 41
assert test_assertions_enabled('clang++ -UNDEBUG -o probe.cpp')
assert test_assertions_enabled('clang-cl.exe /UNDEBUG /Fo probe.obj')
assert not test_assertions_enabled('clang-cl.exe /DNDEBUG /Fo probe.obj')

try:
    parse_ctest_passed_count('95% tests passed, 2 tests failed out of 41')
except RuntimeError as error:
    assert '2 failures out of 41' in str(error)
else:
    raise AssertionError('failed CTest summary was accepted')

try:
    parse_ctest_passed_count('95% tests passed (2 tests failed out of 41)')
except RuntimeError as error:
    assert '2 failures out of 41' in str(error)
else:
    raise AssertionError('parenthesized failed CTest summary was accepted')

try:
    parse_ctest_passed_count('95% tests passed out of 41')
except RuntimeError as error:
    assert '95% passed out of 41' in str(error)
else:
    raise AssertionError('incomplete CTest summary was accepted')

try:
    parse_ctest_passed_count('Test project has no tests')
except RuntimeError as error:
    assert 'no recognized final summary' in str(error)
else:
    raise AssertionError('missing CTest summary was accepted')

def must_refuse(check, error_type, message):
    try:
        check()
    except error_type as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError('incorrect qualification input was accepted')


assert DEFAULT_CORE_ABI == 9
assert positive_core_abi('10') == 10
for invalid in ('0', '-1', '09', '9.5', '+9', ' 9'):
    must_refuse(lambda: positive_core_abi(invalid), argparse.ArgumentTypeError, 'positive decimal integer')

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    lock = root / 'reviewed-lock.cmake'
    prefix = root / 'pkg'
    share = prefix / 'share/jellyframe-render-core'
    share.mkdir(parents=True)
    archive = prefix / 'lib/jellyframe_render_core.lib'
    archive.parent.mkdir()
    archive.write_bytes(b'fixture library')
    source_files = [{'path': 'src/example.cpp', 'sha256': '1' * 64}]
    source_hash = hashlib.sha256(('src/example.cpp\t' + '1' * 64 + '\n').encode()).hexdigest()
    manifest = {'sourceHash': source_hash, 'sourceFileCount': 1, 'files': source_files}
    (share / 'jellyframe_render_core_source_manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    features = ['core.document', 'core.paint', 'graphics.canvas2d', 'css.modern-paint', 'css.flex-grid',
                'forms.advanced', 'text.contextual-jfsp2', 'text.contextual-layout']
    for abi, other_abi, version in ((9, 10, '0.10.1'), (10, 9, '0.11.0')):
        profile = {'engineAbi': abi, 'packageVersion': version, 'features': features}
        (share / 'jellyframe_render_core_profile.json').write_text(json.dumps(profile), encoding='utf-8')
        identity = {'version': version, 'abi': abi, 'sourceHash': source_hash}
        assert inspect_package(prefix, 'full', identity, 10)['abi'] == abi
        for field, wrong in (('abi', other_abi), ('version', '0.0.0'), ('sourceHash', '2' * 64)):
            must_refuse(lambda: inspect_package(prefix, 'full', {**identity, field: wrong}, 10),
                        ValueError, 'differs from the selected candidate')
        lock_digest = '3' * 64
        provenance = {'packageVersion': version, 'lockedVersion': version,
                      'engineAbi': abi, 'lockedEngineAbi': abi,
                      'sourceHash': source_hash, 'lockedSourceHash': source_hash,
                      'lockFileSha256': lock_digest, 'lockEnforced': True,
                      'provider': 'package', 'lockKind': 'qualification'}
        check_qualification_provenance(provenance, identity, lock_digest, 'probe')
        for field, wrong in (('engineAbi', str(abi)), ('lockedEngineAbi', other_abi),
                             ('lockedSourceHash', '4' * 64), ('lockFileSha256', '5' * 64),
                             ('lockKind', 'production'), ('lockEnforced', False), ('provider', 'source')):
            must_refuse(lambda: check_qualification_provenance(
                {**provenance, field: wrong}, identity, lock_digest, 'probe'), ValueError, field)

    lock.write_text('set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.11.0")\n'
                    'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "10")\n'
                    f'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "{source_hash}")\n', encoding='utf-8')
    installer = ROOT / 'project_tools/qualify_contextual_installed_packages.py'
    historical = subprocess.run([sys.executable, str(installer), '--qualification-lock', str(lock),
                                 '--package', f'full={prefix}', '--fixtures', str(root), '--jerry', str(root),
                                 '--output', str(root / 'evidence'), '--build-root', str(root / 'build')],
                                capture_output=True, text=True, check=False)
    assert historical.returncode == 2 and 'must agree with the selected qualification lock' in historical.stderr
    assert not (root / 'evidence').exists()

print('installed qualification summary and exact candidate tests passed')

