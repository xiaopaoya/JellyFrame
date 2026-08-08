# ESP32-S3 Board Adapters

> Last updated: 2026-07-14; Applies to: 0.5.0

Board-local display and touch adapters live here. They are optional ESP-IDF
bring-up code and must stay outside the platform-neutral engine.

Adapters should expose small profile/runtime structs to `main.cpp`, keep
hardware initialization behind Kconfig gates, and provide only framebuffer flush
or input hooks needed by the validation app.

Current adapters:

- `waveshare_touch_lcd_boards.*`: bring-up profile for the Waveshare
  ESP32-S3-Touch-LCD-1.47 board. It initializes the JD9853 SPI LCD, probes the
  AXS5106L touch controller and presents JellyFrame's RGB565 dirty rectangles
  through a packed panel flush callback. The adapter waits for the LCD color
  transfer-done callback before reusing the DMA strip buffer, keeps the
  backlight off until the first app frame is flushed, and can attach touch
  events to the normal `BoardInputQueue` flow.
- `waveshare_touch_lcd_169_board.cpp`: board-local profile for the Waveshare
  ESP32-S3-Touch-LCD-1.69. It uses the ESP-IDF ST7789 driver with its
  vendor-documented 240x280 geometry and 20-row RAM offset, bounded internal
  DMA strips and a CST816T pointer stream. The disabled-by-default A/B fixture
  uses ST7789 `VSCRDEF`/`VSCSAD` with a `20/280/20` GRAM split to submit only
  the exposed strip. It is never selected for ordinary app UI and resets the
  panel scroll address before every normal recovery present.
