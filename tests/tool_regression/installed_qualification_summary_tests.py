#!/usr/bin/env python3
"""Keep installed-package qualification compatible with supported CTest summaries."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'project_tools'))
from qualify_contextual_installed_packages import parse_ctest_passed_count


assert parse_ctest_passed_count('0 tests failed out of 41') == 41
assert parse_ctest_passed_count('100% tests passed, 0 tests failed out of 41') == 41
assert parse_ctest_passed_count('100% tests passed (0 tests failed out of 41)') == 41

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
    parse_ctest_passed_count('Test project has no tests')
except RuntimeError as error:
    assert 'no recognized final summary' in str(error)
else:
    raise AssertionError('missing CTest summary was accepted')

print('installed qualification CTest summary parser tests passed')
