# Native Tools

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

This directory contains native C++ desktop tools used to inspect JellyFrame
output. Sample pages and app packages live in `../../samples`.

- `*_dump.cpp` tools print parser, DOM, CSSOM, render tree, layer tree or full
  pipeline output.
- `pseudo_browser.cpp` runs the platform-neutral pipeline in a desktop shell and
  can emit structured pipeline diagnostics with `--diagnostics-json`, including
  development-time visual hints for horizontal overflow, scroll-needed content
  and high display-command density.
- `win32_browser.cpp` is the Windows validation shell with OS input and capture
  support. It can open either loose HTML/CSS files or a source package via
  `--app`. Use `--frame-script PATH` for hidden deterministic animation capture
  with scripted time, event injection, per-frame BMP output and an optional
  contact-sheet image. Lower-level `--capture-frames DIR --frame-count 30
  --frame-step-ms 33` and repeated `--frame-event` arguments remain available
  for quick smoke tests.
- `win32_browser.cpp` also has a Win32-only host audio smoke path:
  `--audio-smoke local.wav` or `--app package --audio-smoke /audio/tone.wav`.
  This validates package-resource handoff to the desktop host adapter; it does
  not add an embedded audio codec or a public JavaScript audio API.
- Hidden frame capture prints host-completion, system-event, frame-policy and
  service-activity counters, a per-app budget snapshot and scroll blit counts.
  Use those counters to validate that manifest `backgroundServices`,
  screen-off and low-power policies pause or keep network/audio/sensor/location
  work without making the render core hardware-aware.
- Hidden frame capture also prints `present_estimate_rgb565`. This is an
  embedded-output estimate for a 16-bit RGB565 panel, not Win32 GDI work. It
  accounts for the same full-frame, dirty-rect and scroll-strip presents that
  the shell blits on desktop, and reports estimated flush count, converted
  pixels and tightly packed bytes. Use it to compare Win32 frame-script runs
  with board-side `embedded_framebuffer`/panel logs before changing dirty-region
  or scroll reuse logic.
- Debug image decode and debug network fetch are pumped through
  `pump_app_host_service_workers(...)`, matching the request/completion boundary
  recommended for MCU ports.

Frame scripts are line-oriented and intentionally tiny:

```text
output-dir out/motion_lab_frames
montage out/motion_lab_montage.bmp
frames 30
step-ms 33
viewport 300 300
event 8 click 150 260
event 10 wheel 150 160 -120
event 12 time-ms 1700000000123
event 14 battery 88 1
event 16 weather 213 rain
event 18 activity 6400 32
animation-fps 30
animation-callbacks 4
script-watchdog-checks 2048
script-watchdog-interval 16
```

JavaScript/rAF playback requires a build configured with
`JELLYFRAME_BUILD_SCRIPTING=ON`; CSS animation capture works in non-scripting
builds.

Use `animation-fps 0` and `animation-callbacks 0` in a frame script, or pass
`--animation-fps 0 --animation-callbacks 0`, to validate low-power profiles
where the host must stop nonessential motion without changing app source.

Use `event FRAME time-ms VALUE` to inject deterministic host time for
`Date.now()`-driven watch faces, timers and weather samples. This is a shell
debug command, not app-visible syntax.

Use `event FRAME battery PERCENT CHARGING`, `event FRAME weather TEMP_C_X10
CONDITION`, `event FRAME activity STEPS ACTIVE_MINUTES`, `event FRAME location
LATITUDE LONGITUDE ACCURACY_M` and `event FRAME sensor KIND VALUE [Y Z]` to
validate the host-data snapshot path in Win32 captures. These update the shell's
filtered debug summary. `battery`, `weather` and `activity` are additionally
visible through `navigator.jellyframe.getSnapshot()` only when the app manifest
declares the matching `system.*` capability; location and sensor injection do
not create JavaScript sensor APIs.

Use `script-watchdog-checks N`, `script-watchdog-interval N` and
`require-script-watchdog` only for Win32/scripted recovery validation. They map
to the host script-execution budget and require a JerryScript build with VM halt
support; page authors should not rely on private JavaScript syntax for this.

Native tools may use desktop file I/O. Desktop CMake builds expose framebuffer
image writers through `JELLYFRAME_ENABLE_IMAGE_FILE_IO=ON`; embedded or RTOS
ports can leave that definition disabled so render_core does not expose file
writer entry points.

`jellyframe_font_pack_gen` generates offline bitmap font packs from BDF input.
It can emit both a firmware C++ `BitmapFont` header and a runtime `.jffont`
supplement. Use `--coverage-bits 1` for the compact monochrome path, or
`--coverage-bits 2|4` for opt-in glyph coverage antialiasing. Coverage fonts
increase glyph row storage and paint-time alpha blending only for apps that
ship or link those fonts.
