#!/usr/bin/env python3
"""Qualify public ABI9 installed packages in a new, isolated Runtime snapshot.

The candidate lock is used only by the snapshot's CMake package consumer.
Production tooling retains its unchanged production lock/manifests. This is
not product-route, Desktop, signed-store or device qualification.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import traceback


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_installed_package(build, snapshot, prefix, identity, production_lock):
    provenance = json.loads((build / 'generated/jellyframe_render_core_provenance.json').read_text())
    assert provenance['engineAbi'] == 9 and provenance['lockEnforced'], 'package lock not enforced'
    assert provenance['sourceHash'] == identity['sourceHash'], 'package source mismatch'
    commands = json.loads((build / 'compile_commands.json').read_text())
    cpp = [row for row in commands if Path(row['file']).suffix in ('.cpp', '.cc', '.cxx')]
    # JerryScript's selected external C implementation is expected; Core
    # C++ source is not. Every consumer C++ unit must be from the snapshot.
    assert cpp and all(Path(row['file']).is_relative_to(snapshot) for row in cpp), 'external Core C++ source'
    assert not any('/render_core/' in row['file'].replace('\\', '/') for row in cpp), 'bundled Core source fallback'
    tests = [row for row in cpp if '/tests/' in row['file'].replace('\\', '/') or
             row['file'].endswith('qualification_route_probe.cpp')]
    assert tests, 'no consumer test compilation records'
    always_on = []
    for row in tests:
        if '-UNDEBUG' in row['command']:
            continue
        # This existing independent timing test uses explicit throws throughout,
        # rather than assert. Audit that one exception instead of treating
        # -DNDEBUG as proof that its always-on checks have been removed.
        path = Path(row['file'])
        assert path == snapshot / 'tests/cpu2d_sampling_tests.cpp', 'test assertions disabled: ' + str(path)
        text = path.read_text()
        assert not re.search(r'\bassert\s*\(|#\s*include\s*<cassert>', text), 'timing test gained disabled assertions'
        assert 'throw std::runtime_error(' in text, 'timing test lost always-on checks'
        always_on.append(str(path.relative_to(snapshot)))
    ninja = (build / 'build.ninja').read_text().replace('$:', ':').replace('\\', '/')
    archive = prefix / 'lib/jellyframe_render_core.lib'
    assert archive.as_posix() in ninja, 'installed archive not linked'
    assert sha(archive) == identity['archiveSha256'], 'installed archive changed'
    assert sha(snapshot / 'cmake/jellyframe_dependency_lock.cmake') == sha(production_lock), 'production tooling lock changed'
    return {'provenance': provenance, 'runtimeCompileUnits': len(commands),
            'assertionsEnabled': True, 'alwaysOnThrowTests': always_on,
            'coreSourceFallback': False, 'productionToolingLockUnchanged': True}


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
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    args = parser.parse_args()
    runtime = Path(__file__).resolve().parents[1]
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
        assert profile['engineAbi'] == 9 and profile['packageVersion'] == '0.6.2'
        features = set(profile['features'])
        assert {'core.document', 'core.paint'}.issubset(features)
        contextual = {'text.contextual-jfsp2', 'text.contextual-layout'}
        assert bool(features & contextual) == (name != 'off')
        assert name == 'off' or contextual.issubset(features)
        optional = {'graphics.canvas2d', 'css.modern-paint', 'css.flex-grid',
                    'forms.advanced', 'css.length-math'}
        assert (optional.issubset(features) if name != 'cropped' else not (optional & features))
        joined = ''.join(row['path'] + '\t' + row['sha256'] + '\n'
                         for row in manifest['files']).encode()
        assert hashlib.sha256(joined).hexdigest() == manifest['sourceHash']
        assert manifest['sourceFileCount'] == len(manifest['files'])
        assert re.fullmatch('[0-9a-f]{64}', manifest['sourceHash'])
        packages[name] = prefix
        identities[name] = {'version': profile['packageVersion'], 'abi': profile['engineAbi'],
                            'sourceHash': manifest['sourceHash'], 'features': sorted(features),
                            'sourceFileCount': manifest['sourceFileCount'],
                            'archiveSha256': sha(prefix / 'lib/jellyframe_render_core.lib')}
    assert len({row['sourceHash'] for row in identities.values()}) == 1
    output.mkdir(parents=True, exist_ok=False)
    builds.mkdir(parents=True, exist_ok=False)
    snapshot = output / 'runtime-source'

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
        destination = snapshot / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        inputs.append({'path': name, 'sha256': sha(source)})
    (output / 'runtime-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    run('snapshot-git-init', ['git', 'init', '--quiet', snapshot])
    paths = output / 'markdown-paths.nul'
    paths.write_bytes(b''.join(row['path'].encode() + b'\0' for row in inputs
                              if row['path'].endswith('.md')))
    run('snapshot-doc-index', ['git', '-c', 'core.autocrlf=false', 'add', '--force',
        '--pathspec-from-file=' + str(paths), '--pathspec-file-nul'], cwd=snapshot)

    # Separate CMake candidate ABI qualification from the source tools' released
    # manifest policy. Both locks remain exact; no version/hash checks bypassed.
    provider = snapshot / 'cmake/jellyframe_render_core_provider.cmake'
    text = provider.read_text()
    before = 'include("${_jellyframe_runtime_cmake_dir}/jellyframe_dependency_lock.cmake")'
    after = 'include("${_jellyframe_runtime_cmake_dir}/qualification_candidate_lock.cmake")'
    assert text.count(before) == 1
    provider.write_text(text.replace(before, after))
    identity = next(iter(identities.values()))
    (snapshot / 'cmake/qualification_candidate_lock.cmake').write_text(
        f'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "{identity["version"]}")\n'
        f'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "{identity["abi"]}")\n'
        f'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "{identity["sourceHash"]}")\n')
    probe = snapshot / 'qualification_route_probe.cpp'
    probe.write_text('''#include "app_runtime/runtime_shaped_text_binding.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_prepared_text.h"
#include <cassert>
static_assert(JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API == QUALIFICATION_CONTEXTUAL);
static_assert(JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API == QUALIFICATION_CONTEXTUAL);
static_assert(!JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API);
#if QUALIFICATION_CONTEXTUAL && !defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#error "installed positive corpus requires explicit fixtures"
#endif
int main() {
    assert(jellyframe::script_task_app_frame_codec_supports_version(12) == bool(QUALIFICATION_CONTEXTUAL));
    assert(!jellyframe::script_task_app_frame_codec_supports_version(11));
    assert(!jellyframe::script_task_app_frame_codec_supports_version(10));
}
''')
    with (snapshot / 'CMakeLists.txt').open('a') as cmake:
        cmake.write('''
# Isolated qualification only: no replacement Core source or private headers.
add_executable(jellyframe_installed_route_probe qualification_route_probe.cpp)
target_link_libraries(jellyframe_installed_route_probe PRIVATE jellyframe_script_task_runtime)
target_compile_definitions(jellyframe_installed_route_probe PRIVATE QUALIFICATION_CONTEXTUAL=${QUALIFICATION_CONTEXTUAL})
jellyframe_enable_shaped_test_fixtures(jellyframe_installed_route_probe)
jellyframe_enable_test_asserts(jellyframe_installed_route_probe)
add_test(NAME jellyframe_installed_route_probe COMMAND jellyframe_installed_route_probe)
''')
    changed = ['CMakeLists.txt', 'cmake/jellyframe_render_core_provider.cmake']
    (output / 'qualification-only-edits.json').write_text(json.dumps({
        'changed': [{'path': name, 'sourceSha256': sha(runtime / name),
                     'snapshotSha256': sha(snapshot / name)} for name in changed],
        'added': ['cmake/qualification_candidate_lock.cmake', 'qualification_route_probe.cpp'],
        'sourceTestsModified': False, 'productionLockModified': False,
        'scope': 'All configured installed Runtime tests; source tool tests keep production manifests. '
                 'Examples, benchmarks, sample/Desktop suites and signed execution are not included.'
    }, indent=2) + '\n')

    def qualify(name):
        build = builds / name
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER=' + args.compiler,
                  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                  '-DJELLYFRAME_RENDER_CORE_PROVIDER=package',
                  '-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=' + str(packages[name]),
                  '-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF',
                  '-DJELLYFRAME_CONTEXTUAL_TEST_FIXTURES_ROOT=' + str(args.fixtures.resolve()),
                  '-DJELLYFRAME_BUILD_SCRIPTING=ON', '-DJELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON',
                  '-DJELLYFRAME_BUILD_EXAMPLES=OFF', '-DJELLYFRAME_BUILD_BENCHMARKS=OFF',
                  '-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF',
                  '-DJERRYSCRIPT_ROOT=' + str(args.jerry.resolve()),
                  '-DQUALIFICATION_CONTEXTUAL=' + ('0' if name == 'off' else '1')]
        if name == 'cropped':
            common += ['-DJELLYFRAME_ENABLE_' + feature + '=OFF' for feature in
                       ('CANVAS2D', 'MODERN_PAINT', 'FLEX_GRID', 'ADVANCED_FORMS', 'LENGTH_MATH')]
        run(name + '-configure', [args.cmake, '-S', snapshot, '-B', build] + common)
        run(name + '-build', [args.cmake, '--build', build, '--parallel', '2'])
        run(name + '-ctest', [args.ctest, '--test-dir', build, '--output-on-failure'])
        log = (output / (name + '-ctest.log')).read_text()
        total = int(re.search(r'0 tests failed out of (\d+)', log)[1])
        assert total >= 36 and 'Skipped' not in log
        audit = audit_installed_package(build, snapshot, packages[name], identities[name],
                                        runtime / 'cmake/jellyframe_dependency_lock.cmake')
        return {'profile': name, 'passed': total, 'skipped': 0, 'failed': 0,
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
    print(json.dumps(results, indent=2))
    return int(any('error' in row for row in results))


if __name__ == '__main__':
    raise SystemExit(main())
