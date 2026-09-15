# Render Core Benchmarks

> Last updated: 2026-09-15; Applies to: 0.6.0-dev

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

```powershell
jellyframe_cpu2d_compare <output-directory> 100 opaque-fill
jellyframe_cpu2d_compare <output-directory> 100 horizontal-gradient
jellyframe_cpu2d_compare <output-directory> 100 vertical-gradient
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
