# JellyFrame Porting Work Guide

Date: 2026-06-16

This guide is for developers porting JellyFrame to ESP32-S3, RTOS hosts, LVGL
shells or custom wearable hardware. It is not a browser feature document. It is
the porting task contract: what each board-side module must provide, how it
connects to the current core, how to validate it, and when the work must return
to the core instead of being forced into a board port.

The current core is sufficient for a first bring-up with static resources,
software rendering, RGB565 partial presentation, touch/button input, bitmap
fonts and optional JerryScript. It does not yet provide a no-full-framebuffer
tiled renderer, production-grade complex text shaping, image decoding or a
network resource stack.

The project was developed under the early codename `WearWeb`. Old ESP32-S3/QEMU
experiments still use that name. Anything merged into the main repository must
use `JellyFrame`, `jellyframe`, `JELLYFRAME` and `namespace jellyframe`.

## Stable Core Contracts

These interfaces exist now and can be used as porting entry points:

- `src/core/host.h`: device capabilities, resource requests, clocks, frame
  sinks and budgets.
- `src/core/budget.h`: maps `HostBudgets` into parser, render/layout/layer,
  dirty-rectangle and scripting limits.
- `src/core/embedded_framebuffer.h`: converts the core RGBA framebuffer into a
  host-owned RGB565, grayscale, monochrome or similar target buffer.
- `src/core/input.h`: touch, pointer, wheel, key, text input, focus navigation
  and activation.
- `src/core/text_backend.h` and `src/core/bitmap_font.h`: host text measurement
  and bitmap font painting.
- `src/core/document_style.h` and `src/core/document_script.h`: host callbacks
  for linked CSS and classic scripts.
- `src/script/jerryscript_runtime.h`: optional JerryScript runtime and DOM,
  event, form and timer bridges.

The first board port should not call Win32, filesystem or desktop shell code
directly. Use `jellyframe_embedded_host_demo` as the reference shape: it wires
static HTML/CSS, bitmap fonts, input and RGB565 presentation without windows,
files, networking or hardware I/O.

## Porting Phases

### P0: Import And Naming

Goal: turn experimental port files into maintainable mainline directories
without breaking the existing core build.

Requirements:

- Add or normalize `ports/esp32s3-idf/` as an independent ESP-IDF app.
- Add or normalize `ports/virtual_board/` as a desktop performance estimator.
- Do not overwrite the main root `CMakeLists.txt` with the experimental one.
- Rename old symbols:
  - `WearWeb` -> `JellyFrame`
  - `wearweb` -> `jellyframe`
  - `WEARWEB` -> `JELLYFRAME`
  - `namespace wearweb` -> `namespace jellyframe`
- Rename the ESP-IDF component to `jellyframe_core`.
- Use JellyFrame in log tags, Kconfig menus, READMEs and benchmark output.

Implementation:

- The ESP-IDF app directory should contain only port code, Kconfig, sdkconfig
  defaults and component CMake files.
- The component CMake should reference mainline `src/core/*.cpp`; its source
  list must match the current root `jellyframe_core` target.
- Keep JerryScript out of the P0/P1 ESP32-S3 component until the non-scripted
  pipeline is stable.

Acceptance:

- The desktop mainline build and tests still work.
- The ESP-IDF app can run `idf.py set-target esp32s3 && idf.py build`.
- `rg -n "WearWeb|wearweb|WEARWEB" ports docs README* CHANGELOG*` only finds
  intentional historical notes.

### P1: Device Capabilities And Budgets

Goal: make the board tell the core what the device can afford, avoiding
unbounded allocation and uncontrolled degradation.

Requirements:

- Fill `HostDeviceCapabilities` in the port layer.
- Set `HostBudgets` from real screen size, PSRAM, heap size and largest free
  block.
- Derive parser/layout/layer/dirty/script options through `src/core/budget.h`.
- When resources or framebuffers exceed the budget, degrade or skip cleanly.

Initial ESP32-S3 suggestion:

```cpp
jellyframe::HostBudgets budgets;
budgets.max_dom_nodes = 1024;
budgets.max_dom_depth = 48;
budgets.max_attributes_per_element = 24;
budgets.max_css_rules = 512;
budgets.max_css_declarations_per_rule = 96;
budgets.max_render_objects = 1024;
budgets.max_layout_boxes = 1024;
budgets.max_layers = 128;
budgets.max_display_commands = 2048;
budgets.max_dirty_rects = 8;
budgets.max_timers = 24;
budgets.max_event_listeners = 192;
budgets.max_resource_bytes = 128 * 1024;
budgets.max_framebuffer_pixels = width * height;
```

4 MB PSRAM can run the 300x300 bring-up benchmark, but it is tight and best
treated as a minimum. 8 MB is a practical baseline. Prefer 16 MB if the product
will enable JerryScript, Chinese fonts, multiple screens or resource caches.

Acceptance:

- Low-budget configurations truncate or degrade cleanly.
- Oversized resources, deep DOM, large CSS and excessive dirty rects do not
  hang, overflow or crash.
- Serial diagnostics include budget values, heap watermarks and largest free
  allocation blocks.

### P2: Resource Bundle Loading

Goal: feed HTML/CSS/JS from flash, partitions, LittleFS/FATFS or static arrays.

Requirements:

- Support `HostResourceKind::Stylesheet` and `HostResourceKind::ClassicScript`.
- Resolve relative paths in the host, not in the core.
- Honor `max_resource_bytes` on every load.
- Missing CSS/script must be recoverable: render with default styles or continue
  as a static page.
- Prefer compile-time resource bundles for production; filesystems are a debug
  convenience.

Implementation:

- Current ESP32-S3 bring-up: `ports/esp32s3-idf/tools/generate_resource_bundle.py`
  generates a read-only `ResourceEntry { url, kind, bytes, size }` table from
  `ports/esp32s3-idf/resources/app`.
- If a partition or filesystem is used, read into a bounded `std::string` and
  pass it to the existing document style/script loader.

Acceptance:

- Static resources, inline `<style>`, local `<link rel="stylesheet">` and
  classic `<script>` can load through host callbacks.
- Missing resources do not block first paint.
- The desktop capability checker can scan the same resource set.

### P3: Display And Framebuffer

Goal: present core output on a real panel.

Current core capability:

- The core renderer writes a logical RGBA8888 `FrameBuffer`.
- `embedded_framebuffer` converts dirty rects into a host-owned RGB565, BGR565,
  RGB332, Gray8 or 1-bit target buffer.
- The host-provided `flush(Rect)` calls the display driver.

Requirements:

- Start with an RGB565 target buffer.
- Keep one persistent `FrameBuffer` for dirty repaint.
- If memory permits, keep one persistent RGB565 target buffer to avoid per-frame
  allocation.
- `flush` must submit dirty rectangles, not always full frames.
- If the display driver requires tightly packed rows and a dirty rect is not
  full-width, pack rows into a static or stack scratch buffer before calling the
  driver.

Implementation:

```cpp
jellyframe::EmbeddedFrameBufferTarget target {
    width,
    height,
    jellyframe::EmbeddedPixelFormat::Rgb565,
    reinterpret_cast<std::uint8_t*>(rgb565_pixels),
    rgb565_size_bytes,
    width * sizeof(std::uint16_t),
};

jellyframe::EmbeddedFrameBufferSink sink { target, flush_dirty_rect, panel_context };
jellyframe::HostFrameSink frame_sink = jellyframe::embedded_frame_sink(sink);
jellyframe::present_frame(framebuffer, frame_sink, dirty_rects, dirty_count);
```

Acceptance:

- Solid backgrounds, borders, text, buttons, range/select/checkbox controls draw
  correctly.
- When dirty area is smaller than the screen, physical display transfer area is
  also reduced.
- Rotation, sleep/wake and repeated repaint do not corrupt the panel.

Not directly supported yet:

- If the device cannot hold a full RGBA framebuffer, do not force this path.
  Implement a core tiled renderer or scanline/tile compositor first.
- If the device only has a very small DMA buffer, the host may shrink the RGB565
  target, but the core still needs the source RGBA framebuffer. Removing the
  source framebuffer is future core work.

### P4: Text And Chinese Fonts

Goal: make Chinese UI text predictable in coverage, measurement and painting.

Requirements:

- Use offline-generated bitmap font packs in production.
- Measurement and painting must use the same glyph metrics.
- Font callbacks should avoid heap allocation.
- Missing glyphs must be visible and must not break layout.
- Run capability checks before board builds to emit used characters and verify
  coverage.

Implementation:

1. Run `jellyframe_capability_check --emit-used-chars` on HTML/CSS/JS.
2. Generate BDF or equivalent bitmap glyph data from a licensed source font.
3. Run `jellyframe_font_pack_gen` to emit a C++ `BitmapFont` header.
4. Wire `bitmap_font_measure_callback` into `LayoutEngine` and
   `bitmap_font_paint_callback` into `SoftwareCompositor`.

Acceptance:

- Chinese, digits and symbols do not clip in buttons, headings, lists or inputs.
- Bold text is visible through a real bold glyph set or a cheap emboldening
  approximation.
- Missing font coverage can fail or warn at desktop build time.

Avoid for the first ESP32-S3 port:

- Runtime vector font rasterization.
- Font loading inside the core.
- Complex shaping. Arabic, Indic scripts and similar text should be listed as a
  later capability.

### P5: Input And Interaction

Goal: convert real hardware input into platform-neutral DOM/input events.

Requirements:

- Touchscreens map to `pointer_down`, `pointer_move` and `pointer_up`.
- Crowns map to `wheel` or `focus_next/focus_previous`, depending on product UX.
- OK/confirm buttons map to `activate_focused`.
- Back buttons are host navigation; inside text fields they may map to
  `KeyCode::Backspace`.
- Software keyboards or IMEs send UTF-8 through `text_input(utf8)`.
- Hardware event processing must be bounded; never run layout or scripting
  directly in an ISR.

Implementation:

- ISR or driver callbacks enqueue small events.
- The UI task drains a bounded number of events per tick and calls
  `InputController`.
- If input dirties the DOM, enter the bounded rebuild/repaint path.

Acceptance:

- Buttons, checkboxes, radios, ranges, selects and text inputs are usable.
- Event delegation works: listeners on a parent can receive bubbled child
  events.
- `disabled` controls do not activate; `hidden` elements are invisible and not
  interactive.
- Fast repeated touch or crown input cannot grow the queue without bound.

### P6: Run Loop And Incremental Update

Goal: create a stable, measurable embedded UI frame loop.

Requirements:

- First paint runs the full pipeline: HTML/CSS -> DOM -> style -> render tree ->
  layout -> layer -> framebuffer.
- Non-structural changes prefer dirty-rect repaint.
- Structural DOM changes may conservatively rebuild the full viewport.
- Bound timer callbacks, input events and repaint region count per frame.
- Stop or slow repaint and timer pumping in low-power screen-off mode.

Recommended loop:

```text
load resources once
build first document and style
render first frame
present dirty/full frame

loop:
  poll bounded hardware events
  dispatch input through InputController
  pump bounded timers if scripting is enabled
  if tree/style/layout dirty:
      rebuild conservative pipeline
  else if paint dirty:
      compute dirty rects
  repaint dirty/full regions
  present through HostFrameSink
  sleep until next tick or hardware event
```

Acceptance:

- Static UI reaches first paint.
- Control state changes do not always clear the full screen.
- Timer-driven clock and timer examples do not slow down over time.
- Every queue has a hard limit.

### P7: JerryScript

Goal: enable small JavaScript apps after the non-scripted pipeline is stable.

Prerequisites:

- P1-P6 pass.
- Font and framebuffer paths are stable.
- Resource loading can load classic scripts.
- Timer pumping has a per-tick callback limit.
- Script exceptions can be logged.

Requirements:

- JerryScript is an optional component and does not enter `jellyframe_core`.
- Heap, timer and listener counts are constrained by `HostBudgets`.
- Script execution never happens in ISR or display flush callbacks.
- The supported script model is classic scripts only. Do not promise ES modules,
  fetch, broad Web APIs or full dynamic CSSOM.

Acceptance:

- Calculator, timer and clock-style apps respond to touch/buttons on the board.
- `setTimeout` and `setInterval` are pumped by the host.
- DOM mutation triggers repaint.
- Script exceptions do not crash the UI task.

### P8: Diagnostics, Benchmarks And Release Gates

Goal: make every board port reproducible in performance and memory behavior.

Requirements:

- Print per-stage timings: parse, style/render tree, layout, layer, flatten,
  render and present.
- Print heap free, minimum heap, largest free block and PSRAM free.
- Print dirty rect count, dirty area, flush count and flush bytes.
- Provide QEMU or virtual board benchmarks, but final data must be taken on real
  hardware.
- Detailed diagnostics must be removable from release builds.

Existing QEMU findings:

- ESP-IDF `v5.3.1` plus QEMU `esp_develop_9.2.2_20260417` can run the ESP32-S3
  PSRAM benchmark.
- Octal PSRAM completed 4M, 8M, 16M and 32M runs at 300x300, 40 cards and 20
  iterations.
- 4 MB is the minimum bring-up tier; 8 MB is a practical baseline; 16 MB is more
  suitable for JerryScript, Chinese fonts and resource caches.
- QEMU timing is useful for trends and memory-capacity validation, not as a
  replacement for real-chip FPS and latency data.

Release gates:

- Mainline desktop tests pass.
- The port's ESP-IDF build passes.
- At least one static page, one control page and one timer/script page pass on
  target hardware or QEMU.
- Documentation records board model, PSRAM, panel bus, resolution, font pack
  size, resource size, average frame cost and maximum heap watermark.

## Current Prerequisite Assessment

The first ESP32-S3 bring-up no longer needs new core interfaces. The required
work is to merge the experimental files into mainline JellyFrame form, then add
the real display driver, input driver, font resources and hardware measurements.

If a product target requires any of the following, plan core work first:

- Tiled rendering without a full RGBA framebuffer.
- Image decoding and real image painting for `object-fit`.
- Finer layer/display-command invalidation.
- Complex text shaping.
- Network resource loading and a security model.
- Arena allocation and small-vector attributes to reduce heap fragmentation.

## Recommended Merge Order

1. Merge `ports/virtual_board`, renamed to JellyFrame, as a desktop performance
   estimator.
2. Merge `ports/esp32s3-idf`, renamed to JellyFrame, initially building the
   non-scripted core only.
3. Move the QEMU report into `docs/esp32s3_qemu_benchmark.md` and the Chinese
   counterpart.
4. Wire `HostBudgets` and `budget.h` options into the ESP-IDF app.
5. Add the real panel `flush(Rect)` implementation.
6. Add touch/button/crown event queues.
7. Add the bitmap font pack.
8. Run real hardware benchmarks and update documentation.
9. Enable the JerryScript component afterwards.
