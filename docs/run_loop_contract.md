# Run Loop And Incremental Update Contract

Date: 2026-06-16

This document defines JellyFrame's recommended host run loop. It covers only the
hardware-neutral contract: how input, timers, dirty flags, rebuilds, repaint and
presentation are ordered. Threads, ISRs, display drivers, power management and
RTOS scheduling remain host responsibilities.

## Recommended Order

First paint:

1. Load local resources.
2. Parse HTML/CSS.
3. Optionally bind and execute classic scripts.
4. Build the render tree, layout tree and layer tree.
5. Render the full framebuffer.
6. Present a full dirty rect through `HostFrameSink` or `embedded_framebuffer`.
7. Clear DOM dirty flags.

Loop frame:

1. Drain a bounded number of host input events.
2. Dispatch pointer/wheel/key/text/focus operations through `InputController`.
3. If JerryScript is enabled, pump a bounded number of timer callbacks.
4. Read root `subtree_dirty_flags(document)`.
5. Call `plan_frame_update(...)` to choose an update path.
6. Reuse existing layout/layers or rebuild the pipeline according to the plan.
7. Generate dirty rectangles with `compute_dirty_rects(...)`, or fall back to a
   full frame.
8. Call `SoftwareCompositor::render_into(...)` or full `render(...)`.
9. Present dirty rectangles through `HostFrameSink`.
10. Clear DOM dirty flags.

Hosts may place these steps in a UI task, desktop message loop or validation
shell, but scripts, layout and rendering should not run inside an ISR or display
flush callback.

## `plan_frame_update`

Header: `src/core/frame_update.h`

`plan_frame_update` does not own the DOM and does not run layout. It only turns
the current cache state and dirty flags into an update strategy.

Inputs:

- `dirty_flags`: aggregated dirty flags from the root node.
- `has_render_tree` / `has_layout_tree` / `has_layer_tree`: whether reusable
  pipeline caches exist.
- `has_framebuffer`, `framebuffer_width`, `framebuffer_height`: whether a
  matching framebuffer exists.
- `viewport`: current visible area.
- `content_height`: current or estimated content height; target framebuffer
  height is at least the viewport height.

Outputs:

- `FrameUpdateAction::None`: cache is complete and no dirty work exists.
- `FrameUpdateAction::RepaintExisting`: reuse render/layout, rebuild layers and
  repaint dirty rects from the current layout.
- `FrameUpdateAction::RebuildPipeline`: rebuild render/layout/layer.
- `FrameDirtyRectMode::CurrentLayout`: dirty rects can be computed from the
  current layout, useful for paint-only changes.
- `FrameDirtyRectMode::PreviousAndCurrentLayout`: compare old and new layouts
  after rebuild, useful for text/style/layout changes.
- `FrameDirtyRectMode::FullFrame`: use a conservative full-frame render when
  caches are missing, dimensions changed or the viewport is invalid.

## Dirty Flag Semantics

- `DomDirtyPaint`: control values, selection state and similar visual-only
  changes. If caches and framebuffer match, this can use `RepaintExisting`.
- `DomDirtyText` / `DomDirtyLayout` / `DomDirtyStyle` / `DomDirtyAttributes`:
  rebuild render/layout/layer, but may still use `PreviousAndCurrentLayout`
  dirty rects if framebuffer size is stable.
- `DomDirtyTree`: structural mutation. Current `compute_dirty_rects` falls back
  conservatively to the full viewport/content rect. M9 can refine this.

Unchanged `textContent`, unchanged attributes and similar no-op mutations should
not create dirty flags.

## Boundaries

M8 does not implement:

- full retained-layout reuse;
- display-command-level invalidation;
- tiled/scanline rendering;
- automatic threading or power policy.

Those belong to M9, M13 or host policy.

## Acceptance

- clean + cached frame performs no work.
- clean + uncached document triggers first-paint full render.
- paint-only dirty reuses render/layout when caches and framebuffer dimensions
  match.
- layout/style/text dirty can compare old/new layout for incremental repaint
  when framebuffer dimensions match.
- missing caches, dimension changes and invalid viewports conservatively use a
  full frame.
