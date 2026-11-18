#!/usr/bin/env python3
"""Build one pinned ABI9 Core profile and qualify its installed Runtime consumer."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from qualify_contextual_installed_packages import sha


RUNTIME = Path(__file__).resolve().parents[1]
JERRY_COMMIT = 'b7069350c2e52e7dc721dfb75f067147bd79b39b'
MBEDTLS_COMMIT = '72aa687352a469044cbb946f3fdb261430e41ce1'  # IDF 5.3.1 gitlink
CROP_FEATURES = ('CANVAS2D', 'MODERN_PAINT', 'FLEX_GRID', 'ADVANCED_FORMS', 'LENGTH_MATH')
CONTEXTUAL_FEATURES = ('CONTEXTUAL_TEXT', 'CONTEXTUAL_LAYOUT', 'SHAPED_PACKET_V2')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def check_commit(root, commit, clean=True):
    if git(root, 'rev-parse', '--show-toplevel').casefold() != root.as_posix().casefold():
        raise ValueError(f'expected a separate Git checkout at {root}')
    if git(root, 'rev-parse', 'HEAD') != commit or (clean and git(root, 'status', '--porcelain')):
        raise ValueError(f'{root} must be at {commit}' + (' with no working tree changes' if clean else ''))


def checked_ctest(run, label, build, required):
    inventory = json.loads(run(label + '-inventory', ['ctest', '--test-dir', build, '--show-only=json-v1']))
    tests = {test['name'] for test in inventory['tests']}
    if not required <= tests:
        raise RuntimeError(f'{label}: missing expected tests {sorted(required - tests)}')
    log = run(label, ['ctest', '--test-dir', build, '--output-on-failure', '--no-tests=error'])
    if 'Skipped' in log or 'Not Run' in log:
        raise RuntimeError(f'{label}: incomplete test execution')
    return len(tests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True, help='Clean Core checkout')
    parser.add_argument('--core-commit', required=True)
    parser.add_argument('--core-version', required=True)
    parser.add_argument('--core-source-hash', required=True)
    parser.add_argument('--profile', required=True, choices=('full', 'cropped', 'off'))
    parser.add_argument('--scalar-frame', type=int, choices=(10, 13), default=10,
                        help='Expected transport for the ordinary signed app (10 for typed mainline, 13 for the experiment)')
    parser.add_argument('--jerry', type=Path, required=True, help='Pinned and patched JerryScript built with VM halt')
    parser.add_argument('--mbedtls', type=Path, required=True, help='Pinned clean ESP-IDF mbedTLS checkout')
    parser.add_argument('--output', type=Path, required=True, help='New directory for build and evidence')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--c-compiler', default='clang')
    parser.add_argument('--cxx-compiler', default='clang++')
    args = parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{40}', args.core_commit) or
            not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', args.core_version) or
            not re.fullmatch('[0-9a-f]{64}', args.core_source_hash) or args.jobs < 1):
        parser.error('give a full commit, numeric Core version, lowercase SHA-256 source hash and positive job count')

    core, jerry, mbedtls = (path.resolve(strict=True) for path in (args.core, args.jerry, args.mbedtls))
    output = args.output.resolve()
    if any(output.is_relative_to(path) for path in (RUNTIME, core, jerry, mbedtls)):
        parser.error('output must be outside all source checkouts')
    check_commit(core, args.core_commit)
    check_commit(mbedtls, MBEDTLS_COMMIT)
    check_commit(jerry, JERRY_COMMIT, clean=False)
    runtime_commit = git(RUNTIME, 'rev-parse', 'HEAD')
    check_commit(RUNTIME, runtime_commit)
    production_lock = RUNTIME / 'cmake/jellyframe_dependency_lock.cmake'
    lock_hash = sha(production_lock)
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'
    logs.mkdir()
    lock = output / 'candidate-lock.cmake'
    lock.write_text(f'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "{args.core_version}")\n'
                    'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "9")\n'
                    f'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "{args.core_source_hash}")\n', encoding='utf-8')
    env = os.environ.copy()
    env.pop('JELLYFRAME_TEST_RENDER_CORE_VERSION', None)

    def run(label, command):
        command = list(map(str, command))
        path = logs / f'{label}.log'
        print(f'{args.profile}: {label}', flush=True)
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        path.write_bytes(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label} failed ({result.returncode}); see {path}')
        return result.stdout.decode('utf-8', errors='replace')

    configure = ['cmake', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 f'-DCMAKE_CXX_COMPILER={args.cxx_compiler}']
    profile_flags = [f'-DJELLYFRAME_ENABLE_{feature}={"OFF" if args.profile == "off" else "ON"}'
                     for feature in CONTEXTUAL_FEATURES]
    if args.profile == 'cropped':
        crop_features = CROP_FEATURES if args.scalar_frame == 13 else CROP_FEATURES[:-1]
        profile_flags += [f'-DJELLYFRAME_ENABLE_{feature}=OFF' for feature in crop_features]
    core_build = output / 'core-build'
    install = output / 'core-install'
    run('core-configure', [*configure, '-S', core, '-B', core_build, '-DJELLYFRAME_BUILD_TESTS=OFF',
                           '-DJELLYFRAME_BUILD_BENCHMARKS=OFF', '-DJELLYFRAME_INSTALL_RENDER_CORE=ON', *profile_flags])
    manifest = json.loads((core_build / 'generated/jellyframe_render_core_source_manifest.json').read_text())
    profile = json.loads((core_build / 'generated/jellyframe_render_core_profile.json').read_text())
    if ((profile['packageVersion'], profile['engineAbi'], manifest['sourceHash']) !=
            (args.core_version, 9, args.core_source_hash)):
        raise ValueError('Core checkout metadata differs from the supplied candidate identity')
    run('core-build', ['cmake', '--build', core_build, '--parallel', args.jobs])
    run('core-install', ['cmake', '--install', core_build, '--prefix', install])
    fixtures = core / 'tests/fixtures'
    installed = output / 'installed-evidence'
    run('runtime-installed', [sys.executable, RUNTIME / 'project_tools/qualify_contextual_installed_packages.py',
                              '--package', f'{args.profile}={install}', '--qualification-lock', lock,
                              '--fixtures', fixtures, '--jerry', jerry, '--output', installed,
                              '--scalar-frame', args.scalar_frame,
                              '--build-root', output / 'runtime-builds', '--compiler', args.cxx_compiler,
                              '--jobs', args.jobs])
    route_tests = {}
    routes = {'signed_scalar': {f'jfsg_scalar_installed_frame{args.scalar_frame}'}}
    if args.profile != 'off':
        routes['signed_shaped'] = {'jfsg_shaped_pipeline', 'jfsg_shaped_reboot',
                                   'jfsg_shaped_provider_transport', 'jfsg_vm_fatal_boundary'}
    for route, required in routes.items():
        build = output / route
        parameters = [f'-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR={install.as_posix()}',
                      f'-DJELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE={lock.as_posix()}',
                      f'-DJFSG_MBEDTLS_SOURCE_DIR={mbedtls.as_posix()}', f'-DJERRYSCRIPT_ROOT={jerry.as_posix()}',
                      f'-DCMAKE_C_COMPILER={args.c_compiler}']
        if route == 'signed_shaped':
            parameters.append(f'-DJFSG_CORE_FIXTURES_DIR={fixtures.as_posix()}')
        else:
            parameters.append(f'-DJFSG_EXPECT_SCALAR_FRAME={args.scalar_frame}')
        run(route + '-configure', [*configure, '-S', RUNTIME / 'ports/esp32s3-idf/tests' / route, '-B', build,
            '-DJELLYFRAME_RENDER_CORE_PROVIDER=package', *parameters, *profile_flags])
        provenance = json.loads((build / 'runtime/generated/jellyframe_render_core_provenance.json').read_text())
        if (provenance['provider'], provenance['lockKind'], provenance['lockEnforced'], provenance['sourceHash'],
                provenance['lockFileSha256']) != ('package', 'qualification', True, args.core_source_hash, sha(lock)):
            raise ValueError(f'{route}: selected Core package/lock is inconsistent')
        run(route + '-build', ['cmake', '--build', build, '--parallel', args.jobs])
        route_tests[route] = checked_ctest(run, route + '-ctest', build, required)

    desktop_build = output / 'desktop-consumer-build'
    desktop_parameters = [f'-DJELLYFRAME_RENDER_CORE_PROVIDER=package',
                          f'-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR={install.as_posix()}',
                          f'-DJELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE={lock.as_posix()}',
                          f'-DJERRYSCRIPT_ROOT={jerry.as_posix()}',
                          '-DJELLYFRAME_BUILD_APP_RUNTIME=ON', '-DJELLYFRAME_BUILD_SCRIPTING=ON',
                          '-DJELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON', '-DJELLYFRAME_BUILD_EXAMPLES=ON',
                          '-DJELLYFRAME_BUILD_BENCHMARKS=OFF',
                          '-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=ON', *profile_flags]
    run('desktop-consumer-configure', [*configure, '-S', RUNTIME, '-B', desktop_build,
        f'-DCMAKE_C_COMPILER={args.c_compiler}', *desktop_parameters])
    desktop_provenance = json.loads((desktop_build / 'generated/jellyframe_render_core_provenance.json').read_text())
    if (desktop_provenance['provider'], desktop_provenance['lockKind'], desktop_provenance['lockEnforced'],
            desktop_provenance['sourceHash'], desktop_provenance['lockFileSha256']) != (
            'package', 'qualification', True, args.core_source_hash, sha(lock)):
        raise ValueError('desktop consumers did not select the exact qualified Core package')
    run('desktop-consumer-build', ['cmake', '--build', desktop_build, '--target',
        'jellyframe_desktop_shell', 'jellyframe_pseudo_browser', '--parallel', args.jobs])
    desktop_inventory = json.loads(run('desktop-consumer-inventory',
        ['ctest', '--test-dir', desktop_build, '--show-only=json-v1']))
    if 'jellyframe_native_core_text_route' not in {test['name'] for test in desktop_inventory['tests']}:
        raise RuntimeError('desktop consumer build is missing jellyframe_native_core_text_route')
    desktop_test_log = run('desktop-consumer-route-test', ['ctest', '--test-dir', desktop_build,
        '-R', '^jellyframe_native_core_text_route$', '--output-on-failure', '--no-tests=error'])
    if 'Skipped' in desktop_test_log or 'Not Run' in desktop_test_log:
        raise RuntimeError('desktop consumer route test did not complete')

    check_commit(core, args.core_commit)
    check_commit(RUNTIME, runtime_commit)
    if lock_hash != sha(production_lock):
        raise RuntimeError('production lock changed during candidate qualification')
    test_result, = json.loads((installed / 'result.json').read_text())
    evidence = {'schemaVersion': 1,
                'scope': 'installed Runtime, signed host tests and Win32 desktop consumers; no target execution',
                'coreCommit': args.core_commit, 'runtimeCommit': runtime_commit, 'profile': args.profile,
                'scalarFrame': args.scalar_frame,
                'candidateLockSha256': sha(lock), 'productionLockSha256': lock_hash,
                'jerryCommit': JERRY_COMMIT, 'mbedtlsCommit': MBEDTLS_COMMIT,
                'installed': test_result, 'signedTestsPassed': route_tests,
                'desktopConsumers': {'provenance': desktop_provenance,
                                    'targets': ['jellyframe_desktop_shell', 'jellyframe_pseudo_browser'],
                                    'routeTest': 'jellyframe_native_core_text_route'}}
    (output / 'receipt.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(f'{args.profile}: {test_result["passed"]} installed tests, {route_tests}; {output / "receipt.json"}')


if __name__ == '__main__':
    main()
