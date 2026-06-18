# Ports

Port-support code and board-facing experiments live here.

The platform-neutral engine remains in `src/`. Port directories may contain
hardware glue, generated resources, board build files and validation demos.

Current port areas:

- `virtual_board/`: desktop virtual-board benchmark and presentation harness.
- `esp32s3-idf/`: ESP32-S3 bring-up project and static-resource experiment.

LVGL or vendor SDK integration should remain a thin optional adapter around
panel/input/text hooks, not a replacement UI framework inside JellyFrame.
