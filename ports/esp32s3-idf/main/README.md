# ESP32-S3 Main

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Board demo entry points for the ESP32-S3 bring-up project.

Use this directory for hardware initialization, static-resource startup and
presentation smoke tests. Do not move renderer, parser or DOM logic here.

Start with `main.cpp` for startup modes. Board implementations are under
`boards/`; HAL/display and input adapters are the `jellyframe_esp32s3_*.{h,cpp}`
files; acceptance entry points are the `*_acceptance.cpp` files. Keep telemetry,
DMA and panel work here or in a board adapter, never in `src/render_core`.
