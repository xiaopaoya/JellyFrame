# Ports

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Port-support code and board-facing experiments live here.

The platform-neutral engine remains in `src/`. Port directories may contain
hardware glue, generated resources, board build files and validation demos.

Choose by target and evidence ownership:

| Need | Directory | Scope |
| --- | --- | --- |
| Learn the minimum board-facing integration | `embedded_host_demo/` | Host-side reference, no real hardware |
| Estimate CPU/panel transfer trends without a board | `virtual_board/` | Desktop model, not MCU timing evidence |
| Build, flash and validate ESP32-S3 boards | `esp32s3-idf/` | ESP-IDF port, board drivers and port-owned acceptance |
| Add another board family | New sibling port directory | Keep vendor SDK and panel/input code outside `src/` |

Current port areas:

- `embedded_host_demo/`: platform-neutral host bring-up demo for static
  resources, bitmap text, input and RGB565 presentation.
- `virtual_board/`: desktop virtual-board benchmark and presentation harness.
- `esp32s3-idf/`: ESP32-S3 bring-up project and static-resource experiment.

LVGL or vendor SDK integration should remain a thin optional adapter around
panel/input/text hooks, not a replacement UI framework inside JellyFrame.

For a new port, read `../docs/porting_work_guide.md`, then the closest port
README. Report core timing separately from conversion, present/DMA, memory,
input and visual inspection; desktop estimates cannot substitute for device
evidence.
