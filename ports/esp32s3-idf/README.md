# JellyFrame ESP32-S3 ESP-IDF Port

This directory is a first hardware bring-up path for ESP32-S3. It keeps the
engine core platform-neutral and builds a small ESP-IDF app around the HAL
shape described in `docs/embedded_hal_api.md`.

## What Runs Now

- Builds `src/core` as an ESP-IDF component named `jellyframe_core`.
- Runs a synthetic HTML/CSS pipeline benchmark from `app_main`.
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

JerryScript is intentionally not part of this first bring-up. Add it after the
core pipeline and framebuffer path are stable on the board.

The bring-up default raises `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to `32768` bytes
because the smoke app still runs parser, layout, framebuffer and validation code
from `app_main`. Treat this as a validation default, not a final product target:
a real board shell should move work into an owned UI task, keep persistent
buffers and re-measure stack high-water marks.

## Build And Flash

From this directory:

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Useful `menuconfig` entries:

- `JellyFrame ESP32-S3 benchmark -> Synthetic card count`
- `JellyFrame ESP32-S3 benchmark -> Benchmark iterations`
- `JellyFrame ESP32-S3 benchmark -> Viewport width`
- `JellyFrame ESP32-S3 benchmark -> Viewport height`
- `JellyFrame ESP32-S3 benchmark -> Benchmark RGB565 framebuffer presentation`

The QEMU/bring-up defaults are `300x300`, `40` cards and `20` iterations. This
configuration expects PSRAM for the framebuffer and full pipeline benchmark.

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

The formal benchmark summary lives in
`../../project_docs/esp32s3_qemu_benchmark.md` and
`../../project_docs/esp32s3_qemu_benchmark_zh.md`.

## Resource Bundle Hook

P2 resource loading is implemented in `main/jellyframe_esp32s3_resources.*`.
The current bring-up stores source assets under `resources/app/` and generates
a compile-time C++ table during ESP-IDF configure using
`tools/generate_resource_bundle.py`.

The smoke-test bundle contains:

- `/app/p2_smoke.html`
- `/app/styles/benchmark.css`
- `/app/scripts/benchmark.js`

The loader resolves relative URLs against the host-provided base URL, rejects
non-local URLs, enforces `HostBudgets::max_resource_bytes` on every load, and
adapts to the existing linked stylesheet and classic-script callbacks. Missing
resources return `false`, so first paint can continue with inline or default
content.

To replace the smoke-test resources, edit files under `resources/app/` or point
the generator at a different asset root in `main/CMakeLists.txt`; generated
sources stay in the build directory and should not be committed.

## Display Hook

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
dirty-rectangle buffer, use `packed_flush` and provide a scratch buffer:

```cpp
std::uint16_t* rgb565_pixels = persistent_rgb565_buffer;
std::uint16_t* scratch_pixels = persistent_scratch_buffer;

jellyframe_esp32s3::Rgb565Panel panel;
panel.pixels = rgb565_pixels;
panel.width = width;
panel.height = height;
panel.stride_pixels = width;
panel.packed_flush = your_packed_rect_flush;
panel.scratch_pixels = scratch_pixels;
panel.scratch_pixel_capacity = scratch_pixel_count;
```

The HAL passes full-width tight rectangles directly when possible. For
partial-width dirty rectangles, it packs rows into `scratch_pixels` before
calling `packed_flush`. A real `packed_flush` implementation can call:

```cpp
esp_lcd_panel_draw_bitmap(panel_handle,
                          dirty.x,
                          dirty.y,
                          dirty.x + dirty.width,
                          dirty.y + dirty.height,
                          pixels);
```

The QEMU smoke path exercises both a full-frame strided flush and a padded-stride
partial dirty rectangle that requires scratch packing. It logs flush count,
dirty pixels, transferred bytes, scratch usage and failed flushes.

For `esp_lcd_panel_draw_bitmap`, be careful with partial-width dirty rectangles:
the API does not carry a source stride, so passing `pixels + y * stride + x`
only works when the dirty rectangle spans the whole row or when the source rows
are packed into a temporary tight buffer.

## Text And Input Smoke Hooks

`main/jellyframe_esp32s3_font.*` contains a deliberately tiny bring-up bitmap
font. It covers the ASCII glyphs needed by the smoke text plus one CJK glyph
(`U+4E2D`) so the board path can validate UTF-8 codepoint measurement and
painting. It is not a production Chinese font. Product firmware should generate
an app-specific bitmap font pack with `jellyframe_font_pack_gen` and document
the source font license, glyph count and flash size.

`main/jellyframe_esp32s3_input.*` contains a fixed-capacity
`BoardInputQueue`. Drivers or ISRs should enqueue small board events, then the
UI task should drain a bounded number per frame and call `dispatch_input_events`
to forward them into `InputController`. Text events are copied from a fixed
16-byte buffer with bounded length handling, so an unterminated hardware buffer
cannot read past the event object.

The current P4/P5/P6 smoke path is still a validation harness. It proves that
bitmap font callbacks, focus navigation, activation, text input, checkbox state,
bounded queue overflow accounting and dirty-rectangle presentation all connect
to the mainline core. It is not yet the final board run loop: a real port still
needs persistent frame buffers, real panel/touch drivers, timer pumping,
low-power policy and product-level resource packaging.

## Next Porting Steps

1. Replace the no-op `Rgb565Panel` path with your board's panel driver.
2. Add touch/crown/button input and translate it into `InputController` calls.
3. Replace fallback text rendering with a bitmap font pack for production UI.
4. Replace the smoke-test resource table with a generated real app bundle.
5. Enable JerryScript only after the non-scripted pipeline is stable.
