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
- Prints timing and heap watermarks over the serial monitor.
- Provides a thin RGB565 panel flush hook in `main/jellyframe_esp32s3_hal.*`.

JerryScript is intentionally not part of this first bring-up. Add it after the
core pipeline and framebuffer path are stable on the board.

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
python -m esptool --chip esp32s3 merge_bin --output flash_image.bin --fill-flash-size 2MB "@flash_args"

qemu-system-xtensa.exe -M esp32s3 -m 4M `
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

The formal benchmark summary lives in `docs/esp32s3_qemu_benchmark.md` and
`docs/esp32s3_qemu_benchmark_zh.md`.

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

The callback receives the full RGB565 buffer, dimensions, stride and dirty
rectangle. If your panel API accepts a tight rectangle buffer, copy each dirty
row into a scratch buffer before calling the driver. If your panel API accepts
strided memory or full-width row windows, submit the dirty rectangle directly.

For `esp_lcd_panel_draw_bitmap`, be careful with partial-width dirty rectangles:
the API does not carry a source stride, so passing `pixels + y * stride + x`
only works when the dirty rectangle spans the whole row or when the source rows
are packed into a temporary tight buffer.

## Next Porting Steps

1. Replace the no-op `Rgb565Panel::flush` path with your board's panel driver.
2. Add touch/crown/button input and translate it into `InputController` calls.
3. Replace fallback text rendering with a bitmap font pack for production UI.
4. Replace the smoke-test resource table with a generated real app bundle.
5. Enable JerryScript only after the non-scripted pipeline is stable.
