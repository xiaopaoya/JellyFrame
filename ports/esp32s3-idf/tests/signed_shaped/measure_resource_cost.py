"""Serial host samples; never extrapolate these C++ counters to target heap."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--fixtures', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--runs', type=int, default=5)
args = parser.parse_args()
if args.runs < 2:
    parser.error('At least two independent processes are required')
args.output.mkdir(parents=True, exist_ok=False)
samples = []
for index in range(args.runs):
    result = subprocess.run([str(args.executable), 'resource_cost', str(args.fixtures)],
                            text=True, capture_output=True, timeout=60)
    (args.output / f'run-{index}.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(f'run {index} failed: {result.returncode}')
    records = [json.loads(line.removeprefix('RESOURCE '))
               for line in result.stdout.splitlines() if line.startswith('RESOURCE ')]
    lifetime = [json.loads(line.removeprefix('RESOURCE_LIFETIME '))
                for line in result.stdout.splitlines() if line.startswith('RESOURCE_LIFETIME ')]
    counts = defaultdict(int)
    for row in records:
        counts[row['phase']] += 1
        assert row['peak'] >= max(row['liveBefore'], row['liveAfter'])
    assert dict(counts) == dict(binding=1, initialize=1, script=1, publish=1,
                               take=33, paint_with_framebuffer=34, pointer_down=32,
                               pointer_up_update_publish=32), counts
    assert len(lifetime) == 1 and lifetime[0]['bindingExpired']
    # Same width/text branch and two-digit button count, after caches warmed.
    takes = [r for r in records if r['phase'] == 'take']
    samples.append(dict(index=index, records=records, lifetime=lifetime[0],
                        sameBranchRetainedDelta=takes[32]['liveAfter']-takes[12]['liveAfter']))

groups = defaultdict(list)
for sample in samples:
    phase_counts = defaultdict(int)
    for row in sample['records']:
        occurrence = phase_counts[row['phase']]
        phase_counts[row['phase']] += 1
        suffix = ''
        if row['phase'] in ('pointer_down', 'pointer_up_update_publish', 'take', 'paint_with_framebuffer'):
            suffix = ':first' if occurrence == 0 else ':later'
        # Last paint is retained replay after teardown, not ordinary paint.
        if row['phase'] == 'paint_with_framebuffer' and occurrence == 33:
            suffix = ':retained_replay'
        groups[row['phase'] + suffix].append(row)

summaries = {}
for name, rows in groups.items():
    times = sorted(row['nanoseconds'] for row in rows)
    summaries[name] = dict(samples=len(rows), medianMicroseconds=statistics.median(times)/1000,
                          p95Microseconds=times[math.ceil(len(times)*.95)-1]/1000,
                          medianRequestedBytes=statistics.median(r['requested'] for r in rows),
                          maxIncrementalPeakBytes=max(r['peak']-r['liveBefore'] for r in rows),
                          medianAllocations=statistics.median(r['allocations'] for r in rows))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = dict(executableSha256=sha(args.executable), signedSha256=sha(args.fixtures/'full-shaped.jfsg'),
              processes=args.runs, clicksPerProcess=32, summaries=summaries, samples=samples,
              limitations=['host wall time with allocation instrumentation, no device latency',
                           'requested C++ bytes only; excludes C malloc, VM arena and allocator overhead',
                           'process live total includes simulated NOR and test/reference buffers',
                           'bounded retained-delta observation is not an exhaustive leak proof',
                           'host Jerry arena512KiB, ESP external-context arena256KiB'])
(args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(dict(summaries=summaries,
                     retainedDeltas=[s['sameBranchRetainedDelta'] for s in samples]), indent=2))
