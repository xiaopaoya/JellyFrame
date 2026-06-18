# Embedded HAL API


This document is the implementation checklist for a board or RTOS host such as
ESP32-S3. `jellyframe_core` stays platform-neutral; the hardware program owns every
real I/O operation and calls the core through small C++ structs and callbacks.

## Required Runtime Loop

A minimal embedded host should perform this loop:

1. Load HTML/CSS/script bytes from flash, partition, ROM bundle or host storage.
2. Build DOM, style, render, layout and layer trees.
3. Render into a persistent `FrameBuffer`.
4. Present dirty rectangles through `HostFrameSink` or `embedded_framebuffer`.
5. Convert hardware input into `InputController` calls.
6. Pump JerryScript timers if scripting is enabled.
7. If DOM dirty flags changed, rebuild the simplified pipeline and repaint.

## Device Capability API

Header: `src/core/host.h`

```cpp
struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
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
- `budgets`: DOM/CSS/display-list/timer/listener/resource limits;
- service flags for monotonic time, filesystem and network.

Suggested ESP32-S3 watch starting point:

- `preferred_pixel_format = HostPixelFormat::Rgb565`;
- `supports_partial_present = true`;
- `has_full_framebuffer = true` only when RAM/PSRAM can hold the selected target
  buffer plus core working memory;
- set either `touch`, `crown`/`focus_buttons`, or both according to the board;
- leave `has_network = false` unless the product host explicitly exposes a
  bounded network/data layer to apps.

## Resource API

Header: `src/core/host.h`

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

Header: `src/core/host.h`

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

- `src/core/software_renderer.h`
- `src/core/host.h`
- `src/core/embedded_framebuffer.h`

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

LVGL is optional here. It is reasonable to reuse LVGL or a vendor BSP to
initialize the panel, touch controller or backlight, or to forward a dirty
rectangle into a vendor flush primitive. It is not recommended to translate
JellyFrame nodes into LVGL widgets as the primary renderer. Keep the core
pipeline and framebuffer path authoritative, then adapt only the final I/O
hooks.

## Input API

Header: `src/core/input.h`

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

- `src/core/text_backend.h`
- `src/core/software_renderer.h`

Measurement:

```cpp
using TextMeasureCallback = bool (*)(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     TextMetrics* metrics,
                                     void* context);
```

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

ESP32-S3 mapping:

- Use one static embedded font pack for production UI.
- Generate the pack at build time from a licensed vector font.
- Measure and paint from the same glyph metrics.
- Avoid heap allocation inside callbacks.
- For Chinese products, subset common app characters and verify coverage with
  `jellyframe_capability_check --font-coverage`. Use the capability checker
  profile output to choose between `tiny`, app-specific Chinese subsets,
  `cn-standard` or market-specific global packs. `cn-standard` means ASCII +
  common symbols + GB2312 level-1 Chinese; it is a Chinese-market preset, not a
  global default.

Vector fonts are feasible on high-end targets, but the default ESP32-S3 path
should be offline-rasterized bitmap glyphs.

Core now provides `src/core/bitmap_font.h` for this default path. A board port
can expose generated glyph arrays through `BitmapFont`, then wire
`bitmap_font_measure_callback` into `LayoutEngine` and
`bitmap_font_paint_callback` into `SoftwareCompositor`.

The desktop generator accepts BDF input:

```text
jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
```

Use `jellyframe_capability_check --emit-used-chars` first, then generate a BDF
from the licensed source font with your preferred offline toolchain.

## Platform-Neutral Bring-Up Demo

`jellyframe_embedded_host_demo` is the current core-side reference for a board
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
jellyframe_embedded_host_demo
```

The output should report one flush, one button click, a checked checkbox, a
changed select value and non-zero foreground pixels. A board port should keep
the same shape, replacing only the static resources, font pack, input source
and framebuffer flush callback.

## Budgets

Header: `src/core/host.h`

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
    std::size_t max_input_events_per_frame;
    std::size_t max_timer_callbacks_per_frame;
    std::size_t max_event_listeners;
    std::size_t max_resource_bytes;
    std::size_t max_framebuffer_pixels;
};
```

`src/core/budget.h` maps these values into the current HTML/CSS/parser,
render/layout/layer/display-list, dirty-rectangle, frame-loop and JerryScript timer/listener
entry points. Suggested ESP32-S3 starting point:

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
- event listeners: 128-256
- single resource: 64-256 KiB
- framebuffer pixels: physical screen area, or smaller if using tiled output

## Diagnostics API

The core does not require logging yet, but a board port should provide:

- panic/assert output;
- frame timing counters;
- maximum heap watermark;
- dirty rectangle count/area;
- resource load failures;
- script exception reporting if JerryScript is enabled.

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
