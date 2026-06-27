# Run Loop And Incremental Update Contract


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
7. Wait for, or certify, present completion before reusing frame buffers.
8. Clear DOM dirty flags.

Loop frame:

1. If a previous display present is still in flight and it still owns the
   framebuffer/target buffer, sleep or process only non-render work that cannot
   touch those buffers.
2. Drain a bounded number of host input events.
3. Dispatch pointer/wheel/key/text/focus operations through `InputController`.
4. Drain a bounded number of host completion events, such as resource, image
   decode, audio state, network response or installation results.
5. If JerryScript is enabled, pump a bounded number of timer callbacks.
6. If animation is active, pump a bounded number of animation frame callbacks.
7. Read root `subtree_dirty_flags(document)`.
8. Fill `FramePipelineCacheState`, call `make_frame_update_state(...)`, then
   call `plan_frame_update(...)` to choose an update path.
9. Reuse existing layout/layers or rebuild the pipeline according to the plan.
10. After layout is known, call `plan_frame_repaint(...)` with the resolved
   content height to confirm whether the existing framebuffer still matches.
11. Generate dirty rectangles with `compute_dirty_rects(...)`, or fall back to a
   full frame.
12. Call `SoftwareCompositor::render_into(...)` or full `render(...)`.
13. Present dirty rectangles through `HostFrameSink`.
14. If the panel driver uses asynchronous DMA, mark present in flight until the
    flush-done event arrives.
15. Clear DOM dirty flags.

Hosts may place these steps in a UI task, desktop message loop or validation
shell, but scripts, layout and rendering should not run inside an ISR or display
flush callback. Async workers also must not run these steps directly; they only
post completion events.

`HostFrameSink::present` is the frame-lifetime boundary. Returning success means
the host has either finished the panel flush, copied pixels into memory owned by
the display driver, or arranged for the UI loop to wait before reusing the same
framebuffer/target buffer. JellyFrame should not run the next render while a
panel DMA transfer is still reading memory that render would overwrite.

## `plan_frame_loop`

Header: `src/render_core/frame_loop.h`

`plan_frame_loop_work(...)` is a tiny helper for host UI tasks. It does not own
an input queue, timer queue or animation queue. The host reports pending input
events, due timer callbacks and pending animation frame callbacks through
`FrameLoopPendingWork`, then receives a bounded
`FrameLoopWorkPlan`:

- `input_events_to_dispatch`: how many host input events to drain this frame.
- `timer_callbacks_to_pump`: how many script timer callbacks to run this frame.
- `animation_callbacks_to_pump`: how many animation frame callbacks to run this
  frame.
- `has_more_input_events` / `has_more_timer_callbacks` /
  `has_more_animation_callbacks`: whether the host should schedule another
  frame or keep the UI task awake.
- `needs_animation_frame`: whether active animation work should request another
  frame at the host's animation cadence.

`FrameLoopOptions` carries the per-frame caps. A zero cap is valid and means the
host deliberately pauses that class of work, for example while throttling a
screen-off device. The helper never drops work; it only tells the host how much
to consume. Hosts can derive these caps from `HostBudgets` with
`frame_loop_options_from_budgets(...)`.
The app runtime also provides `AppFramePolicy`, which maps
foreground/suspended, screen-on and low-power state into these budgets:
low-power may keep input/timers while stopping animation, while screen-off or
suspended state pauses foreground input, timers, rAF and presentation and
recommends a first repaint when the app becomes visible again.
`app_runtime/app_load_telemetry.h` adds an advisory load classifier for hosts
that need DVFS or sleep decisions. Feed it the current `FrameLoopWorkPlan`,
`FrameUpdatePlan`, dirty-region summary, service queue depths and frame policy;
it returns `sleep-ok`, `low-frequency-ok`, `normal`, `boost-needed` or
`overloaded`. The helper only reports load. Real CPU frequency, PM locks,
tickless idle, watchdog feeding and worker scheduling remain host policy.
The Win32 validation shell exposes the animation budget through
`--animation-fps`, `--animation-callbacks` and matching frame-script commands,
so low-power behavior can be tested without changing app source.

Animation caps are separate from timer caps so a page with motion cannot starve
input, network completions or ordinary timers. A no-animation page reports zero
pending animation callbacks and does not request animation frames.

`plan_frame_loop(...)` combines that bounded work plan with
`plan_frame_update(...)` for hosts that want one shared planning call. It still
does not dispatch input, pump timers or animation callbacks, mutate DOM, rebuild
layout or present pixels.

## Async Completion Events

Hosts may run slow work outside the UI task, but results return through a
bounded queue consumed by the frame loop. Typical events include:

- package/resource load finished or failed;
- image decode completed with a surface handle;
- audio playback state changes such as started, ended or error;
- network fetch completed with a bounded byte buffer;
- app bundle install/delete/update result.

Rules:

- Consume at most `HostAsyncCapabilities::max_completion_events_per_frame`
  events per frame.
- Completion events carry small handles, status and error codes; large response
  bodies, pixels and audio buffers stay in the host resource layer.
- If an event affects DOM or JavaScript, dispatch DOM events, resolve
  promise-like callbacks or mark dirty from the UI task.
- If events remain queued, schedule another frame instead of looping
  indefinitely in the current one.
- On app switch, document teardown or system sleep, cancel or isolate jobs that
  belong to the old document.

This is the coordination rule that lets network requests, media decode and
installable third-party apps coexist without giving up a single UI owner.

## `plan_frame_update`

Header: `src/render_core/frame_update.h`

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
- `FrameUpdateReason`: stable diagnostic names for why the planner chose idle,
  repaint or rebuild, including first paint, paint-only dirty, tree dirty,
  missing cache and framebuffer-size mismatch.
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
  back conservatively to the full viewport/content rect. Future retained-subtree
  work can refine this.

Unchanged `textContent`, unchanged attributes and similar no-op mutations should
not create dirty flags. Updating `textContent` on an element that already owns a
single text child should update that text node in place and avoid
`DomDirtyTree`; replacing mixed or multi-child content remains a structural
mutation.

## Dirty Region Diagnostics

Header: `src/render_core/dirty_region.h`

`compute_dirty_rects(...)` remains the simple compatibility API for hosts that
only need rectangles. `compute_dirty_region(...)` returns the same rectangles
plus:

- `DirtyRegionMode::Clean`: no repaint needed.
- `DirtyRegionMode::DirtyRects`: bounded local rectangles were produced.
- `DirtyRegionMode::FullFrame`: the core chose a conservative full-frame
  fallback.

`DirtyRegionFallbackReason` explains why a full-frame fallback happened:
invalid viewport, missing previous/current layout, structural tree dirty,
missing dirty-node bounds or clipping that removed every local rect. This is a
diagnostic contract for hosts and tests; it does not mean retained subtree reuse
is complete.

Embedded run loops should prefer `compute_dirty_region_into(...)`, writing the
result into a long-lived `FrameScratch::dirty_region` and the internal bounds
work area into `FrameScratch::dirty_region_scratch`. That lets dirty rectangles
and dirty-node bounds keep capacity across frames. Call `FrameScratch::release()`
on memory pressure, screen-off or app switches. The older return-by-value API is
kept for simple tools and compatibility call sites.

`dirty_region_mode_name(...)` and `dirty_region_fallback_reason_name(...)`
provide stable short names for shell diagnostics. The Win32 validation shell
uses them in the window title after incremental repaints, so fallback causes can
be seen while interacting with a page.

`DirtyRegionStatistics` can accumulate many `DirtyRegionResult` samples. It
tracks clean frames, dirty-rect frames, full-frame frames, total rect count,
total dirty area and fallback reason counts. This is intended for audits:
measure where full-frame fallback comes from before adding heavier retained
subtree reuse.
Use it together with `FrameUpdateStatistics`: frame-update reasons explain why
a rebuild was chosen, while dirty-region reasons explain why a planned local
repaint still fell back to a full frame.

Hosts can also use `dirty_region_area(...)`,
`dirty_region_area_percent(...)` and
`dirty_region_should_repaint_incrementally(...)` to decide whether local repaint
is still worth doing. The area estimate intentionally sums clipped rectangle
areas without subtracting overlaps. This is a conservative embedded-friendly
cost signal: when the estimate exceeds the host threshold, a full repaint can be
cheaper than issuing many partial flushes. The Win32 validation shell currently
uses a 70% threshold and records `DirtyAreaTooLarge` when it chooses that
full-frame path.

## Display Invalidation Diagnostics

Header: `src/render_core/display_invalidation.h`

`analyze_display_invalidation(...)` reports how a dirty-rectangle set maps onto
the current layer tree and display commands. It counts visited/intersecting
layers, clipped/composited layers and visited/intersecting display commands.
The Win32 validation shell surfaces the latest command coverage as
`cmds=intersecting/visited` in the window title.

This is an audit helper, not retained display-list reuse. The compositor still
replays commands inside each dirty clip. The value is that hosts and tests can
see whether a page interaction genuinely narrows paint work before the project
adds heavier retained layer/display-command structures.

## Animation Invalidation

Header: `src/render_core/animation_invalidation.h`

`compute_animation_dirty_region(...)` /
`compute_animation_dirty_region_into(...)` are for animation frames. They use
the previous and current `StyleOverride` lists, find matching nodes in the
current layout tree, and union the node subtree bounds with previous/current
transform bounds.

This path avoids falling back to full-frame repaint just because an animation
frame marks the root as `DomDirtyPaint`. It is intended for the Track D
paint/compositor property set: `opacity`, `background-color`, `color` and
`transform: translate()/scale()/rotate()`. Layout-property animation still does not
reflow every frame; structural or layout-changing animation should use the
normal dirty-region/full-frame path.

Hosts may set the root aggregate `DomDirtyPaint` bit to schedule an animation
frame, but should not mark the root as a local dirty node for animation-only
work. A local root dirty bound is equivalent to the whole document and will
erase the benefit of animation dirty-region calculation when script/text work
happens in the same frame.

CSS `@keyframes` / `animation-*` uses the same timeline and dirty-region path.
The first supported subset only starts animations from resolved render-tree
styles, samples `from`/`to` declarations for the same paint/compositor property
set, and keeps transition plus keyframe animations under the shared
`max_active_animations` budget. Unsupported keyframe properties are reported
through diagnostics and ignored; they do not trigger per-frame layout.

## Boundaries

The current core still does not implement:

- full retained-layout reuse;
- retained display-command reuse;
- tiled/scanline rendering;
- automatic threading or power policy.

Those belong to future retained-rendering work, optional tiled presentation or
host policy.

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
