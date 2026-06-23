# ESP32-S3 App Resource

Local app resource bundle used by the ESP32-S3 smoke demo and board bring-up
profiles.

This mirrors the package shape used by JellyFrame apps while avoiding filesystem
or network dependencies on the board.

- `/p2_smoke.html` remains the default smoke-test entry in
  `jellyframe.app.json`.
- `/timer.html` is a small 172x320 Waveshare 1.47 bring-up page. It is packaged
  as an additional resource, not as the default entry.
