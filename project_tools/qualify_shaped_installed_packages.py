#!/usr/bin/env python3
"""Isolated local candidate-package qualification; never changes production lock.

Copies the current Runtime source bytes, pins only that copy to the candidate
Core identity, then builds through the actual imported-package provider. Core
private fixture helpers are test-only includes, never Core source link fallback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_package_linkage(build, snapshot, prefix, targets):
    """Prove the test executables import the archive, with live assertions."""
    ninja = (build / 'build.ninja').read_text(encoding='utf-8')
    # Ninja escapes drive colons; normalize only for comparisons, not execution.
    normalized = ninja.replace('$:', ':').replace('\\', '/')
    snapshot_path = snapshot.resolve().as_posix().lower() + '/'
    archive = (prefix / 'lib/jellyframe_render_core.lib').resolve().as_posix()
    compile_rules = re.findall(r'^build [^\n]+: CXX_COMPILER[^\n]*\n(?:  [^\n]*\n)*',
                               normalized, re.M)
    assert compile_rules, 'no C++ compilation records'
    for rule in compile_rules:
        # Every compiled C++ source belongs to Runtime's isolated snapshot.
        # Core private fixture headers may be included, but no source fallback
        # or second Core implementation is compiled into the consumer.
        first_line = rule.splitlines()[0]
        source = first_line.split(' ', 3)[3].split(' || ', 1)[0]
        assert source.lower().startswith(snapshot_path), first_line
    links = []
    for target in targets:
        match = re.search(r'^build ' + re.escape(target) + r'\.exe: CXX_EXECUTABLE_LINKER[^\n]*\n(?:  [^\n]*\n)*',
                          normalized, re.M)
        assert match, 'missing executable link: ' + target
        # Device wire/store contracts intentionally have no Core dependency.
        uses_core = target != 'jellyframe_device_runtime_contracts_tests'
        assert (archive in match.group()) == uses_core, 'unexpected Core linkage: ' + target
        objects = [rule for rule in compile_rules if
                   rule.startswith('build CMakeFiles/' + target + '.dir/')]
        assert objects and all('-UNDEBUG' in rule for rule in objects), 'assertions disabled: ' + target
        links.append({'target': target, 'archive': archive if uses_core else None, 'compileUnits': len(objects),
                      'assertionsEnabled': True})
    return {'coreSourceFallback': False, 'runtimeCompileUnits': len(compile_rules),
            'coreArchiveSha256': sha(Path(archive)), 'targets': links}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New isolated experiment directory')
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jerry', type=Path, required=True)
    parser.add_argument('--profile', choices=('full', 'cropped'), required=True)
    args = parser.parse_args()
    runtime = Path(__file__).resolve().parents[1]
    core = args.core.resolve()
    output = args.output.resolve()
    builds = args.build_root.resolve()
    output.mkdir(parents=True, exist_ok=False)
    builds.mkdir(parents=True, exist_ok=False)
    def run(label, command, cwd=None):
        with (output / (label + '.log')).open('w', encoding='utf-8') as log:
            log.write(json.dumps([str(arg) for arg in command]) + '\n')
            log.flush()
            subprocess.run([str(arg) for arg in command], cwd=cwd, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        print(label + ': passed', flush=True)
    snapshot = output / 'runtime-source'
    names = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'],
                                    cwd=runtime, text=True).splitlines()
    inputs = []
    for name in sorted(set(names)):
        source = runtime / name
        if not source.is_file():
            continue
        destination = snapshot / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        inputs.append({'path': name, 'sha256': sha(source)})
    (output/'runtime-inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
    # The documentation gate intentionally enumerates versioned Markdown via
    # Git. Give this disposable snapshot its own index, including new candidate
    # documents, rather than skipping that gate or touching the real index.
    run('snapshot-doc-index-init', ['git', 'init', '--quiet', snapshot])
    doc_index = output/'snapshot-doc-index.paths'
    doc_index.write_bytes(b''.join(item['path'].encode()+b'\0' for item in inputs
                                  if item['path'].endswith('.md')))
    run('snapshot-doc-index-add', ['git', '-c', 'core.autocrlf=false', 'add', '--force',
        '--pathspec-from-file='+str(doc_index), '--pathspec-file-nul'], cwd=snapshot)
    lock = runtime/'cmake/jellyframe_dependency_lock.cmake'
    original_lock_sha = sha(lock)
    core_build = builds/'core'
    prefix = output/'installed-core'
    enabled = 'ON' if args.profile == 'full' else 'OFF'
    run('core-configure', [args.cmake, '-S', core, '-B', core_build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+args.compiler,
        '-DCMAKE_MAKE_PROGRAM='+args.ninja, '-DJELLYFRAME_ENABLE_TEXT_SHAPING=ON',
        '-DJELLYFRAME_BUILD_TESTS=OFF', '-DJELLYFRAME_BUILD_BENCHMARKS=OFF',
        '-DJELLYFRAME_INSTALL_RENDER_CORE=ON',
        *['-DJELLYFRAME_ENABLE_'+family+'='+enabled for family in
          ('CANVAS2D', 'MODERN_PAINT', 'FLEX_GRID', 'ADVANCED_FORMS')]])
    run('core-build', [args.cmake, '--build', core_build, '-j', '4'])
    run('core-install', [args.cmake, '--install', core_build, '--prefix', prefix])
    manifest = json.loads((prefix/'share/jellyframe-render-core/jellyframe_render_core_source_manifest.json').read_text())
    for item in manifest['files']:
        normalized = (core/item['path']).read_bytes().replace(b'\r\n', b'\n').replace(b'\r', b'\n')
        assert hashlib.sha256(normalized).hexdigest() == item['sha256'], item['path']
    joined = ''.join(item['path']+'\t'+item['sha256']+'\n' for item in manifest['files']).encode()
    assert hashlib.sha256(joined).hexdigest() == manifest['sourceHash']
    # This is a candidate lock in an isolated copy, not a weakened package check.
    (snapshot/'cmake/jellyframe_dependency_lock.cmake').write_text(
        'set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.9.0")\n'
        'set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "4")\n'
        'set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH "'+manifest['sourceHash']+'")\n')
    targets = ['jellyframe_app_runtime_tests', 'jellyframe_script_tests',
               'jellyframe_script_task_runtime_tests',
               'jellyframe_esp32s3_installed_core_text_0_tests',
               'jellyframe_esp32s3_installed_core_text_1_tests',
               'jellyframe_esp32s3_installed_resource_owner_tests',
               'jellyframe_device_runtime_contracts_tests']
    cmake_path = snapshot/'CMakeLists.txt'
    cmake = cmake_path.read_text(encoding='utf-8')
    cmake += '\n# Isolated package qualification: fixture helpers only, no Core source target.\n'
    for target in targets[:5]:
        cmake += f'target_include_directories({target} PRIVATE "{core.as_posix()}/src" "${{CMAKE_CURRENT_SOURCE_DIR}}/src/app_runtime/tests")\n'
        cmake += f'target_compile_definitions({target} PRIVATE JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT="{core.as_posix()}/tests/fixtures")\n'
    cmake_path.write_text(cmake, encoding='utf-8')
    # A missing feature/fixture must fail compilation instead of silently
    # excluding the positive shaped corpus from an apparently green run.
    for relative in ('src/app_runtime/tests/app_installed_bundle_tests.cpp',
                     'src/app_runtime/tests/script_task_frame_renderer_tests.cpp',
                     'src/script/tests/script_task_worker_runtime_tests.cpp',
                     'ports/esp32s3-idf/tests/installed_core_text_tests.cpp'):
        path = snapshot/relative
        text = path.read_text(encoding='utf-8')
        text += '\n#if !JELLYFRAME_RUNTIME_SHAPED_TEXT_API || !defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT) || !defined(JELLYFRAME_SHAPED_TEXT_LAYOUT_API_VERSION)\n#error "installed qualification omitted shaped DOM corpus"\n#endif\n'
        path.write_text(text, encoding='utf-8')
    worker = snapshot/'src/script/tests/script_task_worker_runtime_tests.cpp'
    text = worker.read_text(encoding='utf-8')
    old = 'if (script_task_app_frame_codec_supports_version(11)) {\n            worker_shaped_dom_layout_and_input();'
    assert old in text
    text = text.replace(old, 'check(script_task_app_frame_codec_supports_version(11), "installed v11 must be available");\n        {\n            worker_shaped_dom_layout_and_input();', 1)
    worker.write_text(text, encoding='utf-8')
    changed = ('CMakeLists.txt', 'cmake/jellyframe_dependency_lock.cmake',
        'src/app_runtime/tests/app_installed_bundle_tests.cpp',
        'src/app_runtime/tests/script_task_frame_renderer_tests.cpp',
        'src/script/tests/script_task_worker_runtime_tests.cpp',
        'ports/esp32s3-idf/tests/installed_core_text_tests.cpp')
    (output/'qualification-only-edits.json').write_text(json.dumps([
        {'path':name, 'originalSha256':sha(runtime/name), 'qualifiedSha256':sha(snapshot/name)}
        for name in changed], indent=2)+'\n')
    runtime_build = builds/'runtime'
    run('runtime-configure', [args.cmake, '-S', snapshot, '-B', runtime_build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+args.compiler,
        '-DCMAKE_MAKE_PROGRAM='+args.ninja,
        '-DJELLYFRAME_RENDER_CORE_PROVIDER=package', '-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR='+str(prefix),
        '-DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF', '-DJELLYFRAME_ENABLE_TEXT_SHAPING=ON',
        '-DJELLYFRAME_BUILD_TESTS=ON', '-DJELLYFRAME_BUILD_APP_RUNTIME=ON',
        '-DJELLYFRAME_BUILD_SCRIPTING=ON', '-DJELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON',
        '-DJELLYFRAME_BUILD_EXAMPLES=OFF', '-DJELLYFRAME_BUILD_BENCHMARKS=OFF',
        '-DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF', '-DJERRYSCRIPT_ROOT='+str(args.jerry)])
    run('runtime-build', [args.cmake, '--build', runtime_build, '--target', *targets, '-j', '4'])
    ctest = str(Path(args.cmake).with_name('ctest.exe')) if Path(args.cmake).is_absolute() else 'ctest'
    expression = '^('+'|'.join(targets)+')$'
    run('runtime-ctest', [ctest, '--test-dir', runtime_build, '-C', 'Release', '-V', '-R', expression])
    run('runtime-build-complete', [args.cmake, '--build', runtime_build, '-j', '4'])
    run('runtime-ctest-complete', [ctest, '--test-dir', runtime_build, '-C', 'Release',
                                    '--output-on-failure'])
    provenance = json.loads((runtime_build/'generated/jellyframe_render_core_provenance.json').read_text())
    cache = (runtime_build/'CMakeCache.txt').read_text()
    assert 'JELLYFRAME_RENDER_CORE_PROVIDER:STRING=package' in cache
    assert 'JELLYFRAME_RENDER_CORE_SOURCE_DIR:PATH=\n' in cache
    assert original_lock_sha == sha(lock), 'production lock changed'
    log = (output/'runtime-ctest.log').read_text(encoding='utf-8')
    assert '100% tests passed' in log and 'Skipped' not in log
    complete_log = (output/'runtime-ctest-complete.log').read_text(encoding='utf-8')
    assert '100% tests passed' in complete_log and 'Skipped' not in complete_log
    linkage = audit_package_linkage(runtime_build, snapshot, prefix, targets)
    (output/'package-linkage-audit.json').write_text(json.dumps(linkage, indent=2)+'\n')
    result = {'profile':args.profile, 'coreSourceHash':manifest['sourceHash'],
        'coreSourceFileCount':manifest['sourceFileCount'], 'provenance':provenance,
        'productionLockSha256':original_lock_sha, 'productionLockChanged':False,
        'coreLibrarySha256':sha(prefix/'lib/jellyframe_render_core.lib'),
        'packageLinkageAudit':'package-linkage-audit.json',
        'completeCTestLog':'runtime-ctest-complete.log',
        'targets':targets, 'qualificationOnlyEdits':list(changed),
        'limit':'local candidate package and explicit test fixtures; not published lock/CI or device qualification'}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)


if __name__ == '__main__':
    main()
