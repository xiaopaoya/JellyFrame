"""Observe the actual pinned host VM fatal boundary, never call it recovery."""
import subprocess
import sys

result = subprocess.run([sys.argv[1], 'vm_oom', sys.argv[2]],
                        capture_output=True, text=True, timeout=30)
print(result.stdout, end='')
print(result.stderr, end='', file=sys.stderr)
if result.returncode != 10 or 'VM_OOM_BEGIN initialized authenticated worker' not in result.stdout:
    raise SystemExit(f'Expected initialized worker then Jerry OOM exit10; got {result.returncode}')
print('Verified host VM fatal exit10; worker recovery and ESP task cleanup are NOT established')
