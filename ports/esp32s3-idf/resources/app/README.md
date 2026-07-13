# ESP32-S3 App Resource

> Last updated: 2026-07-13; Applies to: 0.5.0-dev

Local app resource bundle used by the ESP32-S3 smoke demo and board bring-up
profiles.

This mirrors the package shape used by JellyFrame apps while avoiding filesystem
or network dependencies on the board.

- `/p2_smoke.html` remains the default smoke-test entry in
  `jellyframe.app.json`.
- `/timer.html` is a small 172x320 Waveshare 1.47 bring-up page. It is packaged
  as an additional resource, not as the default entry.
- `/scroll_bench_panel.html` is the Phase D A/B-only full-screen retained list.
  Its single opaque rectangular scroll viewport is deliberately stricter than
  the normal scroll samples so a physical-GRAM panel callback can prove that
  each scroll step has exactly one exposed strip. Its 960 px content extent
  makes the 320 px viewport cross the physical GRAM ring in both directions.
