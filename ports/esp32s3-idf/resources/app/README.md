# ESP32-S3 App Resource

> Last updated: 2026-07-16; Applies to: 0.5.0-dev

Local app resource bundle used by the ESP32-S3 smoke demo and board bring-up
profiles.

This mirrors the package shape used by JellyFrame apps while avoiding filesystem
or network dependencies on the board.

- `/p2_smoke.html` remains the default smoke-test entry in
  `jellyframe.app.json`.
- `/timer.html` is a small 172x320 Waveshare 1.47 bring-up page. It is packaged
  as an additional resource, not as the default entry.
- `/band_shell.html` is a static 172x320 wearable system-shell fixture. The
  board UI task owns its narrow navigation binding, so it validates visual
  composition and touch routing without enabling JerryScript. It mirrors the
  source-package shell, including bounded gradient depth, outline-offset focus
  rings and pressed transform feedback; it does not keep a route-animation
  loop alive while idle.
- `/gradient_fastpath.html` is a single opaque, square full-screen linear
  gradient. Its opt-in native task repeats full presentation at 30 Hz for
  renderer/panel A/B evidence; it is not a user-facing sample or shell route.
- `/scroll_bench_panel.html` is the Phase D A/B-only full-screen retained list.
  Its single opaque rectangular scroll viewport is deliberately stricter than
  the normal scroll samples so a physical-GRAM panel callback can prove that
  each scroll step has exactly one exposed strip. Its 960 px content extent
  makes the 320 px viewport cross the physical GRAM ring in both directions.
