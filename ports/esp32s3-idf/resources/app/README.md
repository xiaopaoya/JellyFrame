# ESP32-S3 App Resource

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Local app resource bundle used by the ESP32-S3 smoke demo and board bring-up
profiles.

Use this directory by fixture purpose: `p2_smoke` and `timer` are bring-up
smokes, `band_shell` is a static shell fixture, `gradient_fastpath` and
`scroll_bench_*` are performance workloads, and `resource_failure`/
`image_acceptance` are negative-path checks. They are not public app-author
templates; use `tools/templates/apps/` for those.

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
- `/resource_failure.html` deliberately requests missing local resources while
  preserving an inline fallback. It verifies that bounded resource failures do
  not prevent a first paint or reset the board.
- `/image_acceptance.html` and `image_acceptance/` exercise the opt-in BMP
  adapter with one valid 24-bit BMP plus missing, corrupt, oversized and
  unsupported inputs. They are acceptance fixtures, not a declaration of
  general PNG/JPEG/WebP support.
