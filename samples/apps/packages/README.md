# App Examples

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

Current packages:

- `watch_weather`: watch weather app with package resources and optional data.
- `jelly_controls`: Jelly UI controls and motion style sample.
- `jelly_motion_lab`: LVGL-style motion validation app with icon-to-window,
  sheet and button jelly animations.
- `jelly_service_status`: optional network/audio/location service-boundary
  sample with system events and local storage.
- `jelly_audio_smoke`: package audio resource used by the Win32 host-owned
  audio smoke path.
- `jelly_font_policy`: package font-family and `.jffont` supplement policy
  sample.
