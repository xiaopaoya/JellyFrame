# Host Abstraction Draft


JellyFrame's core should stay independent from filesystems, network stacks,
windowing systems, display controllers, timers, input hardware and font APIs.
The host abstraction is the seam where a desktop shell, RTOS task, LVGL driver
or custom wearable platform supplies those services.

This is intentionally a thin draft, not a full HAL. It records the interfaces
that should harden before a real embedded backend is written.

## Design Rules

- Core owns parsing, DOM, style, layout, layer organization, display-list
  generation, software rasterization and platform-neutral events.
- Host owns resource bytes, wall-clock time, framebuffer presentation, hardware
  input, font/text backend and device capabilities.
- Core APIs should use callbacks and plain structs, not inheritance-heavy
  objects.
- Host callbacks must be optional; missing callbacks degrade cleanly.
- Resource loading remains local/host-provided. No network is implied.
- Budgets must be explicit before tiny-MCU deployment.

## Minimal Interfaces

`src/render_core/host.h` now defines the first small set:

- `HostResourceRequest` and `HostResourceLoadCallback`
- `HostClock`
- `HostFrameBufferView`
- `HostFrameSink`
- `HostBudgets`
- `HostDeviceCapabilities`

These are now lightly wired into presentation: software rendering can expose a
`FrameBuffer` as `HostFrameBufferView`, `present_frame` can call a
`HostFrameSink`, and the pseudo browser uses that path to write its validation
image. Resource loading, clocks, budgets and device capabilities are still
draft-level shapes.

## Device Capabilities

`HostDeviceCapabilities` is a plain description supplied by the board port. The
core does not own hardware state through it; it only records facts that later
policy can use:

- display size, DPI, preferred pixel format, partial-present support and whether
  a full framebuffer fits;
- input sources such as touch, pointer, wheel, crown, focus buttons, keyboard
  and text input;
- heap and maximum single-allocation estimates;
- optional async jobs, media decode/playback, runtime network fetch and
  installable bundle-store capabilities;
- explicit `HostBudgets`;
- whether monotonic time, filesystem and network services exist.

For a typical ESP32-S3 watch target, start with RGB565, partial present enabled,
`has_full_framebuffer` set according to PSRAM/RAM layout, touch or crown flags
matching the board and conservative heap/allocation numbers. Treat filesystem
and network as optional host services, not core assumptions.

## Slow Work And Optional Services

`HostDeviceCapabilities` now splits slow-service facts into four groups:

- `async`: whether the host can run jobs outside the UI task, whether jobs are
  cancellable and how many completion events can be consumed per frame;
- `media`: image decode, audio playback, lightweight video decode and hard
  size/buffer caps;
- `network`: runtime data fetch capability, request/response caps and the
  remote-page-resource switch;
- `app_bundles`: third-party flash bundle installation, integrity checks and
  capacity caps.

These fields do not mean the core calls hardware directly. They are a policy
contract shared by ports, desktop tools, packagers and future JS APIs. If an app
declares `network.fetch` or `media.audio.mp3`, tools can compare that against
the target profile before packaging or installation, and runtime bindings can
decide whether to expose the API.

Recommended execution boundary:

- The UI/main task exclusively owns DOM, JerryScript, style/layout/layer, dirty
  regions and the framebuffer.
- Decode, network, install and file I/O work runs only in host workers, RTOS
  tasks or system services.
- Workers post small completion events back to the UI queue.
- The UI frame loop consumes a bounded number of completion events, then marks
  DOM dirty or dispatches JavaScript events.
- Workers must not hold raw DOM node pointers, call layout/render or write the
  framebuffer.

This keeps installable third-party apps, network requests and audio playback
from blocking the system/app main process.

The ESP32-S3 decode experiments should map into target profiles, not default
core features: MP3 playback and small MJPEG/image decode can be optional host
services; the 2026-06-20 H.264 QEMU + Octal PSRAM retest succeeds, but the
low-resolution baseline sample is still below real-time, so H.264 should remain
explicitly experimental or disabled.

## Resource Loading

Current concrete callbacks:

- CSS: `StylesheetLoadCallback` in `document_style.h`
- Scripts: `ScriptLoadCallback` in `document_script.h`

Future convergence:

```text
HostResourceLoadCallback(kind=Stylesheet or ClassicScript)
  -> bounded byte buffer
  -> parser or JerryScript runtime
```

Rules:

- Relative paths are resolved by the host, not the core.
- Missing resources are ignored conservatively.
- Network, ES modules and dynamic import remain out of scope.
- Resource byte limits come from `HostBudgets::max_resource_bytes`.

## Time And Timers

Current scripting timers are host-pumped. The host decides:

- current time in milliseconds;
- how often to pump timers;
- maximum callbacks per tick;
- whether low-power mode delays callbacks.

`HostClock` is the minimal shape for replacing direct desktop calls such as
`GetTickCount64()` later.

## Framebuffer Presentation

Current renderers produce a `FrameBuffer`. Desktop tools either write BMP/PPM
through a frame sink or blit through Win32/GDI.

`HostFrameSink` should become the embedded presentation boundary:

- `HostFrameBufferView` points to RGBA pixels.
- `dirty_rects` is optional; empty means full-frame present.
- The host maps pixels to display format or DMA/layer hardware.
- A successful return means the frame buffers are safe to reuse. Hosts using
  asynchronous panel DMA must wait, copy into driver-owned memory or keep the UI
  loop from rendering the next frame until flush completion.

`SoftwareCompositor::render_into` can repaint caller-supplied dirty rectangles
into an existing framebuffer. This is the first embedded-oriented presentation
path: a host can keep a persistent framebuffer, ask the core to redraw bounded
regions, then present the same rectangles to the display driver.

`dirty_region` now provides the first automatic rectangle source. It compares
old and new layout boxes for nodes with direct dirty flags and emits bounded
rectangles for text, attribute and form-control changes. Tree mutations remain
conservative and request a full viewport repaint, because removed nodes cannot
be safely addressed through stale layout pointers. This is enough for text
input, range/select state changes and small script updates to avoid full
framebuffer clears in the Win32 validation shell.

`embedded_framebuffer` is the first deployable host-side adapter. It consumes a
`HostFrameBufferView`, converts dirty rectangles into a caller-owned target
buffer and invokes an optional flush callback per rectangle. Supported target
formats are RGBA8888, BGRA8888, RGB565, BGR565, RGB332, Gray8 and 1-bit
monochrome packing. It does not schedule DMA or own flush completion; a real
port must still respect the `HostFrameSink` buffer-lifetime boundary.

Still missing:

- finer-grained layer/display-command invalidation;
- tunable dirty rectangle coalescing policy;
- tiled/scanline presentation for devices that cannot keep a full framebuffer.

## Vendor And LVGL Backend Policy

Keep JellyFrame's main rendering path independent from LVGL and vendor UI widget trees. The core should continue to own HTML/CSS parsing, DOM/style/layout/layer construction, software framebuffer rendering and input event dispatch. A board port may use LVGL or a vendor BSP as a thin adapter for panel initialization, touch/backlight setup, text measurement/painting callbacks or final dirty-rectangle flushes, but it should not map JellyFrame DOM/CSS/layout into an LVGL widget tree. That would create two competing layout, style, focus, event and font systems.

For ESP32-S3, the preferred path is: JellyFrame software `FrameBuffer` -> `embedded_framebuffer` RGB565 conversion -> `flush(Rect)`/`packed_flush(Rect)` -> `esp_lcd_panel_draw_bitmap` or an equivalent panel driver call. Input should flow from board queues into `InputController`; text should flow through `TextMeasureProvider` and `TextPainter`. If a vendor SDK is LVGL-centric, wrap only the final panel/input/text hooks and keep `src/render_core` free of LVGL headers.

## Text Backend

Current shape:

- `TextMeasureProvider` in `text_backend.h`
- `TextPainter` in `software_renderer.h`
- Win32 shell injects GDI measurement and painting for readable UTF-8/Chinese.
- Core fallback remains tiny: UTF-8-aware measurement plus ASCII bitmap painting
  with non-ASCII placeholder glyphs.

Embedded backends can provide:

- generated bitmap font packs for `tiny`, app-specific, `cn-standard` or market-specific profiles;
- LVGL text draw bridge used only as a host text hook;
- vendor font engine;
- shaping-capable text painter for production scripts that need it.

Do not let the render core read or parse arbitrary font files by itself. The
current production path is compile-time bitmap font packs; future high-priority
`.jfapp` dynamic font supplements should still enter through a controlled font
resource provider and the same host text backend. Measurement and painting must
come from the same glyph metrics to avoid clipping and mismatched wrapping. See
`src/render_core/docs/text_backend.md`.

## Input Backend

Current shape:

- Host converts native mouse/wheel/key events into `PointerInput`,
  `WheelInput` and `KeyInput`.
- `InputController` performs hit testing, focus, activation and DOM event
  dispatch.

Future wearable input adapters can map:

- touch to pointer down/move/up;
- crown rotation to wheel or app-specific events;
- hardware buttons to focus navigation and activation;
- long press to host-defined commands.

The next missing piece is a small focus/navigation model for button/crown-only
devices. A first core API now exists on `InputController`: `focus_next()`,
`focus_previous()` and `activate_focused()`.

## Budgets

The host should configure:

- DOM node cap;
- CSS rule/declaration cap;
- display command cap;
- timer count, event listener, detached DOM node and per-frame input/timer
  callback caps;
- resource byte cap;
- framebuffer/offscreen buffer policy.

`HostBudgets` is now wired through `src/render_core/budget.h` into the main HTML/CSS
parser, render/layout/layer, display-list, dirty-rectangle and frame-loop work
caps. JerryScript runtime construction also consumes timer, listener and
detached DOM node limits. The software compositor also accepts primary and
offscreen pixel caps derived from the framebuffer budget. The remaining work is tile/scanline
presentation policy and long-running stability tests for budget-exceeded paths.

## Recommended Order

1. Organize local resource bundles and app packaging.
2. Continue allocator work and use dirty-region diagnostics to decide whether
   retained subtree reuse is worth its ownership cost.
3. Refine tile/scanline presentation policy when real hardware pressure requires it.
