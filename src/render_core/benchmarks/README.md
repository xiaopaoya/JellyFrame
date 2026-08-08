# Render Core Benchmarks

> Last updated: 2026-07-16; Applies to: 0.5.0

Microbenchmarks in this directory measure the platform-neutral render pipeline:
HTML parsing, CSS parsing, style resolution, render tree, layout, layer tree,
display-list flattening and software rendering.

Executable: `jellyframe_render_core_microbench`.

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

These probes quantify the remaining cost after text/style layout reuse. They do
not imply display-list diffing or subtree replay.
