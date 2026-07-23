# JellyFrame ESP32-S3 ESP-IDF Port

> Last updated: 2026-07-24; Applies to: 0.5.0-dev

This directory is a first hardware bring-up path for ESP32-S3. It keeps the
engine core platform-neutral and builds a small ESP-IDF app around the HAL
shape described in `docs/embedded_hal_api.md`.

## What Runs Now

- Builds `src/render_core` as an ESP-IDF component named `jellyframe_render_core`.
- Builds the platform-neutral `jellyframe_app_runtime` component as a separate
  static archive. The default port does not instantiate an AppRuntime host, so
  the linker does not retain it in the normal firmware image. This is build
  integration only; it does not yet claim third-party App isolation or
  JerryScript support on ESP32-S3.
- Provides mutually exclusive startup modes for the one-shot synthetic
  benchmark, retained UI fixtures, deterministic scroll/presentation A/B work,
  bounded resource handling, and board-local sleep acceptance.
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
- Enables deterministic RGB565 ordered dithering only for retained pages that
  actually contain a CSS gradient. This removes the most visible 5:6:5 color
  banding without changing RGBA rendering or adding conversion work to plain
  pages. The port refreshes this policy after every layer-tree rebuild, including
  paint-only updates. It can be disabled for panel A/B work or controllers with
  hardware dithering. The bit-exact ordered-dither conversion fast path measured
  28.385 ms/flush on the WS147 172x320 gradient fixture, down from 33.116 ms
  before the quantization cleanup; panel transport remains board-specific.
- Provides an optional Waveshare ESP32-S3-Touch-LCD-1.47 board adapter for the
  172x320 JD9853 LCD and AXS5106L touch controller. It is disabled by default
  and should be enabled only for physical-board bring-up. The adapter owns a
  bounded board-input queue; its touch task only enqueues events, while the UI
  task exclusively owns DOM, layout, composition, framebuffer presentation and
  input dispatch.
- Provides an optional Waveshare ESP32-S3-Touch-LCD-1.69 board adapter for the
  240x280 ST7789V2 LCD and CST816T touch controller. Its physical-GRAM scroll
  experiment is disabled by default and bound only to the full-screen opaque
  panel fixture; it has no effect on the Render Core or ordinary app pages.
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

For WS169 first bring-up, use an isolated generated `sdkconfig`:

```powershell
idf.py -B build-ws169-bringup `
  -D "SDKCONFIG=build-ws169-bringup/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws169_bringup.defaults" build
idf.py -B build-ws169-bringup -p COMx flash monitor
```

The WS169 profile is `240x280`, ST7789V2 over SPI mode 0 with `y_gap=20`,
40-row DMA strips and CST816T input. Its A/B fixture is deliberately separate
from the WS147 path:

```powershell
# A: normal framebuffer scroll-blit and full viewport present.
idf.py -B build-ws169-panel-a -D "SDKCONFIG_DEFAULTS=sdkconfig.ws169_scroll_benchmark.defaults;sdkconfig.ws169_panel_scroll_a.defaults" build

# B: ST7789 VSCRDEF/VSCSAD GRAM ring, one exposed-strip submit per step.
idf.py -B build-ws169-panel-b -D "SDKCONFIG_DEFAULTS=sdkconfig.ws169_scroll_benchmark.defaults;sdkconfig.ws169_panel_scroll_b.defaults" build
```

WS169 B reserves the ST7789's 320-row GRAM as `20/280/20` fixed/scroll/fixed
rows. It is valid only for the paired full-screen, opaque, rectangular,
single-scroll-viewport fixture. Rounded clipping, fixed overlays, transparency,
scroll indicators or multiple dirty rectangles must use the normal safe path;
any callback failure resets `VSCSAD` before that fallback present.

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

To prove the B path can leave physical-GRAM mapping safely, use the
acceptance-only one-shot fallback probe with an isolated sdkconfig:

```powershell
idf.py -B build-ws147-panel-fallback-probe `
  -D "SDKCONFIG=build-ws147-panel-fallback-probe/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_scroll_benchmark.defaults;sdkconfig.ws147_panel_scroll_b.defaults;sdkconfig.ws147_panel_scroll_fallback_probe.defaults" build
```

After 30 successful strip submissions, the log must show `phase=inject`, one
successful full present with `mapped_after=0`, and `phase=reentry` on the next
eligible frame. This option remains off in normal A/B profiles.

For visual and touch acceptance of the 172x320 Band System Shell fixture:

```powershell
idf.py -B build-ws147-band-shell `
  -D "SDKCONFIG=build-ws147-band-shell/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_band_shell.defaults" build
idf.py -B build-ws147-band-shell `
  -D "SDKCONFIG=build-ws147-band-shell/sdkconfig" -p COMx flash monitor
```

The fixture is intentionally non-scripted. Its native adapter can only switch
among the watch face, app grid, activity, weather, quick-settings and notices
views. It is useful for panel/text/input acceptance, not evidence that the
product shell, service permissions or app lifecycle are complete.

After flashing, verify the initial `FLOW` view, then tap the two metric cards,
weather card and the two bottom actions. From the app grid, tap Activity,
Weather and Settings; each visible detail page has a Back control. Every route
must repaint the full new view without a reset, corrupted rounded corners or
stale text. The serial monitor should first contain `ui_task kind=band-shell`
and later a `port_telemetry case=band_shell_ui_cumulative` line. Record the
frame/present p95, internal/PSRAM low-water marks, board profile and any touch
misroutes. The fixture uses the recovered Noto Sans SC production bitmap
family. The serial startup line reports its face, coverage, glyph and
bitmap-byte counts; physical visual review is still required for typography
acceptance.

When a shell route is slow, collect the accompanying
`port_pipeline_telemetry` line as well. It separates input dispatch, frame
planning, render-tree construction, layout, layer-tree construction, input
controller binding and remaining active-frame work. It is port-local
instrumentation: it allocates no buffers and does not change the Render Core
or ordinary application behavior. Compare timings per rebuild, not the idle
frame average. Each accepted native route also writes
`band_shell route=<view> count=<n>`; the final transition count must match the
manual or replay script. A test-only injected-input replay validates the path after the
board adapter, but a manual run is still required to accept AXS5106L coordinate
calibration and gesture discoverability.

All retained UI modes also emit one `port_cold_start_telemetry` line after the
first successful present. It reports resource/parse, pipeline-build, first-frame
and first-present durations separately, with current free and largest internal
RAM/PSRAM blocks. Keep this line with every cold-boot acceptance log; it is not
included in steady-state p50/p95 telemetry.

For an isolated opaque linear-gradient raster/present capture, build the fixed
30 Hz fixture instead of the shell:

```powershell
idf.py -B build-ws147-gradient-fastpath `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_gradient_fastpath.defaults" build
idf.py -B build-ws147-gradient-fastpath -p COMx flash monitor
```

It renders one full-screen square, opaque `linear-gradient()` and produces
`port_telemetry case=opaque_linear_gradient_cumulative`. It intentionally has
no text, input, animation, shell navigation or rounded/translucent paint, so it
is suitable only for comparing an equivalent renderer revision or compile-time
candidate. Keep the same dither policy for both runs and compare `compose`,
`framebuffer_convert`, `present`, DMA and heap-watermark fields separately.

For the WS147 board-local panel power boundary, use a separate build directory:

```powershell
idf.py -B build-ws147-power `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_power_acceptance.defaults" build
idf.py -B build-ws147-power -p COMx flash monitor
```

The fixture cycles JD9853 `DISPOFF`/`SLPIN`, turns the backlight off, then uses
`SLPOUT`/`DISPON`, turns the backlight on and forces one full repaint. It emits
`screen_power_transition` and `screen_power_offs`, `screen_power_ons`,
`screen_power_failures` fields in `port_telemetry`. This is panel sleep and
backlight evidence only; it does not measure ESP32-S3 light/deep-sleep current
or GPIO wake behavior.

For bounded missing-resource behavior, use `sdkconfig.ws147_resource_failure.defaults`.
The fixture links a missing stylesheet and references a missing image while its
inline fallback remains renderable. Acceptance requires the resource log to
show the expected missing/rejected count, a successful first present and no
reset. This fixture only validates bounded missing-resource behavior. The
port also contains an acceptance-only bounded BMP image adapter; use the
image acceptance profile to exercise successful decode, cache reuse,
unsupported format rejection, and oversized/corrupt input handling.

For the AppRuntime lifecycle preflight, use
`sdkconfig.ws147_app_runtime_recovery_acceptance.defaults` with an isolated
build directory:

```powershell
idf.py -B build-ws147-app-runtime-recovery `
  -D "SDKCONFIG=build-ws147-app-runtime-recovery/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_app_runtime_recovery_acceptance.defaults" build
idf.py -B build-ws147-app-runtime-recovery `
  -D "SDKCONFIG=build-ws147-app-runtime-recovery/sdkconfig" -p COMx flash monitor
```

The native system shell must present first, then the log must report zero
failures for 30 cycles each of `runtime-error`, `budget-exceeded` and
`load-failure`, followed by `app_runtime_native_recovery_summary scripting=0`
with `system_shell_retained=1`. Confirm that the shell remains interactive
after the summary. This is deliberately a native AppRuntime integration
preflight, not H5 script-watchdog acceptance: no third-party DOM or
JerryScript realm is started by this configuration.

For the ESP32-S3 SoC sleep fixture, use a separate build directory:

```powershell
idf.py -B build-ws147-soc-power `
  -D "SDKCONFIG=build-ws147-soc-power/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_soc_power_acceptance.defaults" build
idf.py -B build-ws147-soc-power `
  -D "SDKCONFIG=build-ws147-soc-power/sdkconfig" -p COMx flash monitor
```

This runs 100 light-sleep cycles with timer and AXS5106L INT GPIO wake
configuration, then 30 timer-driven deep-sleep cold-start cycles and finally
starts the interactive Band Shell. USB-Serial/JTAG output is unavailable
during light sleep, so its summary is retained in RTC memory and printed after
the first deep wake. The fixture reports timing and heap watermarks but does
not measure current; use an external meter for the H4 power gate.

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
Once B has started, it elides the duplicate CPU framebuffer row move: the
panel already owns the reused visible rows, so the CPU only composes and packs
the exposed strip. The framebuffer is deliberately considered stale during
those frames. Before any normal present or a panel callback fallback, the port
recomposes the complete current viewport, resets `VSCSAD`, then submits that
full framebuffer through A. `panel_scroll_cpu_blits_elided` and
`panel_scroll_recovery_compose_ms_total` make both behaviors observable.
The callback is board-local; the Render Core has no JD9853 commands or panel
state. B is disabled by default and resets `VSCSAD` to zero before any normal
present. Invalid geometry, a second dirty area, mixed content, callback failure
or reset failure leaves the path on A rather than attempting a partial repair.
Framebuffer scroll-blit also rejects a layer with `LayerReasonRoundedClip`: a
rectangular pixel move would carry stale content through the fixed rounded
corners. That layer instead retains its trees and recomposes the full visible
scroll region before the normal dirty present. The dedicated Phase D fixture is
rectangular and remains eligible for the A/B path.

Useful `menuconfig` entries:

- `JellyFrame ESP32-S3 benchmark -> Startup run mode`
- `JellyFrame ESP32-S3 benchmark -> Start 172x320 Band System Shell UI task`
- `JellyFrame ESP32-S3 benchmark -> Run opaque linear-gradient presentation fixture`
- `JellyFrame ESP32-S3 benchmark -> Run panel screen-off/resume acceptance fixture`
- `JellyFrame ESP32-S3 benchmark -> Run missing-resource fallback fixture`
- `JellyFrame ESP32-S3 benchmark -> Run native AppRuntime recovery preflight`
- `JellyFrame ESP32-S3 benchmark -> UI task stack size`
- `JellyFrame ESP32-S3 benchmark -> Dither RGB565 presentation for pages containing gradients`
- `JellyFrame ESP32-S3 benchmark -> Scroll benchmark step in pixels`
- `JellyFrame ESP32-S3 benchmark -> Retained scroll benchmark workload`
- `JellyFrame ESP32-S3 benchmark -> Experimental WS147 physical-GRAM scroll A/B path`
- `JellyFrame ESP32-S3 benchmark -> Experimental WS169 ST7789 physical-GRAM scroll A/B path`
- `JellyFrame ESP32-S3 benchmark -> Synthetic card count`
- `JellyFrame ESP32-S3 benchmark -> Benchmark iterations`
- `JellyFrame ESP32-S3 benchmark -> Viewport width`
- `JellyFrame ESP32-S3 benchmark -> Viewport height`
- `JellyFrame ESP32-S3 benchmark -> Benchmark RGB565 framebuffer presentation`
- `JellyFrame ESP32-S3 board support -> Enable physical board display/touch drivers`
- `JellyFrame ESP32-S3 board support -> Waveshare ESP32-S3-Touch-LCD-1.47`
- `JellyFrame ESP32-S3 board support -> Waveshare ESP32-S3-Touch-LCD-1.69`

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
wraps. Record `panel_scroll_backend`, `panel_scroll_wraps`, `panel_scroll_cpu_blits_elided`, frame/present
p95, DMA timing, internal and PSRAM watermarks, watchdog/reset status and a
30 fps visual capture. Do not
compare the older 240 px internal-list workloads with this fixture: their
rounded container, header/footer and indicator create mixed dirty regions and
cannot exercise the single-strip contract.

## Flash Layout

The port defaults to a 16 MB flash image with a custom partition table in
`partitions.csv`. The 8 MB factory partition leaves room for the recovered
bitmap font pack, JerryScript and generated app resources.

Current layout:

| Name | Type | Offset | Size | Purpose |
|---|---|---:|---:|---|
| `nvs` | data/nvs | `0x9000` | 24 KB | system settings |
| `phy_init` | data/phy | `0xf000` | 4 KB | PHY init data |
| `factory` | app/factory | `0x10000` | 8 MB | JellyFrame firmware and production bitmap fonts |
| `assets` | data/spiffs | `0x810000` | 6 MB | generated resources and future app assets |
| `storage` | data/nvs | `0xe10000` | 1 MB | app settings/state |
| `coredump` | data/coredump | `0xf10000` | 256 KB | crash diagnostics |

For ESP32-S3 N16R8 boards with 16 MB flash and 8 MB octal PSRAM, this directory
also provides `partitions_16mb_n16r8.csv` and
`sdkconfig.n16r8_bench.defaults`. That profile uses the same 8 MB app / 6 MB
assets plan and reserves 512 KB for coredumps. It is the current real-chip
benchmark profile for the 300x300 synthetic UI workload.

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
$qemuDir = Join-Path $env:TEMP "jellyframe-qemu-s3"
New-Item -ItemType Directory -Force $qemuDir | Out-Null
$flashImage = Join-Path $qemuDir "flash_image.bin"
$efuseImage = Join-Path $qemuDir "qemu_efuse.bin"

python -m esptool --chip esp32s3 merge_bin --output $flashImage --fill-flash-size 8MB "@flash_args"

qemu-system-xtensa.exe -M esp32s3 -m 4M `
  -global driver=ssi_psram,property=is_octal,value=true `
  -drive "file=$flashImage,if=mtd,format=raw" `
  -drive "file=$efuseImage,if=none,format=raw,id=efuse" `
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
- `/band_shell.html`
- `/styles/band_shell.css`
- `/gradient_fastpath.html`
- `/styles/gradient_fastpath.css`

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

`main/jellyframe_esp32s3_noto_sans_sc_font.cpp` is the recovered offline
generated Noto Sans SC bitmap pack. It contains Regular/Medium/Bold at 16/20/24
px, ASCII, common symbols and GB2312 level-1 coverage: 9 faces, 4,541 covered
characters, 40,869 glyphs and 3,262,475 bitmap bytes. The generator is
`tools/generate_noto_sans_sc_font_pack.py`; Pillow and the source font files
are needed only when regenerating the checked-in source. The tiny
`main/generated` fixture remains available for early debug fallback, but the
normal AppFont callbacks use Noto. The recovered pack is 1bpp. Its Noto Sans
SC 2.002/OFL source and reproduction record live under `font/`; product
acceptance still requires a normal-distance 1bpp/2bpp visual selection. The
current 2bpp candidate leaves only 7.21% of the 8 MB app partition free, while
4bpp does not fit that layout.

Run the physical smoke with an isolated configuration:

```powershell
idf.py -B build-ws147-typography-acceptance-v2 `
  -D "SDKCONFIG=build-ws147-typography-acceptance-v2/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.ws147_typography_acceptance.defaults" build
```

The decisive line is `p4_p5_p6_ui_smoke`; it reports ASCII, CJK and Bold
measurement success plus first/dirty present status.

The ESP32-S3 port can bind a board-owned bounded BMP image adapter for
uncompressed 24/32-bit BMP resources. It does not bind audio or video
adapters. Enable `CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER` only for
an image-capable profile: the adapter is then advertised through the host
capability snapshot and is limited to 96x96 resources, 32 KiB of decoded
RGB565/alpha cache, 16 cached URLs and 256-byte URLs. It is disabled by
default, so Apps that do not use images neither link its decoder nor retain
cache state. Unsupported or invalid resources follow the documented fallback
behavior.

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
3. Use the checked-in Noto Sans SC 2.002/OFL pack for functional smoke, then
   complete visual review and the 1bpp/2bpp product-format decision before
   release.
4. Replace the smoke-test resource table with a generated real app bundle.
5. Enable JerryScript only after the non-scripted pipeline is stable.
