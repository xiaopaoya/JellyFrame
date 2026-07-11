# Render Core Benchmarks

> Last updated: 2026-07-12; Applies to: 0.5.0-dev

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
  It exercises `StyleResolveContext` custom-property caching.
- `style_resolve` measures the equivalent batched resolver path for a normal
  page with no custom properties. It guards the invariant that an unused
  custom-property feature does not allocate per-node cache entries.
- `dirty_rect_replay_contained` measures software compositor replay when dirty
  rectangles contain duplicates or nested rectangles. The compositor normalizes
  those rectangles before clearing and replaying commands.

These probes quantify the remaining cost after text/style layout reuse. They do
not imply display-list diffing or subtree replay.
