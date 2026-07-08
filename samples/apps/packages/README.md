# App Examples

> Last updated: 2026-07-09; Applies to: 0.5.0-dev

Complete JellyFrame source-package examples live here. Each app should include
`jellyframe.app.json`, local HTML/CSS/classic JavaScript and any bounded local
resources needed for preview or packaging.

Use these examples to validate runtime behavior and visual acceptance. Starter
templates copied by the developer CLI live under `../../../tools/templates/apps`.

Several packages declare multiple target profiles. Use the CLI responsive pass
to check whether a package remains usable on the common wearable shapes:

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --build-dir build\Release
```

`watch_weather`, `jelly_controls`, `jelly_component_recipes` and
`jelly_watch_face` are the primary showcase/recipe packages and should keep
their target gates aligned with current trial targets. Capability smoke packages
such as audio, font policy, motion stress and focused Canvas demos may
intentionally omit hard gates or carry explainable warnings because they exist
to exercise one subsystem rather than represent a polished release app. The
default `doctor` still scans them so subsystem smoke packages cannot hide
errors; judge trial showcase quality primarily from the gated showcase/recipe
packages.

Current packages:

- `watch_weather`: watch weather app with package resources and optional data.
- `jelly_controls`: Jelly UI controls and motion style sample.
- `jelly_component_recipes`: copyable small-screen button, card, scroll-list
  and fixed bottom-navigation recipes.
- `jelly_motion_lab`: LVGL-style motion validation app with icon-to-window,
  sheet and button jelly animations.
- `jelly_watch_face`: analog watch face using `transform: rotate(...)` and
  `transform-origin` for hands.
- `jelly_canvas_smoke`: optional Canvas 2D V0.3 trend-line and bar-chart sample.
- `jelly_canvas_gauges`: optional Canvas 2D gauge/ring sample using `arc`,
  `fill`, `globalAlpha`, Canvas text, linear gradients and bounded path drawing.
- `jelly_service_status`: optional network/audio/location service-boundary
  sample with system events and local storage.
- `jelly_audio_smoke`: package audio resource used by the Win32 host-owned
  audio smoke path.
- `jelly_font_policy`: package font-family and `.jffont` supplement policy
  sample with two runtime families, missing-glyph diagnostics and Win32
  `--use-app-fonts` validation.
