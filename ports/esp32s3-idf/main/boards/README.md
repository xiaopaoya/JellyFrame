# ESP32-S3 Board Adapters

Board-local display and touch adapters live here. They are optional ESP-IDF
bring-up code and must stay outside the platform-neutral engine.

Adapters should expose small profile/runtime structs to `main.cpp`, keep
hardware initialization behind Kconfig gates, and provide only framebuffer flush
or input hooks needed by the validation app.

Current adapters:

- `waveshare_touch_lcd_boards.*`: bring-up profile for the Waveshare
  ESP32-S3-Touch-LCD-1.47 board. It initializes the JD9853 SPI LCD, probes the
  AXS5106L touch controller and presents JellyFrame's RGB565 dirty rectangles
  through a packed panel flush callback. Touch is intentionally still a probe
  path; production input should translate board events into the normal
  `BoardInputQueue` flow.
