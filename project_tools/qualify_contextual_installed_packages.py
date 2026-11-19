#!/usr/bin/env python3
"""Qualify public ABI9 installed packages against the active Runtime checkout.

The explicit candidate lock is used only by CMake's package consumer.
Production tooling retains its unchanged production lock/manifests. This is
not product-route, Desktop, signed-store or device qualification.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import traceback


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_ctest_passed_count(log):
    legacy = re.search(r'0 tests failed out of (\d+)', log)
    if legacy:
        return int(legacy[1])

    current = re.search(r'(\d+)% tests passed(?:,\s*|\s+\()(\d+) tests failed out of (\d+)\)?', log)
    if current:
        percent, failed, total = map(int, current.groups())
        if percent == 100 and failed == 0:
            return total
        raise RuntimeError(f'CTest reported {failed} failures out of {total} tests')

    passed_only = re.search(r'(\d+)% tests passed out of (\d+)', log)
    if passed_only:
        percent, total = map(int, passed_only.groups())
        if percent == 100:
            return total
        raise RuntimeError(f'CTest reported {percent}% passed out of {total} tests')

    raise RuntimeError('CTest output has no recognized final summary')


def test_assertions_enabled(command):
    return re.search(r'(?<!\S)(?:-UNDEBUG|/UNDEBUG)(?=\s|$)', command) is not None

def audit_installed_package(build, runtime, probe, prefix, identity, production_lock_sha256):
    provenance = json.loads((build / 'runtime/generated/jellyframe_render_core_provenance.json').read_text())
    assert provenance['engineAbi'] == 9 and provenance['lockEnforced'], 'package lock not enforced'
    assert provenance['lockKind'] == 'qualification', 'qualification lock was not selected'
    assert provenance['sourceHash'] == identity['sourceHash'], 'package source mismatch'
    commands = json.loads((build / 'compile_commands.json').read_text())
    cpp = [row for row in commands if Path(row['file']).suffix in ('.cpp', '.cc', '.cxx')]
    # JerryScript's selected external C implementation is expected; Core
    # C++ source is not. Only the generated route probe is outside the checkout.
    assert cpp and all(Path(row['file']).is_relative_to(runtime) or
                       Path(row['file']) == probe for row in cpp), 'external Core C++ source'
    assert not any('/src/render_core/' in row['file'].replace('\\', '/') for row in cpp), 'bundled Core source fallback'
    tests = [row for row in cpp if '/tests/' in row['file'].replace('\\', '/') or
             row['file'].endswith('qualification_route_probe.cpp')]
    assert tests, 'no consumer test compilation records'
    always_on = []
    for row in tests:
        if test_assertions_enabled(row['command']):
            continue
        # This existing independent timing test uses explicit throws throughout,
        # rather than assert. Audit that one exception instead of treating
        # -DNDEBUG as proof that its always-on checks have been removed.
        path = Path(row['file'])
        assert path == runtime / 'tests/cpu2d_sampling_tests.cpp', 'test assertions disabled: ' + str(path)
        text = path.read_text()
        assert not re.search(r'\bassert\s*\(|#\s*include\s*<cassert>', text), 'timing test gained disabled assertions'
        assert 'throw std::runtime_error(' in text, 'timing test lost always-on checks'
        always_on.append(str(path.relative_to(runtime)))
    ninja = (build / 'build.ninja').read_text().replace('$:', ':').replace('\\', '/')
    archive = prefix / 'lib/jellyframe_render_core.lib'
    assert archive.as_posix() in ninja, 'installed archive not linked'
    assert sha(archive) == identity['archiveSha256'], 'installed archive changed'
    assert sha(runtime / 'cmake/jellyframe_dependency_lock.cmake') == production_lock_sha256, 'production tooling lock changed'
    return {'provenance': provenance, 'runtimeCompileUnits': len(commands),
            'assertionsEnabled': True, 'alwaysOnThrowTests': always_on,
            'coreSourceFallback': False, 'productionToolingLockUnchanged': True}


def write_qualification_harness(harness):
    # The harness adds only the route probe. Runtime source-contract tests retain
    # their real Git checkout and production metadata; there is no source copy.
    probe = harness / 'qualification_route_probe.cpp'
    probe.write_text('''#include "app_runtime/runtime_shaped_text_binding.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_prepared_text.h"
#include <cassert>
static_assert(JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API == QUALIFICATION_CONTEXTUAL);
static_assert(JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API == QUALIFICATION_CONTEXTUAL);
static_assert(JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API == (QUALIFICATION_SCALAR_FRAME == 10));
#if QUALIFICATION_CONTEXTUAL && !defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#error "installed positive corpus requires explicit fixtures"
#endif
int main() {
    assert(jellyframe::script_task_app_frame_codec_supports_version(12) == bool(QUALIFICATION_CONTEXTUAL));
    assert(!jellyframe::script_task_app_frame_codec_supports_version(11));
    assert(jellyframe::script_task_app_frame_codec_supports_version(10) == (QUALIFICATION_SCALAR_FRAME == 10));
    assert(jellyframe::script_task_app_frame_codec_supports_version(13) == (QUALIFICATION_SCALAR_FRAME == 13));
}
''')
    with (harness / 'CMakeLists.txt').open('w') as cmake:
        cmake.write('''
cmake_minimum_required(VERSION 3.16)
project(JellyFrameInstalledQualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_subdirectory("${QUALIFICATION_RUNTIME_SOURCE}" runtime)
# Isolated qualification only: no replacement Core source or private headers.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
add_executable(jellyframe_installed_route_probe qualification_route_probe.cpp)
target_link_libraries(jellyframe_installed_route_probe PRIVATE jellyframe_script_task_runtime)
target_include_directories(jellyframe_installed_route_probe BEFORE PRIVATE
    "${JELLYFRAME_RENDER_CORE_PACKAGE_DIR}/include")
target_compile_definitions(jellyframe_installed_route_probe PRIVATE
    QUALIFICATION_CONTEXTUAL=${QUALIFICATION_CONTEXTUAL} QUALIFICATION_SCALAR_FRAME=${QUALIFICATION_SCALAR_FRAME})
jellyframe_enable_shaped_test_fixtures(jellyframe_installed_route_probe)
jellyframe_enable_test_asserts(jellyframe_installed_route_probe)
add_test(NAME jellyframe_installed_route_probe COMMAND jellyframe_installed_route_probe)
''')
    return probe


def main():
    if not __debug__:
        raise RuntimeError('qualification requires Python assertions; do not use -O')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', action='append', required=True,
                        help='PROFILE=installed-prefix; profiles full, cropped, off')
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--jerry', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--qualification-lock', type=Path, required=True,
                        help='reviewed exact candidate CMake lock, independent of installed packages')
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--scalar-frame', type=int, choices=(10, 13), default=13)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    runtime = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(runtime / 'tools'))
    from bootstrap_render_core import read_lock
    qualification_lock = args.qualification_lock.resolve(strict=True)
    expected = read_lock(qualification_lock)
    assert expected['abi'] == '9', 'this qualifier requires ABI9 Core'
    production_lock_sha256 = sha(runtime / 'cmake/jellyframe_dependency_lock.cmake')
    output = args.output.resolve()
    builds = args.build_root.resolve()
    packages = {}
    identities = {}
    for item in args.package:
        name, separator, value = item.partition('=')
        if not separator or name not in ('full', 'cropped', 'off') or name in packages:
            parser.error('each --package must name a distinct full/cropped/off profile')
        prefix = Path(value).resolve()
        share = prefix / 'share/jellyframe-render-core'
        profile = json.loads((share / 'jellyframe_render_core_profile.json').read_text())
        manifest = json.loads((share / 'jellyframe_render_core_source_manifest.json').read_text())
        assert profile['engineAbi'] == 9 and profile['packageVersion'] == expected['version']
        features = set(profile['features'])
        assert {'core.document', 'core.paint'}.issubset(features)
        contextual = {'text.contextual-jfsp2', 'text.contextual-layout'}
        assert bool(features & contextual) == (name != 'off')
        assert name == 'off' or contextual.issubset(features)
        optional = {'graphics.canvas2d', 'css.modern-paint', 'css.flex-grid', 'forms.advanced'}
        # The experimental implementation also makes length math optional.
        if args.scalar_frame == 13:
            optional.add('css.length-math')
        assert (optional.issubset(features) if name != 'cropped' else not (optional & features))
        joined = ''.join(row['path'] + '\t' + row['sha256'] + '\n'
                         for row in manifest['files']).encode()
        assert hashlib.sha256(joined).hexdigest() == manifest['sourceHash']
        assert manifest['sourceFileCount'] == len(manifest['files'])
        assert re.fullmatch('[0-9a-f]{64}', manifest['sourceHash'])
        assert manifest['sourceHash'] == expected['source_hash'], 'package source differs from reviewed qualification lock'
        packages[name] = prefix
        identities[name] = {'version': profile['packageVersion'], 'abi': profile['engineAbi'],
                            'sourceHash': manifest['sourceHash'], 'features': sorted(features),
                            'sourceFileCount': manifest['sourceFileCount'],
                            'archiveSha256': sha(prefix / 'lib/jellyframe_render_core.lib')}
    assert len({row['sourceHash'] for row in identities.values()}) == 1
    output.mkdir(parents=True, exist_ok=False)
    builds.mkdir(parents=True, exist_ok=False)
    harness = output / 'cmake-harness'
    harness.mkdir()

    def run(label, command, cwd=None):
        with (output / (label + '.log')).open('w', encoding='utf-8') as log:
            log.write(json.dumps([str(arg) for arg in command]) + '\n')
            log.flush()
            result = subprocess.run([str(arg) for arg in command], cwd=cwd,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{label} failed ({result.returncode}); see saved log')

    names = subprocess.check_output(['git', '-C', str(runtime), 'ls-files',
                                    '--cached', '--others', '--exclude-standard', '-z']).decode().split('\0')
    inputs = []
    for name in sorted(set(names) - {''}):
        source = runtime / name
        if not source.is_file():
            continue
        inputs.append({'path': name, 'sha256': sha(source)})
    (output / 'runtime-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    revision = subprocess.check_output(['git', '-C', str(runtime), 'rev-parse', 'HEAD'], text=True).strip()
    (output / 'runtime-source.json').write_text(json.dumps({
        'path': str(runtime), 'revision': revision,
        'qualificationLock': str(qualification_lock), 'qualificationLockSha256': sha(qualification_lock),
        'productionLockSha256': production_lock_sha256,
    }, indent=2) + '\n')

    probe = write_qualification_harness(harness)
    (output / 'qualification-only-edits.json').write_text(json.dumps({
        'changed': [],
        'added': ['cmake-harness/CMakeLists.txt', 'cmake-harness/qualification_route_probe.cpp'],
        'sourceTestsModified': False, 'productionLockModified': False,
        'scope': 'All configured installed Runtime tests; source tool tests keep production manifests. '
                 'Examples, benchmarks, sample/Desktop suites and signed execution are not included.'
    }, indent=2) + '\n')

    def qualify(name):
        build = builds / name
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER=' + args.compiler,
                  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                  '-DQUALIFICATION_RUNTIME_SOURCE=' + runtime.as_posix(),
                  '-DJELLYFRAME_RENDER_CORE_PROVIDER=package',
                  '-DJELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE=' + qualification_lock.as_posix(),
                  '-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=' + str(packages[name]),
                  '-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF',
                  '-DJELLYFRAME_CONTEXTUAL_TEST_FIXTURES_ROOT=' + str(args.fixtures.resolve()),
                  '-DJELLYFRAME_BUILD_SCRIPTING=ON', '-DJELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON',
                  '-DJELLYFRAME_BUILD_EXAMPLES=OFF', '-DJELLYFRAME_BUILD_BENCHMARKS=OFF',
                  '-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF',
                  '-DJERRYSCRIPT_ROOT=' + str(args.jerry.resolve()),
                  '-DQUALIFICATION_SCALAR_FRAME=' + str(args.scalar_frame),
                  '-DQUALIFICATION_CONTEXTUAL=' + ('0' if name == 'off' else '1')]
        if name == 'cropped':
            common += ['-DJELLYFRAME_ENABLE_' + feature + '=OFF' for feature in
                       ('CANVAS2D', 'MODERN_PAINT', 'FLEX_GRID', 'ADVANCED_FORMS', 'LENGTH_MATH')]
        run(name + '-configure', [args.cmake, '-S', harness, '-B', build] + common)
        run(name + '-build', [args.cmake, '--build', build, '--parallel', str(args.jobs)])
        run(name + '-ctest', [args.ctest, '--test-dir', build, '--output-on-failure'])
        log = (output / (name + '-ctest.log')).read_text()
        total = parse_ctest_passed_count(log)
        assert total >= 36 and 'Skipped' not in log
        audit = audit_installed_package(build, runtime, probe, packages[name], identities[name],
                                        production_lock_sha256)
        return {'profile': name, 'scalarFrame': args.scalar_frame, 'passed': total, 'skipped': 0, 'failed': 0,
                'identity': identities[name], **audit}

    results = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = {name: pool.submit(qualify, name) for name in packages}
        for name, future in futures.items():
            try:
                results.append(future.result())
            except Exception as error:
                (output / (name + '-failure.txt')).write_text(traceback.format_exc())
                results.append({'profile': name, 'error': str(error), 'type': type(error).__name__})
    (output / 'result.json').write_text(json.dumps(results, indent=2) + '\n')
    # Report concurrent edits explicitly without treating unrelated shared-worktree
    # changes as changes to compiled Runtime code.
    changed_inputs = [row['path'] for row in inputs
                      if not (runtime / row['path']).is_file() or sha(runtime / row['path']) != row['sha256']]
    (output / 'runtime-input-changes.json').write_text(json.dumps(changed_inputs, indent=2) + '\n')
    print(json.dumps(results, indent=2))
    return int(bool(changed_inputs) or any('error' in row for row in results))


if __name__ == '__main__':
    raise SystemExit(main())

