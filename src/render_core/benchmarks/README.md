# Render Core Benchmarks

> Last updated: 2026-09-19; Applies to: 0.6.0-dev

Microbenchmarks in this directory measure the platform-neutral render pipeline:
HTML parsing, CSS parsing, style resolution, render tree, layout, layer tree,
display-list flattening and software rendering.

Executable: `jellyframe_render_core_microbench`.

On Windows, `jellyframe_cpu2d_compare` runs fixed-condition CPU 2D comparison
workloads through JellyFrame and a memory-DIB GDI operation in the same process.
Both use a 172x320 RGB surface, 30 warm-up calls and equal sample counts. The
default `opaque-fill` workload requires exact normalized RGB output. The
`horizontal-gradient` and `vertical-gradient` workloads compare opaque
gradients against the corresponding GDI `GradientFill` mode and require
normalized RGB RMSE <= 1.0; the implementations' integer endpoint conventions
differ by at most one channel value in the current fixture. The runner writes
`jellyframe.json` and `gdi.json` manifests for
`tools/benchmark_compare.py`.

For `opaque-fill`, `opaque-dirty-fill` and `alpha-grid`, the runner can instead load an SDL2
DLL and use `SDL_CreateSoftwareRenderer` against the same caller-owned 172x320
RGB surface. The dirty workload writes a fixed 96x48 region and batches 64
operations per measured sample before reporting per-operation time. SDL remains
a runtime-only benchmark dependency and is not linked into Render Core, Runtime,
SDK or App packages. Gradient modes are rejected because SDL2's software
renderer does not expose an equivalent primitive.

`alpha-grid` resets the surface outside each timed sample and draws 64
non-overlapping 16x12 source-over tiles. It reports per-tile time and requires
exact normalized RGB agreement with GDI `AlphaBlend` or SDL2 software blending.
V2 includes end-of-batch completion in the timer and completes reset beforehand.
It alternates paired sample order and checks an independent pixel oracle.
V1 lacked completion barriers; its timing rankings must not be reused.

`bitmap-text` is a GDI-only shared-font probe (`bitmap-clock-text-rgb-v1`).
The existing Core `BitmapFont` painter and GDI cached-glyph `TransparentBlt`
draw the same 1bpp clock font at 2x scale with identical advance measurement.
Eight fixed centered lines alternate two ASCII strings. Full-surface exact RGB
and an independent per-pixel mask oracle are mandatory. Missing glyphs are not
allowed. Measurement includes completion and shared advance lookup, excludes
font/atlas creation and reset, and reports batch-average us/line and 548 ink
pixels/line. This is not a benchmark of GDI `DrawText`, TTF/CJK shaping, wrapping,
antialiased text, font loading or complete UI. SDL2 does not support this mode.

The 2026-09-19 Windows Release run used six sequential repeats, 500 samples and
30 warmups each. Median repeat p95 was 2.888 us/line for Core and 49.9565 us/line
for this GDI glyph-blit adapter; all RGB digests were `464c5cd1e8b7b543`.
Artifacts: `D:/JellyFramePerf/bitmap-text-gdi-20260919/final/`. This compares the
declared implementations, including GDI's per-glyph blit calls, not an optimal
native text engine or a general text/library ranking.

```powershell
jellyframe_cpu2d_compare <output-directory> 100 opaque-fill
jellyframe_cpu2d_compare <output-directory> 100 horizontal-gradient
jellyframe_cpu2d_compare <output-directory> 100 vertical-gradient
jellyframe_cpu2d_compare <output-directory> 500 bitmap-text
jellyframe_cpu2d_compare <output-directory> 100 opaque-fill sdl2 C:\path\to\SDL2.dll
jellyframe_cpu2d_compare <output-directory> 100 opaque-dirty-fill sdl2 C:\path\to\SDL2.dll
jellyframe_cpu2d_compare <output-directory> 100 alpha-grid sdl2 C:\path\to\SDL2.dll
```

On the development Windows machine, reusing the first clipped horizontal row
reduced the 100-sample JellyFrame gradient p95 from 437.7 us to 4.3 us while
preserving its `177877a38d09ae83` output digest; GDI measured approximately
4.4 us p95. These values apply only to this opaque 172x320 horizontal-gradient
primitive. They do not describe complete UI, device FPS, rounded/translucent
gradients, text, or GPU performance.

Retained repaint probes:

- `retained_layout_display_pipeline` measures full-page layer rebuild plus
  `flatten_into(...)` from an already retained layout tree.
- `retained_style_apply_layout` measures copying paint/transform style changes
  from a rebuilt render tree into a retained layout tree.
- `retained_style_layer_tree` measures layer/display-command rebuild from that
  retained layout tree.
- `retained_style_display_pipeline` measures layer rebuild plus
  `flatten_into(...)` with reusable display-list storage.
- `custom_property_style_resolve` measures batched style resolution for a
  theme-heavy tree that uses inherited CSS custom properties and `var(...)`.
  It exercises `StyleResolveContext` inherited-scope sharing and matched-rule
  reuse. Only nodes that actually redefine a custom property allocate a local
  scope; ordinary descendants share their parent's immutable map for the build.
- `custom_property_style_resolve_naive` is the same workload through the
  single-node resolver entry point. It is a regression reference for the
  contextual inheritance path, not a recommended host integration pattern.
- `style_resolve` measures the equivalent batched resolver path for a normal
  page with no custom properties. It guards the invariant that an unused
  custom-property feature does not allocate per-node cache entries.
- Radial gradients and rounded shadows share the same integer-only 13/32
  diagonal distance approximation. It avoids per-pixel square roots while
  keeping circular highlights and shadow contours visually close at axis and
  diagonal sample points. A true circle (`border-radius: 50%` on a square box)
  is the intentional exception for box-shadow: it uses exact distance so a
  visible circular glow does not degrade into an octagon. This work is paid
  only by that circular-shadow command.
- Ordinary non-circular rounded shadows resolve their geometry and y distance
  once per scanline before evaluating x distance. This preserves the same
  coverage and quadratic falloff while avoiding repeated invariant math. It
  adds no shadow cache or surface allocation; the exact circular path remains
  separate and is measured by the probe below.
- The provably zero-distance core of a non-circular shadow uses a bounded
  source-over span with the same blend primitive. Rounded corner quadrants and
  circular shadows retain their original per-pixel distance paths. Compare
  this path on a device with the full frame fixture; desktop microbench output
  cannot establish an MCU frame-rate gain.
- On the WS147 full-frame rounded value-frame fixture, the corresponding
  platform-neutral path reduced measured box-shadow replay per frame by 34.46%
  and render p95 by 11.43% without a RAM-watermark regression. That hardware
  A/B is evidence for this command family only, not a general FPS guarantee.
- Full-coverage rows in a rounded temporary-surface composite copy contiguous
  opaque spans directly while preserving source-over blending for translucent
  spans. This targets rounded composite time and must be judged by its separate
  device phase telemetry, not by replay-command timing.
- `circular_box_shadow_exact_raster` measures the exact-distance 120px circular
  glow used by the 172x320 wearable Activity-ring fixture. Compare it with
  `soft_box_shadow_raster`; do not use it to estimate ordinary rounded-card
  shadow cost.
- `dirty_rect_replay_contained` measures software compositor replay when dirty
  rectangles contain duplicates or nested rectangles. The compositor normalizes
  those rectangles before clearing and replaying commands.
- `opaque_linear_gradient_raster` measures the direct-write path for a full
  opaque rectangular screen gradient. Rounded or translucent gradients retain
  the antialiased source-over path, so static pages without this gradient form
  carry no new state or per-frame work.
- `opaque_horizontal_linear_gradient_raster` and
  `opaque_diagonal_linear_gradient_raster` cover the same direct-write subset
  for horizontal and diagonal axes.
- `packed_rgb565_dither_present` measures 172x320 direct packed RGB565 ordered
  dithering. It exists because low-color-depth quality is port-opt-in and must
  be measured separately from RGBA composition and panel/DMA time.
- `text_anywhere_wrap_32`, `text_anywhere_wrap_128`, `text_anywhere_wrap_512`,
  and `text_anywhere_wrap_2048` measure the current UTF-8 scalar wrapping path
  at four text lengths in a narrow column. The corresponding `*_wide_*` probes
  keep the candidate on one line to expose the worst candidate-string
  measurement shape. These results are a baseline, not a performance target,
  and do not authorize a semantic change to font-run measurement.
- `flex_nonwrap_intrinsic_layout` measures a non-wrapping row whose flexible
  children contain text and are stretched on the cross axis. It reports the
  text-measure count for one layout so probe/final/stretch passes remain
  visible while evaluating any intrinsic-size cache. It is a baseline only;
  do not skip a pass unless percentage descendants and cross-axis semantics
  are covered by regression tests.
- `form_select_set_index` measures repeated selected-index updates on a
  256-option select. It covers the common interaction path where option count
  and selected option are needed together; the result is a desktop baseline,
  not a device throughput target.

These probes quantify the remaining cost after text/style layout reuse. They do
not imply display-list diffing or subtree replay.

### Opaque-destination source-over candidate

`a4faa1c6` is the completed-batch timing baseline; `1e375e5b` adds only the
opaque-destination blend specialization and its regression tests. The general
translucent-destination formula is unchanged. All 256^3 single-channel/source-alpha
combinations match the original integer rounding; Debug and Release Core tests pass.

On 2026-09-19, six alternating executable repeats per side, 500 samples per repeat,
using `alpha-grid-rgb-v2` gave JellyFrame median repeat p95 values of 2.171 -> 1.8045
us/tile alongside GDI and 2.162 -> 1.846 alongside SDL2 2.28.2 (forced batching).
These are old/new JellyFrame comparisons, not GDI or SDL2 timings. Each sample is
a completed 64-tile batch divided by 64, not an individual tile tail-latency sample.
All output digests remained `797bc643a0867f83`; the independent RGB oracle passed.
Raw manifests, commands, executable hashes and the hardware handoff are archived
locally at `D:/JellyFramePerf/source-over-opaque-20260919/`. The measured 15-17%
desktop improvement does not establish MCU or complete-UI performance gains.

The WS147 hardware pair completed four embedded-UI workloads with three repeats
per side (24 complete windows). Firmware, sdkconfig and raw-log hashes were
verified; user-confirmed visual inspection and all four non-regression comparisons
passed (`visual-equivalent-only`). Frame p95 stayed unchanged; paint shifts of one
1 ms histogram bucket do not demonstrate significant device acceleration. Evidence
is in the archive's `hardware/` directory. These are repaint/aggregate measurements
(`pipeline_frames=0`, `script_us` missing), not complete UI-pipeline validation.

The accepted cross-library boundary and the deliberate exclusions for rounded
coverage and host-dependent text are recorded in
`../../../docs/render_performance_cpu_workload_matrix_zh.md`. The rounded and text probes
listed above are valid JellyFrame regression baselines; they are not cross-library
claims until an adapter supplies matching coverage, font identity, shaping and
output-validation conditions.

The Windows `jellyframe_cpu2d_compare <directory> 1 rounded-qualification` mode
records the native GDI/Core rounded-coverage mismatch without measuring time.
It validates Core against an independent 4x4 oracle, then writes two captures,
a difference image and a `jellyframe.benchmark.qualification.v0` report. Success
means the diagnostic ran, not that outputs are equivalent. The comparison tool
rejects this report as a timing manifest. This closes the native-GDI qualification
investigation only; an equivalent-AA rounded adapter remains outstanding.

`rounded-supersample-qualification` extends that investigation to eight shapes
using GDI+ native paths at 4x resolution and box reduction. The independent
`cpu2d_rounded_fixture.h` oracle is shared with fixture regression tests, not
with the reference renderer. Qualification compares coverage before RGB rounding;
there are no timing samples, even when all outputs qualify. GDI+ is a dependency
of the Windows benchmark only. See `../../../benchmarks/README.md` for invocation
and the rejected-adapter boundary.
