# Native Tools

This directory contains native C++ desktop tools used to inspect JellyFrame
output. Sample pages and app packages live in `../../samples`.

- `*_dump.cpp` tools print parser, DOM, CSSOM, render tree, layer tree or full
  pipeline output.
- `pseudo_browser.cpp` runs the platform-neutral pipeline in a desktop shell and
  can emit structured pipeline diagnostics with `--diagnostics-json`.
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

Use `script-watchdog-checks N`, `script-watchdog-interval N` and
`require-script-watchdog` only for Win32/scripted recovery validation. They map
to the host script-execution budget and require a JerryScript build with VM halt
support; page authors should not rely on private JavaScript syntax for this.

Native tools may use desktop file I/O. The embedded core does not depend on these
entry points.

`jellyframe_font_pack_gen` generates offline bitmap font packs from BDF input.
It can emit both a firmware C++ `BitmapFont` header and a runtime `.jffont`
supplement. Use `--coverage-bits 1` for the compact monochrome path, or
`--coverage-bits 2|4` for opt-in glyph coverage antialiasing. Coverage fonts
increase glyph row storage and paint-time alpha blending only for apps that
ship or link those fonts.
