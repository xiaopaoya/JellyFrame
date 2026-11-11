#!/usr/bin/env python3
"""Compile the script Event capability contract against versioned Core fixtures."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--compiler', type=Path, required=True)
arguments = parser.parse_args()


class ScriptEventPolicyTests(unittest.TestCase):
    def test_policy_and_exact_api_versions(self):
        # These compile-only fixtures test protocol/version admission. Real
        # installed Core/worker tests separately cover layout, linking and JS.
        cases = [
            ('legacy_old_default', None, None, None, None, True, True, True),
            ('legacy_new_default', None, 1, 1, 1, True, True, True),
            ('legacy_new_off', 0, 1, 1, 1, True, True, True),
            ('legacy_no_forms_no_rtti', 0, None, None, None, False, False, True),
            ('legacy_forms_need_rtti', 0, None, None, None, True, False, False),
            ('r3_all_v1_no_rtti', 1, 1, 1, 1, True, False, True),
            ('r3_old_core', 1, None, None, None, True, True, False),
            ('r3_completion_only', 1, 1, None, 1, True, True, False),
            ('r3_failure_only', 1, None, 1, 1, True, True, False),
            ('r3_completion_zero', 1, 0, 1, 1, True, True, False),
            ('r3_completion_future', 1, 2, 1, 1, True, True, False),
            ('r3_failure_zero', 1, 1, 0, 1, True, True, False),
            ('r3_failure_future', 1, 1, 2, 1, True, True, False),
            ('r3_forms_need_kind', 1, 1, 1, None, True, True, False),
            ('r3_future_kind', 1, 1, 1, 2, True, True, False),
            ('r3_no_forms_no_kind', 1, 1, 1, None, False, False, True),
            ('r3_no_forms_ignores_kind', 1, 1, 1, 0, False, False, True),
            ('invalid_policy', 2, 1, 1, 1, True, True, False),
        ]
        with tempfile.TemporaryDirectory(prefix='jellyframe-event-policy-') as directory:
            root = Path(directory)
            headers = root / 'render_core'
            headers.mkdir()
            source = root / 'probe.cpp'
            msvc = arguments.compiler.name.lower() in ('cl.exe', 'cl', 'clang-cl.exe', 'clang-cl')
            for name, policy, completion, failure, submit, forms, rtti, accepted in cases:
                with self.subTest(name=name):
                    api = {'DISPATCH_COMPLETION': completion, 'DISPATCH_FAILURE': failure, 'SUBMIT_KIND': submit}
                    (headers / 'event.h').write_text('\n'.join(
                        f'#define JELLYFRAME_EVENT_{key}_API {value}'
                        for key, value in api.items() if value is not None), encoding='utf-8')
                    (headers / 'feature_config.h').write_text(
                        f'#define JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED {int(forms)}\n', encoding='utf-8')
                    policy_line = '' if policy is None else f'#define JELLYFRAME_SCRIPT_R3_EVENTS {policy}\n'
                    source.write_text(policy_line + '#include "script/script_event_capabilities.h"\n'
                        f'static_assert(JELLYFRAME_SCRIPT_R3_EVENTS == {policy or 0}, "explicit policy");\n',
                        encoding='utf-8')
                    if msvc:
                        flags = ['/nologo', '/Zs', '/std:c++17', f'/I{root}', f'/I{arguments.source_root / "src"}']
                        if not rtti: flags.append('/GR-')
                    else:
                        flags = ['-std=c++17', '-fsyntax-only', '-I', str(root), '-I', str(arguments.source_root / 'src')]
                        if not rtti: flags.append('-fno-rtti')
                    result = subprocess.run([str(arguments.compiler), *flags, str(source)],
                        capture_output=True, text=True, timeout=30)
                    output = result.stdout + result.stderr
                    self.assertEqual(result.returncode == 0, accepted, f'{name}: {output}')
                    if not accepted:
                        self.assertRegex(output, r'R3 script events require|R3 forms require|Legacy script forms require|must be 0 or 1')


if __name__ == '__main__':
    unittest.main(argv=['script_event_policy_tests.py'])
