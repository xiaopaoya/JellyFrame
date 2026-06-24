# App Load Telemetry

`AppLoadTelemetry` is a small hardware-neutral summary for host scheduling,
DVFS and low-power decisions. It does not read clocks, start threads, touch
drivers or change the frame loop. The host feeds it facts it already knows for
the current frame, then maps the result to product policy.

Header: `src/app_runtime/app_load_telemetry.h`

## Inputs

`AppLoadTelemetryInput` combines existing JellyFrame contracts:

- `AppFramePolicy`: whether this app currently accepts input, pumps timers,
  pumps animation and presents frames.
- `AppServiceActivityPolicy`: whether network/audio/sensor/location activity is allowed.
- `FrameLoopWorkPlan`: bounded input/timer/rAF work for this frame.
- `FrameUpdatePlan`: whether the render pipeline is idle, repainting or
  rebuilding.
- dirty-region summary: either a `DirtyRegionResult`, or scalar
  `dirty_region_mode` plus `dirty_area_percent` when a host already recorded
  that data.
- service request/completion queue depth and capacity.
- active animation count and `present_pending` state.

The helper does not allocate large buffers and does not inspect DOM, JS,
framebuffer pixels or hardware state.

## Levels

`analyze_app_load(...)` returns:

| Level | Meaning |
| --- | --- |
| `sleep-ok` | Nothing visible or service-related is pending. The host may enter idle or shallow sleep if external wake sources allow it. |
| `low-frequency-ok` | The app is visible or alive but has no active animation, no backlog and no frame rebuild. A lower CPU frequency should be acceptable. |
| `normal` | Ordinary UI work is present but bounded. |
| `boost-needed` | Full rebuild, large dirty area, queue pressure or callback backlog suggests a temporary higher frequency or immediate scheduling. |
| `overloaded` | Heavy repaint/rebuild combines with backlog or present pressure. Hosts should consider dropping animation frames, reducing animation FPS, or deferring non-critical work. |

The boolean fields (`sleep_ok`, `low_frequency_ok`, `boost_recommended` and
`drop_animation_frame_recommended`) are convenience flags for small ports that
do not want to switch on the enum.

## Host Guidance

Suggested mapping:

- `sleep-ok`: allow tickless idle/shallow sleep, release display or frequency
  locks if the panel and audio policy allow it.
- `low-frequency-ok`: keep the UI task alive at a lower frequency.
- `normal`: use the product's default interactive frequency.
- `boost-needed`: acquire a short PM/frequency lock for the current frame or
  service pump.
- `overloaded`: keep the UI task responsive first; reduce animation budget or
  defer background service work before allowing unbounded frame time.

`present_pending` should normally be true only when a host frame sink still owns
the buffers needed by the next render. In that state, the UI task should wait
for flush completion or do non-render work that does not touch those buffers.

## Boundaries

This is advisory telemetry. The core reports load; the host owns:

- real CPU frequency changes;
- light/deep sleep entry;
- PM locks;
- task priorities;
- watchdog feeding;
- worker scheduling;
- panel DMA or flush-complete gating.

The helper is intentionally conservative. It is better to report
`boost-needed` too early than to hide a full-frame rebuild or backed-up service
queue from the product scheduler.
