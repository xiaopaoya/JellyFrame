# ESP32-S3 Components

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

ESP-IDF component wrappers used by the ESP32-S3 bring-up project.

These files adapt the platform-neutral core to the ESP-IDF build system. Keep
engine logic in `src/` unless it is genuinely board-specific.

- `jellyframe_render_core/`: CMake component wrapper for the neutral renderer.
- `jellyframe_app_runtime/`: runtime contract wrapper; it does not perform
  board I/O by itself.
- `jellyframe_jerryscript/`: optional JerryScript/ESP-IDF adapter.

Port-specific panel, touch, resource and task ownership belongs in `main/`.
