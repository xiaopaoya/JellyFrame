# JellyFrame ESP32-S3 ESP-IDF Port

> Last updated: 2026-07-13; Applies to: 0.5.0-dev

This directory is a first hardware bring-up path for ESP32-S3. It keeps the
engine core platform-neutral and builds a small ESP-IDF app around the HAL
shape described in `docs/embedded_hal_api.md`.

## What Runs Now

- Builds `src/render_core` as an ESP-IDF component named `jellyframe_render_core`.
- Provides three mutually exclusive startup modes: the one-shot synthetic
  benchmark, an interactive retained Timer UI task, and a deterministic
  retained-scroll workload.
- Loads static HTML/CSS/classic-script resources through a bounded host
  resource bundle before the benchmark.
- Measures parser, style/render tree, layout, layer tree, framebuffer rendering
  and optional RGB565 presentation.
- Applies embedded-oriented `HostBudgets` to parser, render/layout/layer and
  display-list construction.
- Runs P4/P5/P6 smoke checks for bitmap text measurement/painting, bounded
  board input queues, focus activation, text input and dirty-rectangle
  presentation.
- Prints timing and heap watermarks over the serial monitor.
- Provides a thin RGB565 panel flush hook in `main/jellyframe_esp32s3_hal.*`.
- Provides an optional Waveshare ESP32-S3-Touch-LCD-1.47 board adapter for the
  172x320 JD9853 LCD and AXS5106L touch controller. It is disabled by default
  and should be enabled only for physical-board bring-up. The adapter owns a
  bounded board-input queue; its touch task only enqueues events, while the UI
  task exclusively owns DOM, layout, composition, framebuffer presentation and
  input dispatch.
- The retained-scroll demo distinguishes taps from vertical drags with the
  shared allocation-free `VerticalScrollGesture`; once a drag crosses its small
  threshold, it cancels the pressed control and runs a bounded inertia tail.
  This is a host gesture policy, not an extra DOM or renderer feature.

JerryScript is intentionally not part of this first bring-up. Add it after the
core pipeline and framebuffer path are stable on the board.

The bring-up default raises `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to `32768` bytes
for the legacy one-shot benchmark. The interactive and scroll modes create a
separate owned UI task with the same initial stack budget, persistent retained
trees and persistent framebuffers. This remains a validation runtime, not a
product shell: JerryScript, app installation, service workers and low-power
policy are intentionally outside this port path. Always record stack
high-water marks before reducing either stack budget.

## Build And Flash

From this directory:

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

For the WS147 retained modes, use their complete defaults files rather than
overlaying the Timer bring-up file:

```powershell
# Deterministic 30 Hz retained-scroll measurement.
idf.py -B build-ws147-scroll -D SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults build

# Interactive retained scroll demo. Apply after the benchmark defaults.
idf.py -B build-ws147-scroll-demo -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults;sdkconfig.ws147_scroll_demo.defaults" build

# Phase D control (A) and experimental physical-GRAM ring path (B).
# Both use the same full-screen, opaque, single-scroll-viewport fixture.
idf.py -B build-ws147-panel-a -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults;sdkconfig.ws147_panel_scroll_a.defaults" build
idf.py -B build-ws147-panel-b -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults;sdkconfig.ws147_panel_scroll_b.defaults" build
```

The first file selects the scroll run mode and the WS147 hardware profile. The
second only disables automatic scrolling and selects the full-list workload;
do not combine either with `sdkconfig.ws147_bringup.defaults`, which selects the
Timer run mode.

`panel-a` and `panel-b` are a paired measurement fixture, not a general scroll
optimization switch. The fixture has one opaque rectangular 172x320 scroll
viewport, no fixed overlay, no scroll indicator and exactly one exposed strip
per step. Its content extent exceeds two panel heights so the fixed 6 px
ping-pong workload crosses the physical GRAM boundary. A uses the existing CPU
framebuffer scroll-blit plus normal dirty present. B keeps the same Core plan
and framebuffer update, but writes only the exposed strip through the WS147
callback's verified physical-GRAM ring mapping.
The callback is board-local; the Render Core has no JD9853 commands or panel
state. B is disabled by default and resets `VSCSAD` to zero before any normal
present. Invalid geometry, a second dirty area, mixed content, callback failure
or reset failure leaves the path on A rather than attempting a partial repair.

Useful `menuconfig` entries:

- `JellyFrame ESP32-S3 benchmark -> Startup run mode`
- `JellyFrame ESP32-S3 benchmark -> UI task stack size`
- `JellyFrame ESP32-S3 benchmark -> Scroll benchmark step in pixels`
- `JellyFrame ESP32-S3 benchmark -> Retained scroll benchmark workload`
- `JellyFrame ESP32-S3 benchmark -> Experimental WS147 physical-GRAM scroll A/B path`
- `JellyFrame ESP32-S3 benchmark -> Synthetic card count`
- `JellyFrame ESP32-S3 benchmark -> Benchmark iterations`
- `JellyFrame ESP32-S3 benchmark -> Viewport width`
- `JellyFrame ESP32-S3 benchmark -> Viewport height`
- `JellyFrame ESP32-S3 benchmark -> Benchmark RGB565 framebuffer presentation`
- `JellyFrame ESP32-S3 board support -> Enable physical board display/touch drivers`
- `JellyFrame ESP32-S3 board support -> Waveshare ESP32-S3-Touch-LCD-1.47`

The QEMU/bring-up defaults are `300x300`, `40` cards and `20` iterations. This
configuration expects PSRAM for the framebuffer and full pipeline benchmark.
The checked-in `sdkconfig.ws147_bringup.defaults` profile targets the Waveshare
1.47 board with a `172x320` viewport, 16 MB flash layout and octal PSRAM.
It expects ESP-IDF 5.x APIs, including the new `i2c_master` driver used by
ESP-IDF 5.3.x.

For the retained-scroll measurement, set startup mode to `Run retained scroll
benchmark`, leave `Automatically advance the scroll workload` enabled, choose
one workload, then capture at least one cumulative `port_telemetry` line.
The workload schedules against a 33.3 ms deadline rather than the generic UI
idle cadence. Its packed panel path converts into one internal DMA buffer and
waits for each panel DMA completion before reusing that buffer. `panel_dma_wait`
is therefore part of the measured present cost, not asynchronous work carried
into the next frame. Each cumulative line is followed by `pipeline_arena` used
and capacity counters, including the optional clipped text/image surface. After
the first retained build, capacity should remain stable during paint-only scroll
frames; a capacity increase identifies a larger pipeline rebuild, while an
unbounded increase is a regression to investigate.

For Phase D, first run A for ten minutes, then B for ten minutes after a fresh
flash. Save the final cumulative `port_telemetry` and `pipeline_arena` lines
from each run. The B result is valid only when `workload=panel`,
`panel_scroll_mode=1`, `panel_scroll_steps` advances, `panel_scroll_fallbacks=0`
and the observed list has no seams or corrupted rows through repeated ring
wraps. Record `panel_scroll_wraps`, frame/present p95, DMA timing, internal and
PSRAM watermarks, watchdog/reset status and a 30 fps visual capture. Do not
compare the older 240 px internal-list workloads with this fixture: their
rounded container, header/footer and indicator create mixed dirty regions and
cannot exercise the single-strip contract.

## Flash Layout

The port defaults to an 8 MB flash image with a custom partition table in
`partitions.csv`. The app partition is intentionally 4 MB so the later
JerryScript component, bitmap fonts and generated app resources have room to
grow.

Current layout:

| Name | Type | Offset | Size | Purpose |
|---|---|---:|---:|---|
| `nvs` | data/nvs | `0x9000` | 24 KB | system settings |
| `phy_init` | data/phy | `0xf000` | 4 KB | PHY init data |
| `factory` | app/factory | `0x10000` | 4 MB | JellyFrame firmware |
| `assets` | data/spiffs | `0x410000` | 2 MB | future generated resources/fonts |
| `storage` | data/nvs | `0x610000` | 512 KB | app settings/state |
| `coredump` | data/coredump | `0x690000` | 256 KB | crash diagnostics |

If the product needs OTA slots, prefer moving to a 16 MB flash module and using
a two-app layout instead of shrinking the 4 MB app partition.

For ESP32-S3 N16R8 boards with 16 MB flash and 8 MB octal PSRAM, this directory
also provides `partitions_16mb_n16r8.csv` and
`sdkconfig.n16r8_bench.defaults`. That profile keeps the 4 MB app partition,
expands `assets` to 8 MB, leaves 1 MB for settings/storage and reserves 512 KB
for coredumps. It is the current real-chip benchmark profile for the 300x300
synthetic UI workload.

Expected serial output shape:

```text
I JellyFrame: JellyFrame ESP32-S3 benchmark cards=40 iterations=20 viewport=300x300
I JellyFrame: device display=300x300 pixel_format=rgb565 partial=1 heap=... largest=... framebuffer_bytes=180000
I JellyFrame: budgets dom_nodes=... css_rules=... render_objects=... layout_boxes=... layers=... display_commands=... dirty_rects=... resource_bytes=... framebuffer_pixels=...
I JellyFrame: p2_resource_smoke html_bytes=... css_bytes=... css_rules=... scripts=... external_scripts=... script_bytes=... loads=... missing=... rejected=... oversized_blocked=1
I JellyFrame: benchmark_resources css_bytes=... scripts=... external_scripts=... script_bytes=... loads=... missing=... rejected=...
I JellyFrame: before heap_free=... heap_min=... largest=... internal_free=... spiram_free=...
I JellyFrame: html_parse iterations=20 avg_us=...
I JellyFrame: css_parse iterations=20 avg_us=...
I JellyFrame: render_tree iterations=20 avg_us=...
I JellyFrame: layout iterations=20 avg_us=...
I JellyFrame: layer_tree iterations=20 avg_us=...
I JellyFrame: flatten_layers iterations=20 avg_us=...
I JellyFrame: render_frame iterations=20 avg_us=...
I JellyFrame: p3_display_smoke full_ok=1 full_flushes=1 full_pixels=90000 full_bytes=180000 full_stride=300 partial_ok=1 partial_flushes=1 partial_pixels=10000 partial_bytes=20000 packed_flushes=1 scratch_flushes=1 failed_flushes=0 last_dirty=75,75 100x100
I JellyFrame: p4_p5_p6_ui_smoke font_ascii_ok=1 font_cjk_ok=1 ascii=... cjk=... first_present=1 dirty_present=1 dispatched=8 queue_left=8 dropped=12 pointer=0 wheel=0 focus=3 text=2 activate=2 checkbox=1 checkbox_clicks=1 input_value=B dirty_area=7680 dirty_flushes=1 dirty_bytes=15360
I JellyFrame: present_rgb565 iterations=20 avg_us=...
I JellyFrame: full_pipeline iterations=20 avg_us=...
I JellyFrame: after heap_free=... heap_min=... largest=... internal_free=... spiram_free=...
```

## QEMU Notes

ESP-IDF v5.3.1 installs Espressif QEMU `esp_develop_9.0.0_20240606` by default.
That version boots ESP32-S3 firmware, but it does not expose usable ESP-IDF
PSRAM in this benchmark: `esp_psram` reads PSRAM ID `0x00000000`, so
`spiram_free=0` and the 300x300 framebuffer stages are skipped.

Espressif QEMU `esp_develop_9.2.2_20260417` works for this benchmark. It accepts
`-m 4M` and ESP-IDF reports `Found 4MB PSRAM device`; `-m 8M` likewise reports
`Found 8MB PSRAM device`.

Manual launch flow:

```powershell
python -m esptool --chip esp32s3 merge_bin --output flash_image.bin --fill-flash-size 8MB "@flash_args"

qemu-system-xtensa.exe -M esp32s3 -m 4M `
  -global driver=ssi_psram,property=is_octal,value=true `
  -drive file=C:\Users\Administrator\AppData\Local\Temp\jellyframe-qemu-s3\flash_image.bin,if=mtd,format=raw `
  -drive file=C:\Users\Administrator\AppData\Local\Temp\jellyframe-qemu-s3\qemu_efuse.bin,if=none,format=raw,id=efuse `
  -nographic -monitor none -no-reboot
```

With `esp_develop_9.2.2_20260417`, `-m 4M`, `300x300`, `40` cards and `20`
iterations, the current synthetic benchmark reports approximately:

```text
html_parse avg_us=33516.20
css_parse avg_us=3622.55
render_tree avg_us=57751.80
layout avg_us=16389.85
layer_tree avg_us=10497.05
flatten_layers avg_us=422.80
render_frame avg_us=24673.50
present_rgb565 avg_us=35101.45
full_pipeline avg_us=178022.15
```

The raw benchmark CSV files in this directory capture the latest QEMU smoke
measurements used during bring-up.

## N16R8 Real-Chip Baseline

On 2026-06-19, a generic ESP32-S3 N16R8 development board passed the current
P2/P3/P4/P5/P6 smoke path and the full 300x300 synthetic UI benchmark.

Board and build profile:

- ESP32-S3 QFN56 rev v0.2, 240 MHz.
- 16 MB flash, 8 MB octal PSRAM at 40 MHz.
- `300x300`, `40` synthetic cards, `20` iterations.
- Generic memory panel path; no physical display bus was measured yet.
- Build used `sdkconfig.n16r8_bench.defaults` and
  `partitions_16mb_n16r8.csv`.

Average timings:

| Stage | Average |
|---|---:|
| `html_parse` | 10787.35 us |
| `css_parse` | 2168.25 us |
| `render_tree` | 189014.00 us |
| `layout` | 11973.60 us |
| `layer_tree` | 4225.80 us |
| `flatten_layers` | 312.80 us |
| `render_frame` | 72705.35 us |
| `present_rgb565` | 47431.30 us |
| `full_pipeline` | 279536.95 us |

Heap watermarks from the same run:

| Point | heap_free | heap_min | largest | internal_free | spiram_free |
|---|---:|---:|---:|---:|---:|
| before | 8736935 | 8728363 | 8257536 | 351215 | 8385720 |
| after | 8313131 | 7382219 | 8257536 | 32975 | 8280156 |

This confirms that 8 MB PSRAM is enough for the current 300x300 full pipeline
benchmark and is a reasonable baseline for small watch-style UIs. The low
post-benchmark `internal_free` value is the main caution for real display work:
panel DMA buffers, SPI/I80/QSPI transaction descriptors, touch drivers and
other internal-RAM-only allocations must be measured on the final board.

Compared with the QEMU 8 MB run, the real N16R8 `full_pipeline` is about 1.66x
slower. Parser/layout helper phases can be faster than QEMU, while
`render_tree`, `render_frame` and future physical display `present` work are
the stages to watch. Treat QEMU as a capacity and regression smoke test, not a
real timing source.

## Resource Bundle Hook

P2 resource loading is implemented in `main/jellyframe_esp32s3_resources.*`.
The current bring-up stores source assets under `resources/app/` and generates
a compile-time C++ table during the ESP-IDF build using the top-level
`tools/package_app.py` packer.

The smoke-test bundle contains:

- `/p2_smoke.html`
- `/styles/benchmark.css`
- `/scripts/benchmark.js`
- `/timer.html`
- `/styles/timer.css`

The loader resolves relative URLs against the host-provided base URL, rejects
non-local URLs, enforces `HostBudgets::max_resource_bytes` on every load, and
adapts to the existing linked stylesheet and classic-script callbacks. Missing
resources return `false`, so first paint can continue with inline or default
content.

To replace the smoke-test resources, edit files under `resources/app/` and its
`jellyframe.app.json`, or point the packer at a different app root in
`main/CMakeLists.txt`; generated sources stay in the build directory and should
not be committed.

## Display Hook

The optional WS147 board adapter allocates its packed RGB565 DMA line buffer
only when the physical board profile is initialized, then releases it from
`release_board_runtime()`. Keep this pattern for new boards: internal/DMA RAM
should be tied to the board runtime lifetime rather than hidden in global
scratch buffers.

`jellyframe_esp32s3::Rgb565Panel` owns the host-facing display contract:

```cpp
jellyframe_esp32s3::Rgb565Panel panel;
panel.pixels = rgb565_buffer;
panel.width = width;
panel.height = height;
panel.stride_pixels = width;
panel.flush = your_panel_flush;
```

The `flush` callback receives the full RGB565 buffer, dimensions, stride and
dirty rectangle. Use it when your panel driver accepts a source stride or when
you choose to submit full-width row windows.

For drivers such as `esp_lcd_panel_draw_bitmap` that expect a tightly packed
dirty-rectangle buffer, use `packed_flush`. The primary retained UI path gives
`EmbeddedPackedRgb565Sink` a persistent tightly packed RGB565 destination;
the render core converts each dirty rectangle into that buffer before invoking
the callback. Its signature additionally receives an optional metrics output:

```cpp
bool your_packed_rect_flush(const std::uint16_t* pixels,
                            jellyframe::Rect dirty,
                            jellyframe_esp32s3::Rgb565PackedFlushMetrics* metrics,
                            void* context);

jellyframe_esp32s3::Rgb565Panel panel;
panel.width = width;
panel.height = height;
panel.stride_pixels = width;
panel.packed_flush = your_packed_rect_flush;
panel.packed_pixels = persistent_rgb565_buffer;
panel.packed_pixel_capacity = static_cast<std::size_t>(width) * height;
```

Set `metrics->convert_us`, `window_setup_us`, `dma_submit_us`, `dma_wait_us`
and `chunks` when the driver can report them. The WS147 callback waits for each
DMA transfer before it reuses its internal chunk buffer, so the metric and the
buffer lifetime have the same boundary. A real `packed_flush` implementation
can call:

```cpp
esp_lcd_panel_draw_bitmap(panel_handle,
                          dirty.x,
                          dirty.y,
                          dirty.x + dirty.width,
                          dirty.y + dirty.height,
                          pixels);
```

`packed_scroll_flush` is a separate optional board callback for a panel with a
verified hardware scroll-address contract. It receives the already packed,
full-width exposed strip and the existing Core `ScrollBlitPlan` delta. It must
reject every shape it cannot prove safe and provide `reset_scroll`; the HAL
then performs a normal full present after reset. It is intentionally not a
Render Core capability and should remain unbound on generic displays.

The legacy QEMU smoke path also exercises a full-frame strided flush and a
padded-stride partial dirty rectangle that requires `scratch_pixels` packing.
That compatibility adapter remains available through `make_rgb565_sink()`;
the retained UI path does not allocate it. The smoke log records flush count,
dirty pixels, transferred bytes, scratch usage and failed flushes.

`coalesce_dirty_rects_into(...)` remains disabled in the default WS147 port
path. On 2026-07-13, a three-adjacent-block workload reduced three flushes to
one with a net present-time gain, but a dispersed three-block workload correctly
kept all three rectangles and still regressed p95 frame time because the port
paid the planning cost. Treat coalescing as an opt-in experiment only: compare
it against a no-merge control, measure timer/wrapper overhead separately, and
keep a direct original-rectangle fallback. Do not move this decision into
Render Core or enable it solely from a board profile.

For `esp_lcd_panel_draw_bitmap`, be careful with partial-width dirty rectangles:
the API does not carry a source stride, so passing `pixels + y * stride + x`
only works when the dirty rectangle spans the whole row or when the source rows
are packed into a temporary tight buffer.

## Text And Input Smoke Hooks

`main/jellyframe_esp32s3_font.*` contains a deliberately tiny bring-up bitmap
font. It covers the ASCII glyphs needed by the smoke text plus one CJK glyph
(`U+4E2D`) so the board path can validate UTF-8 codepoint measurement and
painting. It is not a production Chinese font. Product firmware should choose a
font profile with `jellyframe_font_resource_check`, then generate either an
app-specific bitmap font pack or a documented product profile such as
`cn-standard` with `jellyframe_font_pack_gen`. Record the source font license,
glyph count and flash size.

`main/jellyframe_esp32s3_input.*` contains a fixed-capacity
`BoardInputQueue`. Drivers or ISRs should enqueue small board events, then the
UI task should drain a bounded number per frame and call `dispatch_input_events`
to forward them into `InputController`. Text events are copied from a fixed
16-byte buffer with bounded length handling, so an unterminated hardware buffer
cannot read past the event object.

The P4/P5/P6 smoke path remains a validation harness. It proves that bitmap
font callbacks, focus navigation, activation, text input, checkbox state,
bounded queue overflow accounting and dirty-rectangle presentation connect to
the mainline core. The Timer and scroll startup modes add a retained UI loop
with persistent framebuffers, real panel/touch drivers, bounded input pumping
and port telemetry. They are not yet a final board shell: low-power policy,
app lifecycle isolation and product-level resource packaging still need their
own host integration.

## Board Bring-Up Checklist

1. Replace the no-op `Rgb565Panel` path with your board's panel driver.
2. Add touch/crown/button input and translate it into `InputController` calls.
3. Choose a font profile and replace the bring-up font with a production bitmap
   font pack.
4. Replace the smoke-test resource table with a generated real app bundle.
5. Enable JerryScript only after the non-scripted pipeline is stable.
