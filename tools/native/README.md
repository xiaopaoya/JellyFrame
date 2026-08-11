# Native Tools

> Last updated: 2026-08-11; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This directory contains the native C++ implementation of JellyFrame's desktop
inspection tools. App authors should start with `../debug/README.md`; sample
pages and app packages live in `../../samples`.

## Choose A Tool

| Need | Source / executable | Output |
| --- | --- | --- |
| Render one page or package with structured diagnostics | `pseudo_browser.cpp` | BMP/PPM plus DOM/layout/paint diagnostics |
| Interact with an app, inject events or capture frames | `win32_browser.cpp` -> `jellyframe_desktop_shell` | Desktop shell output, frame scripts and runtime telemetry |
| Inspect one pipeline layer | `dom_dump.cpp`, `cssom_dump.cpp`, `render_tree_dump.cpp`, `layer_tree_dump.cpp`, `pipeline_dump.cpp` | Focused text dumps |
| Inspect style resolution | `style_dump.cpp` | Matched/computed style details |
| Generate or inspect bitmap font resources | `font_pack_gen.cpp`, `font_resource_check.cpp` | Firmware header or `.jffont` and policy report |

The source files are build inputs, not separate user-facing commands until
CMake emits the corresponding executable. Use
`jellyframe_desktop_shell --help` for the shell contract.

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
event 11 escape
event 12 time-ms 1700000000123
event 14 battery 88 1
event 16 weather 213 rain
event 18 activity 6400 32
event 20 click-id theme-toggle
event 20 set-checked theme-toggle 1
event 22 set-value display-name Focus%20mode
event 24 select-index accent 2
animation-fps 30
animation-callbacks 4
script-watchdog-checks 2048
script-watchdog-interval 16
```

JavaScript/rAF playback requires a build configured with
`JELLYFRAME_BUILD_SCRIPTING=ON`; CSS animation capture works in non-scripting
builds.

Use `--force-full-repaint` only as a desktop correctness oracle. Running the
same deterministic frame script once normally and once with this flag should
produce byte-identical BMP frames. A mismatch indicates retained invalidation
or incremental compositing drift; it is not an acceptable rendering downgrade.

Use `animation-fps 0` and `animation-callbacks 0` in a frame script, or pass
`--animation-fps 0 --animation-callbacks 0`, to validate low-power profiles
where the host must stop nonessential motion without changing app source.

## Semantic Interaction Capture

For stable control regression, prefer the semantic events <code>click-id</code>,
<code>set-value</code>, <code>set-checked</code> and <code>select-index</code>.
They resolve a visible form control by its stable HTML <code>id</code>, then use
the normal input/controller event path. <code>set-value</code> uses percent
encoding so spaces and punctuation are safe:

~~~text
event 4 click-id notification-toggle
event 4 set-checked notification-toggle 1
event 8 click-id brightness
event 8 set-value brightness 72
event 12 select-index color-scheme 1
~~~

The VS Code embedded debugger can record this form automatically. Give each
recorded button, input and select a unique ASCII <code>id</code>. The recorder
deliberately does not generate coordinate events for ordinary control activation
or range updates. Keep pointer/wheel events for freeform canvas interaction,
scrolling, drag gestures, or controls without a stable id.

## List Drag Acceptance

Use the deterministic list fixture to inspect standard host-style vertical drag
behavior. These are scroll gestures, not HTML Drag and Drop or complete Pointer
Events conformance.

```powershell
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_no_inertia.jfcapture
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_inertia.jfcapture
.\build\desktop-release\Release\jellyframe_desktop_shell.exe --app tests\fixtures\apps\jelly_scroll_container_probe --frame-script tests\fixtures\apps\jelly_scroll_container_probe\capture_touch_drag_edge_stop.jfcapture
```

The scripts write per-frame BMPs and a montage under `out/`. The slow drag must
report `inertia=0`; the flick must report a positive inertia count; the edge
flick must stop once its scroll container reaches its bound. The existing
`capture_touch_scroll_container.jfcapture` remains the compact drag-plus-inertia
regression fixture.

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
