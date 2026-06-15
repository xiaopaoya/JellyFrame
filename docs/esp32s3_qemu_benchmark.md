# ESP32-S3 QEMU PSRAM Benchmark

Date: 2026-06-16

This document records the JellyFrame ESP32-S3 bring-up PSRAM gradient benchmark
under QEMU. It verifies that the firmware boots, that each PSRAM tier can hold
the current 300x300 software rendering pipeline, and that stage timings have no
obvious trend regressions. It is not a replacement for final FPS or latency
measurements on real ESP32-S3 silicon.

## Test Setup

- ESP-IDF: `v5.3.1`
- QEMU: `esp_develop_9.2.2_20260417`
- Target: `esp32s3`
- Viewport: `300x300`
- Synthetic cards: `40`
- Iterations: `20`
- PSRAM configuration:
  - `CONFIG_SPIRAM=y`
  - `CONFIG_SPIRAM_MODE_OCT=y`
  - `CONFIG_SPIRAM_TYPE_AUTO=y`
  - `CONFIG_SPIRAM_SPEED_40M=y`
  - `CONFIG_SPIRAM_USE_MALLOC=y`

QEMU Octal PSRAM argument:

```powershell
-global driver=ssi_psram,property=is_octal,value=true
```

Raw CSV files live at:

- `ports/esp32s3-idf/qemu_psram_gradient_octal_results.csv`
- `ports/esp32s3-idf/qemu_psram_gradient_quad_results.csv`

## Octal PSRAM Results

All timings are average microseconds per iteration.

| QEMU PSRAM | Detected PSRAM | Free PSRAM Before | HTML Parse | CSS Parse | Render Tree | Layout | Layer Tree | Flatten | Render Frame | Present RGB565 | Full Pipeline |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4M | 4 MB | 4,191,548 | 32,286.25 | 3,523.90 | 56,566.20 | 15,747.70 | 9,554.15 | 414.05 | 24,328.10 | 34,794.05 | 168,014.35 |
| 8M | 8 MB | 8,385,720 | 32,277.15 | 3,497.65 | 56,222.75 | 16,207.85 | 9,560.95 | 417.80 | 24,206.70 | 34,742.05 | 168,882.25 |
| 16M | 16 MB | 16,774,196 | 32,335.10 | 3,552.80 | 56,132.40 | 15,913.90 | 9,518.40 | 410.40 | 24,615.70 | 34,359.75 | 167,822.50 |
| 32M | 32 MB | 32,895,920 | 32,637.85 | 3,546.60 | 55,988.65 | 15,645.10 | 9,497.15 | 425.55 | 24,545.10 | 34,488.30 | 168,361.95 |

## Quad-Mode Control

With `CONFIG_SPIRAM_MODE_QUAD=y` and `CONFIG_SPIRAM_TYPE_AUTO=y`:

| QEMU PSRAM | Result |
|---:|---|
| 4M | Booted, detected 4 MB PSRAM, full benchmark completed. |
| 8M | Booted, detected 8 MB PSRAM, full benchmark completed. |
| 16M | Failed during PSRAM physical-size detection in this ESP-IDF v5.3.1 setup. |
| 32M | Failed during PSRAM physical-size detection in this ESP-IDF v5.3.1 setup. |

The failure disappears under Octal PSRAM simulation, so the full 4M-32M data
uses Octal mode.

## Conclusions

Increasing PSRAM capacity from 4 MB to 32 MB does not materially change the CPU
stage timings in this benchmark. Parser, render tree, layout, framebuffer
rendering, RGB565 presentation and the full pipeline stay within normal
run-to-run variation. For this workload, capacity mainly changes memory
headroom, not render speed.

The current 300x300 benchmark needs enough external memory for the RGBA
framebuffer, RGB565 presentation buffer, DOM/style/layout/layer allocations and
temporary full-pipeline objects. A 4 MB PSRAM device can run it, but it is the
minimum viable tier.

Practical selection:

- 4 MB: acceptable for bring-up, simple watch faces and constrained demos, with
  little headroom.
- 8 MB: recommended baseline for a 300x300 small UI with modest resources.
- 16 MB: preferred if JerryScript, Chinese fonts, multiple screens, images or
  resource caches are planned.
- 32 MB: not directly useful for this benchmark; choose it only for concrete
  large-resource or image-heavy requirements.

The next required step is real-board measurement for panel flush, touch input,
font packs and JerryScript. QEMU cannot replace real SPI/8080 bus behavior,
PSRAM bandwidth or scheduling noise.

