# Benchmarks

> Last updated: 2026-09-19; Applies to: 0.6.0-dev

Root-level benchmarks are reserved for future cross-subproject and app-lifecycle
benchmarks.

Current microbenchmarks live next to their owning subprojects:

- `../src/render_core/benchmarks`: parser/style/render/layout/layer pipeline
  microbenchmarks.
- `../src/app_runtime/benchmarks`: request/completion queue and host-handle
  microbenchmarks.

Build them with:

```powershell
cmake -S . -B build/desktop-release -DJELLYFRAME_BUILD_BENCHMARKS=ON
cmake --build build/desktop-release --config Release
```

The generated executables are `jellyframe_render_core_microbench` and
`jellyframe_app_runtime_microbench`.

Windows builds also produce `jellyframe_cpu2d_compare`. Its `opaque-fill`
workload compares the same opaque full-frame RGB fill against a memory-DIB GDI
operation and requires exact normalized RGB output. Its `horizontal-gradient`
and `vertical-gradient` workloads compare opaque gradients against the
corresponding GDI `GradientFill` mode and require normalized RGB RMSE <= 1.0.
These are 172x320 primitive probes and do
not claim whole-library, complete UI, device, rounded-gradient, text, or GPU
performance.

The same executable can optionally load an SDL2 DLL at runtime and compare the
`opaque-fill`, `opaque-dirty-fill` and `alpha-grid` workloads with
`SDL_CreateSoftwareRenderer`. The dirty workload writes a fixed 96x48 rectangle
on an initialized 172x320 black surface, reports 4,608 pixels per operation,
and batches 64 operations inside each timing sample. This does not add an SDL
dependency to JellyFrame builds or packages. The adapter deliberately rejects
gradient workloads because SDL2's software renderer has no equivalent gradient
primitive.

`alpha-grid` draws 64 non-overlapping 16x12 source-over tiles on black. Every
sample resets the surface outside the timed interval, then reports per-tile time
and 192 pixels. JellyFrame, GDI `AlphaBlend` and SDL2 software blending must
produce exact normalized RGB output.
The `alpha-grid-rgb-v2` timer includes one end-of-batch completion call
(`GdiFlush` or `SDL_RenderFlush`, requiring SDL2 2.0.10+). Reset completion is
outside the timer. Paired samples alternate execution order, and an independent
pixel oracle checks the final image. V1 timing claims are withdrawn because it
had no completion barrier. Percentiles describe batch-average per-tile time,
not individual invocation tail latency.

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-fill 100 opaque-fill
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-gradient 100 horizontal-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-vertical-gradient 100 vertical-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-sdl2 100 opaque-fill sdl2 C:\path\to\SDL2.dll
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-dirty-sdl2 100 opaque-dirty-fill sdl2 C:\path\to\SDL2.dll
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-alpha-sdl2 100 alpha-grid sdl2 C:\path\to\SDL2.dll
```

The SDL2 run writes `jellyframe.json` and `sdl2.json`. Compare them with
`../tools/benchmark_compare.py`; both sides must still pass exact normalized RGB
validation before timing or throughput is comparable. The result describes only
the declared full or local opaque fill, not layout, dirty-region planning,
window present, complete UI performance or GPU rendering.

Cross-library comparisons use the fixed-condition manifest consumed by
`../tools/benchmark_compare.py`. Each adapter must provide the same workload,
viewport, pixel format, antialiasing and repaint mode before p50/p95 or MPix/s
results are considered comparable.

```json
{
  "format": "jellyframe.benchmark.run.v0",
  "library": "jellyframe",
  "version": "0.6.0-dev",
  "workload": "opaque-fill-rgb-v1",
  "viewport": {"width": 172, "height": 320},
  "pixelFormat": "rgb888",
  "antialiasing": false,
  "mode": "full",
  "environment": {"os": "windows", "architecture": "x64", "cpu": "model", "buildType": "release"},
  "warmupIterations": 30,
  "operationsPerSample": 1,
  "workloadParameters": {"surfaceInitialRgb": "000000", "rect": {"x": 0, "y": 0, "width": 172, "height": 320}, "operation": "fill-rect", "sourceRgb": "164757", "blend": "opaque-replace"},
  "outputValidation": {"status": "pass", "method": "normalized-rgb-exact", "reference": "opaque-fill-rgb-v1"},
  "measurements": {"paint_us": [4.0, 3.9, 4.1], "pixels": [55040, 55040, 55040]}
}
```

Run the comparison with:

```powershell
python tools\benchmark_compare.py --baseline build\jellyframe.json --candidate build\cairo.json --output build\comparison.json --html-output build\comparison.html
```

Only standardized metrics are accepted: `frame_us`, `cpu_us`, `paint_us`,
`present_us`, `dma_wait_us`, `pixels`, `dirty_pixels`, and `peak_bytes`.
Throughput is derived only when paired pixel and positive-duration samples have
the same cardinality; same-name metrics with different sample counts are
individually marked `not-comparable`. Both sides must pass the same
output-validation method, reference and tolerance; otherwise every metric is
marked `not-comparable`. `operationsPerSample` defaults to 1 for old manifests,
must be a positive integer, and must match between both sides. New adapters
should also emit the bounded `workloadParameters` object. Geometry, colors,
blend, gradient axis and endpoint convention must match exactly; a legacy
manifest without parameters is comparable only with another parameter-less
manifest.

## Bitmap Text

The GDI-only `bitmap-text` mode adds `bitmap-clock-text-rgb-v1`: eight fixed
ASCII clock lines from the same six-glyph 1bpp font, rendered at 2x scale. Core
uses its existing `BitmapFont` painter; GDI uses cached glyph `TransparentBlt`,
not `DrawText` or a system font. Both include shared advance measurement and
completed drawing inside the timer, with reset and atlas construction outside.
Independent mask validation and exact full-surface RGB comparison are required.

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-text 500 bitmap-text
```

Each sample is an eight-line batch divided by eight; `pixels=548` is the mean
number of ink pixels per line. Results do not cover layout, automatic wrapping,
TTF/CJK shaping, AA, font generation/loading, GDI native text, GPU or devices.
SDL2 rejects this mode. Glyph masks, font metrics and layout are frozen in the
manifest; changing any of them requires a separate comparable workload.

## Rounded Coverage Qualification

Native GDI `RoundRect` uses binary coverage; Core uses a 4x4 quarter-pixel
coverage grid. Check that mismatch explicitly before implementing a rounded
timing adapter:

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\rounded-qualification 1 rounded-qualification
```

This GDI-only mode requires `samples=1` and measures no performance. It checks
Core against an independent coverage oracle and writes `core.bmp`, `gdi.bmp`,
`difference.bmp` and `qualification.json`. Exit 0 means the probe completed,
not that the backends are comparable. The report is deliberately marked
`not-comparable` and `performanceMeasured: false`; `benchmark_compare.py` rejects
its qualification format as a performance manifest. A matching-AA adapter is
still required before rounded performance comparisons are accepted.

The follow-up `rounded-supersample-qualification` mode uses Windows GDI+
`FillPath` at 4x resolution with smoothing disabled and `PixelOffsetModeNone`,
then reduces each 4x4 block. It tests eight frozen cases: a card, a rectangle
control, small and maximum radii, odd dimensions, two clipped shapes and a
2x2 shape. The mask is drawn by GDI+, not generated from the Core oracle.

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\rounded-supersample 1 rounded-supersample-qualification
```

The independent analytic-circle oracle checks Core in every case. Reports
count **coverage** differences before RGB rounding, record the maximum coverage
error and write per-case Core/reference/difference BMPs. All cases must match
exactly for `output-qualified`; any mismatch gives `not-comparable`. Neither
status enables timing or makes this a benchmark manifest. The rectangle control
detects coordinate/stride mistakes; the other cases prevent qualifying a backend
from one favorable radius. Local qualification on 2026-09-19 found seven failing
rounded cases; supersampling alone does not establish matching coverage.

GDI+ is linked only into this Windows benchmark executable, never the Core,
Runtime, SDK or device binaries. An eventual timing adapter would have to include
its supersampled rendering, reduction and completion in the timed interval;
precomputed native masks must not be compared against live Core coverage work.

## Native AA Quality

Native AA quality is separate from exact-output qualification. Export raw white
coverage masks for Core, native GDI+ AA and a binary negative control, then run
the independent continuous-area evaluator:

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\aa-quality 1 rounded-quality-masks
python tools\rounded_aa_quality.py --input build\aa-quality\coverage.json --output build\aa-quality\quality.json --markdown-output build\aa-quality\report.md
```

The experimental `rounded-area-quarter-v0` policy requires exact interior/exterior
coverage, maximum edge error <= 1/4 and edge-only RMS <= 1/8. Integer subdivision
encloses the continuous area of each unit pixel, retaining uncertainty intervals.
An interval crossing a limit is indeterminate, not a pass. Core is assessed under
the same rules; its current point-sampling contract is not the ideal area oracle.
The analyzer rejects changed fixtures, malformed masks, missing cases, path escapes
and outputs overwriting inputs. Reports contain input hashes and no timing data.
Exit 0 only means analysis completed; the quality report cannot enter the benchmark
rankings. This policy is not a Runtime release gate. See
`../docs/render_performance_native_aa_quality_zh.md` for the fixed scope and rules.

For diagnostic completed-draw timings on Windows 10+, use:

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\aa-cost 500 rounded-quality-cost
python tools\rounded_aa_quality.py --input build\aa-cost\coverage.json --cost-input build\aa-cost\cost.json --output build\aa-cost\joint.json --markdown-output build\aa-cost\report.md
```

Targets/contexts/brushes are reused; completed black reset is outside the timer.
Each timed operation includes command/path construction, drawing, destruction
and completion. The three backends rotate through all six execution orders, with
30 warmups per case/backend. Samples are individual operations, not batch means.
Mask extraction and I/O are untimed; masks come from the final measured draw.
SHA-256, fixed fixtures, backend methods and the frozen timing contract must match
before the analyzer joins cost to quality. Sample counts are bounded to 1-10000.

`jellyframe.rounded.quality-cost.v0` retains raw samples, nearest-rank p50/p95 and
quality status, but remains diagnostic-only: no FPS, ratios, winners or claims of
equivalent output. The existing benchmark ranking tool rejects this format.
Use Release and repeat sequentially without simultaneous builds. Do not pool
repeat samples or extrapolate these CPU draw timings to layout, present or devices.

## Core Rounded Cost Probes

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\rounded-probes 500 rounded-core-probes
```

Writes `probes.json` and `report.md` for the eight frozen uniform-radius fixtures.
Three rotating paths measure production draw, isolated coverage calculation into
a preallocated array, and center/corner writes using cached coverage. Setup and
reset are untimed, with 30 warmups and 1-10000 individual samples per path/case.
Both final framebuffer outputs must exactly match the independent quarter-grid
oracle. Mask SHA-256 also matches the quality exporter. Untimed partition counts
describe center/corner work, not sampled hardware instructions.

These are diagnostic microexperiments, **not additive production phase timings**.
Do not subtract p95s, infer frame percentages, rank libraries or claim a production
cache implementation. Probe array traffic/setup differs from the real renderer;
tiny durations may round to zero at the clock resolution. No production rendering
code or device instrumentation is changed. The existing comparison tool rejects
`jellyframe.rounded.core-probes.v0`. Use Release, repeat sequentially, and retain
the raw arrays and executable hash. See the native-AA contract for boundaries.

The current Core candidate uses a conservative full/empty shortcut for the
uniform rounded coverage helper: it compares the maximum or minimum distance
over a pixel's 4x4 sample interval with the same saturating arithmetic used by
the exact loop. Only proven all-full/all-empty pixels skip the sixteen tests;
partial pixels keep the old loop. The candidate's eight masks and RGBA output
match the pre-change hashes. On the fixed desktop probes, the card production
p50 moved from about 151 us to 29.6--29.7 us, and maximum-radius moved from
596.5 us to 97 us across three sequential 500-sample runs. These figures are
diagnostic only and do not establish device or full-frame speedup.

The Render Core microbench also exposes `per_corner_rounded_rect_aa_raster`.
It uses 36 fixed 44x34 opaque commands with radii `{12,6,3,0}` and is intended
only as a local before/after signal; target/rasterizer setup is included in the
existing average-microbench contract. In a sequential Release comparison on
the same machine, three baseline runs measured 368.245 / 385.400 / 367.175 us
and the candidate measured 175.635 / 174.935 / 179.575 us. The corresponding
uniform workload stayed within normal run noise. The candidate fills non-corner
row spans directly and retains the existing coverage helper for all corner
candidate pixels, including overlapping-radius branch precedence.

The microbench also exposes `rounded_stroke_aa_raster` and
`per_corner_stroke_aa_raster`. These are diagnostic workload entries, not a new
cross-library contract. The current Release reference on the local machine was
about 693.695 us for 36 uniform 44x34 stroke commands and 350.305 us for 36
non-uniform 1px stroke commands. The existing stroke implementation already
restricts middle rows to vertical bands plus corner candidates; no additional
production stroke rewrite is justified by this measurement alone.

Rounded clip replay has two additional entries: `rounded_clip_replay_raster` and
`nested_rounded_clip_replay_raster`. Both replay the same 36 fill commands into
one temporary surface; the second applies two clip records and has a smaller
inner visible region, so its time must not be divided by clip count. Three
sequential Release runs measured single-clip `519.490 / 503.450 / 520.095 us`
and nested-clip `437.925 / 430.275 / 443.420 us`.

The one-shot `rounded_clip_profile` line reports the existing counters without
timing instrumentation: 36 commands, 53,856 replay candidate pixels, 83,200
temporary/mask pixels, 208 full-coverage rows, 52 sampled rows, 2,016 clip
visits, 127 subpixel math visits, 14,624 known-full pixels and 9,869 blended
pixels. These counts are work-shape diagnostics, not CPU instruction counts or
additive phase time. Replay, temporary-surface preparation and composite remain
separate concepts; no device performance claim follows from this desktop entry.

The companion `rounded_clip_profile_timing` line enables the existing optional
clock and reports average replay, temporary preparation and composite time over
the same iteration count. Three fresh sequential Release runs measured
replay `141.805 / 143.110 / 144.075 us`, preparation `10.745 / 9.750 / 10.270 us`,
and composite `64.950 / 64.130 / 65.415 us`; sampled/full composite portions were
`40.785 / 39.900 / 40.370 us` and `24.165 / 24.230 / 25.045 us`. Clock reads are
profiling overhead and these values are not a device frame budget. The row
classification scan remains inside composite and has no independent timer;
these results do not justify an allocating row-metadata cache, so the current
production clip algorithm is retained.

## Repeated Comparisons

The same tool accepts 3-32 independent run manifests per side, in paired repeat
order. A single path per side preserves the existing single-run report. Run
measurements sequentially and alternate backend/executable order; do not benchmark
while builds or other benchmark processes compete for the CPU.

```powershell
python tools\benchmark_compare.py `
  --baseline baseline/01.json baseline/02.json baseline/03.json `
  --candidate candidate/01.json candidate/02.json candidate/03.json `
  --output comparison/repeats.json --html-output comparison/repeats.html
```

The repeat report uses `jellyframe.benchmark.repeat-comparison.v0`. It retains
each pair, each repeat p95, the median and min/max of those p95s, paired deltas,
input paths and SHA-256 hashes. It does not pool samples or infer statistical
significance from a lower median. Zero baselines have a null percentage delta.
Batch-average samples remain batch-average samples, not individual-operation tails.

All repeats must have identical fixed conditions, including environment and
workload parameters, even if both sides change together. Library/version must
remain stable within each side (but may differ between sides). Unequal repeat
counts, fewer than three repeats, failed output validation, missing metrics or
changed sample counts prevent the affected comparison. Duplicate input paths and
outputs that overwrite inputs are rejected. The tool cannot prove that separately
named files represent independent measurements or that execution was interleaved;
preserve runner commands and execution-order logs alongside the manifests.

Device aggregate windows must still use `tools/device_performance_compare.py`;
do not reinterpret those histograms as desktop invocation samples.
