# Embedded HAL API


This document is the implementation checklist for a board or RTOS host such as
ESP32-S3. `jellyframe_render_core` stays platform-neutral; the hardware program owns every
real I/O operation and calls the core through small C++ structs and callbacks.

See `host_optional_services.md` for the concrete optional-service shapes for
images, audio, lightweight video, network data requests and installable bundles.
This document keeps the overall checklist and porting guidance.

## Required Runtime Loop

A minimal embedded host should perform this loop:

1. Load HTML/CSS/script bytes from flash, partition, ROM bundle or host storage.
2. Build DOM, style, render, layout and layer trees.
3. Render into a persistent `FrameBuffer`.
4. Present dirty rectangles through `HostFrameSink` or `embedded_framebuffer`;
   this is the display synchronization boundary for the frame.
5. Convert hardware input into `InputController` calls.
6. Pump JerryScript timers if scripting is enabled.
7. If DOM dirty flags changed, rebuild the simplified pipeline and repaint.

JellyFrame should not treat panel refresh as an unrelated background process.
Rendering a frame, submitting dirty rectangles and waiting for the display
driver/DMA to finish or safely take ownership of the buffers are one frame
transaction. A host may use asynchronous LCD DMA, but while DMA is reading a
framebuffer or conversion buffer, the UI task must not start the next frame and
write the same memory. Valid implementation strategies are:

- block inside `present()` until the panel driver flush completes, which is the
  simplest path for low frame rates or small dirty rectangles;
- copy pixels into a driver-owned DMA buffer, then return once JellyFrame's
  buffers are reusable;
- keep `present_in_flight` in the UI task and only render the next frame after
  the panel driver's flush-done callback.

This mirrors LVGL's display flush lifetime rule: the framework may submit work
asynchronously, but draw buffers are reusable only after the driver reports
ready/completion.

## Device Capability API

Header: `src/render_core/host.h`

```cpp
struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
    HostAsyncCapabilities async;
    HostMediaCapabilities media;
    HostNetworkCapabilities network;
    HostAppBundleCapabilities app_bundles;
    HostBudgets budgets;
    bool has_monotonic_clock;
    bool has_filesystem;
    bool has_network;
};
```

The board port should fill this once during bring-up and keep it available to
the app host. Current fields are descriptive, not a mandatory runtime registry:

- `display`: width, height, DPI, preferred pixel format, partial-present support
  and whether a complete framebuffer fits in memory;
- `input`: touch, pointer, wheel, crown, focus buttons, keyboard and UTF-8 text
  input availability;
- `memory`: total heap, maximum single allocation and preferred framebuffer
  budget;
- `async`: whether slow decode/network/install work can run outside the UI task
  and how many completion events may be consumed per frame;
- `media`: optional image/audio/lightweight-video services, preferred decoded
  pixel formats and hard size/buffer caps;
- `network`: optional runtime data fetch service, request/response caps and
  whether remote page resources are allowed;
- `app_bundles`: optional third-party flash bundle installation capability,
  integrity checks and installed-app/bundle-size caps;
- `budgets`: DOM/CSS/display-list/timer/listener/resource limits;
- service flags for monotonic time, filesystem and network.

Suggested ESP32-S3 watch starting point:

- `preferred_pixel_format = HostPixelFormat::Rgb565`;
- `supports_partial_present = true`;
- `has_full_framebuffer = true` only when RAM/PSRAM can hold the selected target
  buffer plus core working memory;
- set either `touch`, `crown`/`focus_buttons`, or both according to the board;
- leave `has_network = false` unless the product host explicitly exposes a
  bounded network/data layer to apps;
- even when network is enabled, keep `network.allows_remote_page_resources =
  false` and expose only runtime app data APIs;
- expose MP3, small-image and MJPEG services as optional host capabilities;
  keep H.264 or high-resolution video disabled by default, enabling it only
  when the target profile explicitly marks it experimental.

## Async Work API

The JellyFrame UI/main task owns DOM, script, layout, layers and the framebuffer.
Potentially blocking work must not run synchronously inside that task:

- flash directory scans, large resource reads and third-party app installation;
- network requests, DNS/TLS and HTTP body reads;
- image, audio and video decoding;
- large font/resource-table validation.

Recommended host shape:

```text
submit(kind, request, budget, priority) -> job_id
cancel(job_id)
pump_completions(max_events) -> completion events
```

Completion events are consumed only on UI/main-task frame boundaries. They carry
a job id, status, resource handle or error code. Worker tasks must never mutate
DOM, run JavaScript, rebuild layout or write the framebuffer directly. This lets
third-party apps request data, play audio or install packages without stalling
the system shell or the active page.

Request, completion and surface/audio/fetch/bundle handle lifetimes are detailed
in `host_optional_services.md`.

Minimal ESP32-S3 policy:

- one low-priority worker task for image decode, package checks and small file
  I/O;
- host-owned audio pipelines; UI receives playback handles and state events;
- network request task with max concurrency, max response bytes and timeouts;
- consume only a few completion events per frame, for example 2-4.

## Media And Decode API

The ESP32-S3 decode experiments support a conservative optional-service design:

- The GMF MP3 QEMU bench reports roughly 27x real-time with stable heap
  headroom, so audio playback is reasonable as an optional host service.
- The GMF video bench is MJPEG -> RGB565, not H.264. A 240x240, 30-frame sample
  reports roughly 46-49fps with about 115 KiB decoded output per frame. This is
  useful evidence for small image/lightweight animation decode.
- The 2026-06-20 H.264 retest succeeds with ESP32-S3 QEMU 9.2.2 and the Octal
  PSRAM option: a 320x192 baseline, 4-frame sample reports about 14.7-16.5fps
  across 8/16/32MB PSRAM, roughly 0.49-0.55x real-time. This proves an
  experimental path, not a default real-time video capability.

Current recommendation:

- **Image decode**: optional. Input comes from local packages or future bundles;
  output is an RGB565/RGBA surface handle. Enforce width, height, decoded-byte
  and concurrency caps. Resize or reject large images during packaging.
- **Audio playback**: optional. Core/JS controls play/pause/stop/volume and
  receives ended/error events; PCM buffers, I2S, codecs and GMF/ADF pipelines
  remain host-owned.
- **Video decode**: experimental. The first useful shape is a low-resolution
  MJPEG frame provider. H.264 may be exposed only behind a `supports_h264`
  target profile as optional frame decode; do not promise `<video>` or make it
  required for normal layout. H.264 is not in the default ESP32-S3 profile.
- **Images in pages**: before image decode is wired, `<img>` and CSS backgrounds
  may use placeholder boxes. Once wired, decoded surfaces can replace them on a
  later dirty repaint.

Decoded surfaces must be owned by the host resource/cache layer. The UI task may
reference handles and request dirty repaint; large pixel buffers should not be
copied into DOM or JavaScript objects.

## Network And Installation API

Package resource loading remains local, deterministic and remote-resource-free.
When network support exists, it means runtime app data requests only:

- allowed: weather APIs, account/device services, small JSON sync and downloads
  of app bundles for the system installer to verify;
- blocked: loading remote HTML/CSS/script/image resources into the page loader,
  unless a future security model explicitly enables that.

Third-party flash bundle installation should be owned by the system shell or app
manager, not directly mounted by the active app's JavaScript:

1. Download or receive `.jfapp` into a staging area.
2. Verify manifest, version, target device, budgets and hash/signature.
3. Write the bundle store outside the active app context.
4. Atomically commit the resource-table index.
5. Notify the launcher to refresh the app list.

Install, delete and update operations must be cancellable or recoverable. A
failure must not corrupt the currently bootable app table.

## Resource API

Header: `src/render_core/host.h`

```cpp
using HostResourceLoadCallback = bool (*)(const HostResourceRequest& request,
                                          std::string& output,
                                          void* context);
```

The host should support:

- `HostResourceKind::Stylesheet`
- `HostResourceKind::ClassicScript`
- later: `Image`, `Font`, `Other`

ESP32-S3 mapping:

- Resolve `url` relative to `base_url`.
- Read from flash, LittleFS/FATFS, embedded arrays or OTA partition.
- Enforce `HostBudgets::max_resource_bytes`.
- Return `false` for missing optional resources.

No network is required by the core.

## Clock And Timers

Header: `src/render_core/host.h`

```cpp
struct HostClock {
    std::uint64_t (*now_ms)(void* context);
    void* context;
};
```

The host owns wall-clock or monotonic time. JerryScript timers are host-pumped:

```cpp
runtime.set_host_time_ms(now_ms);
runtime.pump_timers(now_ms, max_callbacks);
```

ESP32-S3 mapping:

- use `esp_timer_get_time() / 1000`, FreeRTOS tick conversion, or a low-power
  monotonic counter;
- cap callbacks per frame to avoid UI stalls;
- delay timer pumping during low-power display sleep if needed.

## Display API

Headers:

- `src/render_core/software_renderer.h`
- `src/render_core/host.h`
- `src/render_core/embedded_framebuffer.h`

Core framebuffer view:

```cpp
struct HostFrameBufferView {
    int width;
    int height;
    int stride_pixels;
    const Color* pixels; // RGBA8888 logical pixels
};

struct HostFrameSink {
    bool (*present)(const HostFrameBufferView& frame,
                    const Rect* dirty_rects,
                    std::size_t dirty_rect_count,
                    void* context);
    void* context;
};
```

Provided adapter:

```cpp
EmbeddedFrameBufferSink sink;
HostFrameSink frame_sink = embedded_frame_sink(sink);
```

Supported target formats:

- `Rgba8888`
- `Bgra8888`
- `Rgb565`
- `Bgr565`
- `Rgb332`
- `Gray8`
- `Mono1Msb`
- `Mono1Lsb`

ESP32-S3 mapping:

- Prefer RGB565 for TFT/OLED displays.
- Keep one persistent target buffer if RAM allows.
- Use dirty rectangles to limit SPI/8080/DMA flushes.
- If no full framebuffer fits, implement a tiled host sink later; the current
  adapter expects a caller-owned target buffer.

Required host callbacks:

```cpp
using EmbeddedFlushCallback = bool (*)(Rect dirty_rect, void* context);
```

`flush` should send only the dirty rectangle to the panel driver.

`HostFrameSink::present` return value is a buffer-lifetime promise. Returning
`true` means the caller may render into, or convert from, the same frame-related
buffers again. If the SPI/8080/RGB panel path uses asynchronous DMA, the host
must either wait in `present`, copy into a DMA-owned buffer that will not be
overwritten by the next frame, or keep the outer frame loop from starting
another render until the flush-done event arrives. Do not let the render task
and panel DMA access the same in-flight RGB565/framebuffer memory.

`embedded_framebuffer` is a convenience adapter. It converts dirty rectangles
from the RGBA8888 `FrameBuffer` into the full caller-provided target buffer and
then calls `flush(Rect)`. It does not allocate, retain memory, schedule DMA or
know when a device flush is complete. Therefore:

- do not keep a full-screen RGB565 target in internal RAM unless heap
  watermark measurements prove it is safe;
- if PSRAM is display-DMA capable, keep the persistent RGB565 target in PSRAM
  and avoid writing it until flush completion;
- if the display can read only internal DMA-capable RAM, implement a custom
  `HostFrameSink` that converts dirty rectangles by strip/tile into a small
  internal DMA scratch buffer, submits it, waits for that strip to finish, then
  reuses the scratch buffer;
- if even the RGBA framebuffer does not fit, the current core is not enough yet;
  add a tiled/scanline compositor first.

LVGL is optional here. It is reasonable to reuse LVGL or a vendor BSP to
initialize the panel, touch controller or backlight, or to forward a dirty
rectangle into a vendor flush primitive. It is not recommended to translate
JellyFrame nodes into LVGL widgets as the primary renderer. Keep the core
pipeline and framebuffer path authoritative, then adapt only the final I/O
hooks.

## Input API

Header: `src/render_core/input.h`

Touch or pointer:

```cpp
input.pointer_move(PointerInput{x, y, ...});
input.pointer_down(PointerInput{x, y, Primary, buttons});
input.pointer_up(PointerInput{x, y, Primary, buttons});
```

Wheel or crown:

```cpp
input.wheel(WheelInput{x, y, delta_x, delta_y});
```

Text and keys:

```cpp
input.text_input("UTF-8");
input.key_down(KeyInput{KeyCode::Backspace});
```

Button/crown-only focus navigation:

```cpp
input.focus_next();
input.focus_previous();
input.activate_focused();
```

ESP32-S3 mapping:

- Capacitive touch: pointer down/move/up in screen coordinates.
- Rotary crown clockwise/counter-clockwise: `focus_next()` /
  `focus_previous()` for app controls, or `wheel()` for scrollable pages.
- Crown press or hardware OK: `activate_focused()`.
- Back button: app-defined navigation or `KeyCode::Backspace` for text fields.
- On-screen keyboard/IME: call `text_input()` with UTF-8.

The core performs hit testing, focus, activation, DOM event dispatch and basic
form-control state updates.

## Text API

Headers:

- `src/render_core/text_backend.h`
- `src/render_core/software_renderer.h`

Measurement:

```cpp
using TextMeasureCallback = bool (*)(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     TextMetrics* metrics,
                                     void* context);
```

The optional app-font path may additionally provide `TextMeasureFamilyCallback`
through `TextMeasureProvider::measure_family`. It receives the same inputs plus
a normalized manifest `font-family` hash. Ports that use one fixed system font
can leave it null.

Painting:

```cpp
using TextPaintCallback = bool (*)(FrameBuffer& target,
                                   Rect rect,
                                   Color color,
                                   const std::string& text,
                                   int font_size,
                                   int font_weight,
                                   TextCommandAlign align,
                                   bool single_line,
                                   void* context);
```

The optional companion `TextPaintFamilyCallback` mirrors this signature and adds
the same normalized family hash. If it is absent, rendering falls back to the
classic callback.

ESP32-S3 mapping:

- Use one static embedded font pack for production UI.
- Generate the pack at build time from a licensed vector font.
- Measure and paint from the same glyph metrics.
- Avoid heap allocation inside callbacks.
- For Chinese products, subset common app characters and verify coverage with
  `jellyframe_font_resource_check --font-coverage`. Use the font resource checker
  profile output to choose between `tiny`, app-specific Chinese subsets,
  `cn-standard` or market-specific global packs. `cn-standard` means ASCII +
  common symbols + GB2312 level-1 Chinese; it is a Chinese-market preset, not a
  global default.

Vector fonts are feasible on high-end targets, but the default ESP32-S3 path
should be offline-rasterized bitmap glyphs.

Core now provides `src/render_core/bitmap_font.h` for this default path. A board port
can expose generated glyph arrays through `BitmapFont`, then wire
`bitmap_font_measure_callback` into `LayoutEngine` and
`bitmap_font_paint_callback` into `SoftwareCompositor`.

The desktop generator accepts BDF input:

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font --coverage-bits 1
```

Use `--coverage-bits 2` or `--coverage-bits 4` only when the product profile can
spend the extra glyph row storage for font-level antialiasing. This remains an
offline bitmap path; do not add MCU-side vector rasterization to the default
HAL.

Use `jellyframe_font_resource_check --emit-used-chars` first, then generate a BDF
from the licensed source font with your preferred offline toolchain.

## Platform-Neutral Bring-Up Demo

`ports/embedded_host_demo` is the current core-side reference for a board
bring-up. It intentionally avoids Win32, files, networking and hardware I/O:

- static HTML and CSS are compiled into the executable;
- `BitmapFont` supplies measurement and painting callbacks;
- `InputController::focus_next()` and `activate_focused()` exercise
  button/crown-style interaction;
- `embedded_frame_sink()` converts the software RGBA frame into RGB565;
- a tiny `flush(Rect)` callback records the dirty area a panel driver would
  receive.

Run it on desktop:

```text
ports/embedded_host_demo
```

The output should report one flush, one button click, a checked checkbox, a
changed select value and non-zero foreground pixels. A board port should keep
the same shape, replacing only the static resources, font pack, input source
and framebuffer flush callback.

## Budgets

Header: `src/render_core/host.h`

```cpp
struct HostBudgets {
    std::size_t max_dom_nodes;
    std::size_t max_dom_depth;
    std::size_t max_attributes_per_element;
    std::size_t max_css_rules;
    std::size_t max_css_declarations_per_rule;
    std::size_t max_render_objects;
    std::size_t max_layout_boxes;
    std::size_t max_layers;
    std::size_t max_display_commands;
    std::size_t max_dirty_rects;
    std::size_t max_timers;
    std::size_t max_detached_dom_nodes;
    std::size_t max_input_events_per_frame;
    std::size_t max_timer_callbacks_per_frame;
    std::size_t max_animation_callbacks_per_frame;
    std::size_t max_active_animations;
    std::size_t animation_frame_rate;
    std::size_t max_event_listeners;
    std::size_t max_resource_bytes;
    std::size_t max_framebuffer_pixels;
};
```

`src/render_core/budget.h` maps these values into the current HTML/CSS/parser,
render/layout/layer/display-list, dirty-rectangle and frame-loop entry points.
JerryScript runtime construction also consumes timer, listener and detached DOM
node limits. Suggested ESP32-S3 starting point:

- DOM nodes: 512-1500
- DOM depth: 32-64
- attributes per element: 16-32
- CSS rules: 256-1024
- declarations per rule: 64-128
- render/layout boxes: usually match DOM node budget
- layers: 64-256
- display commands: 1024-4096
- dirty rects: 4-16
- timers: 16-32
- animation callbacks per frame: 0-4, depending on product profile
- active animations: 0-16
- animation frame rate: 0, 15 or 30 Hz depending on power policy
- event listeners: 128-256
- script execution checks: finite in product builds; use `JERRY_VM_HALT=ON`
  JerryScript libraries so runaway app code can be interrupted
- single resource: 64-256 KiB
- framebuffer pixels: physical screen area, or smaller if using tiled output

## Diagnostics API

The core does not require logging yet, but a board port should provide:

- panic/assert output;
- frame timing counters;
- maximum heap watermark;
- dirty rectangle count/area;
- resource load failures;
- script exception and watchdog-interrupt reporting if JerryScript is enabled.

Keep diagnostics optional at runtime so release builds can compile them out.

## Minimum ESP32-S3 Bring-Up Set

Implement these first:

- resource loader from flash or embedded arrays;
- monotonic `now_ms`;
- RGB565 framebuffer target and panel `flush(Rect)`;
- touch pointer down/up/move;
- hardware button mapped to `activate_focused`;
- optional crown/encoder mapped to `focus_next/focus_previous`;
- bitmap font measurement and painting for ASCII, digits, symbols and selected
  Chinese subset.

After that, add:

- script timer pumping;
- text input method;
- dirty rectangle coalescing policy;
- sleep/wake display policy;
- build-time font subsetting pipeline.
