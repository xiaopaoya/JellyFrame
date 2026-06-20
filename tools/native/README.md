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

Frame scripts are line-oriented and intentionally tiny:

```text
output-dir out/motion_lab_frames
montage out/motion_lab_montage.bmp
frames 30
step-ms 33
viewport 300 300
event 8 click 150 260
```

JavaScript/rAF playback requires a build configured with
`JELLYFRAME_BUILD_SCRIPTING=ON`; CSS animation capture works in non-scripting
builds.

Native tools may use desktop file I/O. The embedded core does not depend on these
entry points.
