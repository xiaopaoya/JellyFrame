# Host Abstraction Draft

Date: 2026-06-15

WearWeb's core should stay independent from filesystems, network stacks,
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

These are not wired through the whole engine yet. They document the stable
shape future shell/backends should converge on while existing examples keep
their current callback helpers.

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

Current renderers produce a `FrameBuffer` and desktop tools write BMP/PPM or
blit through Win32/GDI.

`HostFrameSink` should become the embedded presentation boundary:

- `HostFrameBufferView` points to RGBA pixels.
- `dirty_rects` is optional; empty means full-frame present.
- The host maps pixels to display format or DMA/layer hardware.

Next implementation step here is dirty rectangle generation from layer
invalidation. Until then, the software compositor still repaints full frames.

## Text Backend

Current shape:

- `TextPainter` in `software_renderer.h`
- Win32 shell injects GDI text for readable UTF-8/Chinese.
- Core fallback remains tiny and ASCII-oriented.

Future embedded backends can provide:

- bitmap font atlas;
- LVGL text draw bridge;
- vendor font engine;
- shaping-capable text painter for production non-Latin text.

Do not put font loading into the core yet.

## Input Backend

Current shape:

- Host converts native mouse/wheel/key events into `PointerInput`,
  `WheelInput` and `KeyInput`.
- `InputController` performs hit testing, focus, activation and DOM event
  dispatch.

Future wearable input adapters should map:

- touch to pointer down/move/up;
- crown rotation to wheel or app-specific events;
- hardware buttons to focus navigation and activation;
- long press to host-defined commands.

The next missing piece is a small focus/navigation model for button/crown-only
devices.

## Budgets

The host should eventually configure:

- DOM node cap;
- CSS rule/declaration cap;
- display command cap;
- timer and event listener cap;
- resource byte cap;
- framebuffer/offscreen buffer policy.

The current parser has some fixed limits. `HostBudgets` documents the desired
centralized direction.

## Recommended Order

1. Finish M7 script loading in examples and docs.
2. Add dirty rectangle invalidation and plumb it into `HostFrameSink`.
3. Add a deployable embedded framebuffer backend.
4. Add platform text backend examples beyond Win32.
5. Add resource/budget plumbing across parsers and scripting.
