# Host Abstraction Draft

Date: 2026-06-15

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

`src/core/host.h` now defines the first small set:

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
- explicit `HostBudgets`;
- whether monotonic time, filesystem and network services exist.

For a typical ESP32-S3 watch target, start with RGB565, partial present enabled,
`has_full_framebuffer` set according to PSRAM/RAM layout, touch or crown flags
matching the board and conservative heap/allocation numbers. Treat filesystem
and network as optional host services, not core assumptions.

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
monochrome packing.

Still missing:

- finer-grained layer/display-command invalidation;
- tunable dirty rectangle coalescing policy;
- tiled/scanline presentation for devices that cannot keep a full framebuffer.

## Vendor And LVGL Backend Policy

Keep JellyFrame's main rendering path independent from LVGL and vendor UI widget trees. The core should continue to own HTML/CSS parsing, DOM/style/layout/layer construction, software framebuffer rendering and input event dispatch. A board port may use LVGL or a vendor BSP as a thin adapter for panel initialization, touch/backlight setup, text measurement/painting callbacks or final dirty-rectangle flushes, but it should not map JellyFrame DOM/CSS/layout into an LVGL widget tree. That would create two competing layout, style, focus, event and font systems.

For ESP32-S3, the preferred path is: JellyFrame software `FrameBuffer` -> `embedded_framebuffer` RGB565 conversion -> `flush(Rect)`/`packed_flush(Rect)` -> `esp_lcd_panel_draw_bitmap` or an equivalent panel driver call. Input should flow from board queues into `InputController`; text should flow through `TextMeasureProvider` and `TextPainter`. If a vendor SDK is LVGL-centric, wrap only the final panel/input/text hooks and keep `src/core` free of LVGL headers.

## Text Backend

Current shape:

- `TextMeasureProvider` in `text_backend.h`
- `TextPainter` in `software_renderer.h`
- Win32 shell injects GDI measurement and painting for readable UTF-8/Chinese.
- Core fallback remains tiny: UTF-8-aware measurement plus ASCII bitmap painting
  with non-ASCII placeholder glyphs.

Future embedded backends can provide:

- bitmap font atlas;
- LVGL text draw bridge;
- vendor font engine;
- shaping-capable text painter for production non-Latin text.

Do not put font loading into the core yet. Measurement and painting should come
from the same host font engine to avoid clipping and mismatched wrapping. See
`docs/text_backend.md`.

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
- timer count, event listener and per-frame input/timer callback caps;
- resource byte cap;
- framebuffer/offscreen buffer policy.

`HostBudgets` is now wired through `src/core/budget.h` into the main HTML/CSS
parser, render/layout/layer, display-list, dirty-rectangle and scripting entry
points, plus the M8 frame-loop work caps. The remaining work is finer
offscreen/tile buffer policy and long-running stability tests for
budget-exceeded paths.

## Recommended Order

1. Improve text backend adapters and font workflow validation.
2. Organize local resource bundles and app packaging.
3. Continue allocator work and use M9 diagnostics to decide whether retained
   subtree reuse is worth its ownership cost.
4. Refine offscreen/tile buffer policy when real hardware pressure requires it.
