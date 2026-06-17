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
5. Fill `FramePipelineCacheState`, call `make_frame_update_state(...)`, then
   call `plan_frame_update(...)` to choose an update path.
6. Reuse existing layout/layers or rebuild the pipeline according to the plan.
7. After layout is known, call `plan_frame_repaint(...)` with the resolved
   content height to confirm whether the existing framebuffer still matches.
8. Generate dirty rectangles with `compute_dirty_rects(...)`, or fall back to a
   full frame.
9. Call `SoftwareCompositor::render_into(...)` or full `render(...)`.
10. Present dirty rectangles through `HostFrameSink`.
11. Clear DOM dirty flags.

Hosts may place these steps in a UI task, desktop message loop or validation
shell, but scripts, layout and rendering should not run inside an ISR or display
flush callback.

## `plan_frame_loop`

Header: `src/core/frame_loop.h`

`plan_frame_loop_work(...)` is a tiny helper for host UI tasks. It does not own
an input queue or a timer queue. The host reports pending input events and due
timer callbacks through `FrameLoopPendingWork`, then receives a bounded
`FrameLoopWorkPlan`:

- `input_events_to_dispatch`: how many host input events to drain this frame.
- `timer_callbacks_to_pump`: how many script timer callbacks to run this frame.
- `has_more_input_events` / `has_more_timer_callbacks`: whether the host should
  schedule another frame or keep the UI task awake.

`FrameLoopOptions` carries the per-frame caps. A zero cap is valid and means the
host deliberately pauses that class of work, for example while throttling a
screen-off device. The helper never drops work; it only tells the host how much
to consume. Hosts can derive these caps from `HostBudgets` with
`frame_loop_options_from_budgets(...)`.

`plan_frame_loop(...)` combines that bounded work plan with
`plan_frame_update(...)` for hosts that want one shared planning call. It still
does not dispatch input, pump timers, mutate DOM, rebuild layout or present
pixels.

## `plan_frame_update`

Header: `src/core/frame_update.h`

`plan_frame_update` does not own the DOM and does not run layout. It only turns
the current cache state and dirty flags into an update strategy.

Hosts should normally build its input through `FramePipelineCacheState` and
`make_frame_update_state(...)`. This keeps render/layout/layer/framebuffer
ownership in the host while making the cache snapshot shape shared by desktop
and embedded integrations.

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

`plan_frame_repaint(...)` is the second-stage check used after a rebuild has
resolved the new layout height. It keeps `PreviousAndCurrentLayout` or
`CurrentLayout` only when the existing framebuffer dimensions still match the
resolved target. If text/style/layout changes make content taller or shorter,
the host must resize or recreate the framebuffer and repaint the full frame.

## Dirty Flag Semantics

- `DomDirtyPaint`: control values, selection state and similar visual-only
  changes. If caches and framebuffer match, this can use `RepaintExisting`.
- `DomDirtyText` / `DomDirtyLayout` / `DomDirtyStyle` / `DomDirtyAttributes`
  without `DomDirtyTree`: rebuild render/layout/layer, but may still use
  `PreviousAndCurrentLayout` dirty rects if framebuffer size is stable.
- `DomDirtyTree`: structural mutation. The planner uses `FullFrame` and does
  not retain the previous layout tree, because current dirty-region logic falls
  back conservatively to the full viewport/content rect. M9 can refine this.

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
  when framebuffer dimensions still match after the new layout is resolved.
- missing caches, dimension changes and invalid viewports conservatively use a
  full frame.
- long-running frame-loop smoke keeps input/timer consumption bounded per frame
  and eventually reaches clean cached idle frames after backlogs drain.
