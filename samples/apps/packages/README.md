# App Packages

> Last updated: 2026-08-26; Applies to: 0.6.0-dev; Render Core baseline: 0.6.1

Complete JellyFrame source-package examples live here. Each app should include
`jellyframe.app.json`, local HTML/CSS/classic JavaScript and any bounded local
resources needed for preview or packaging.

Use the showcase packages as readable references for app structure and current
visual work. Starter templates copied by the developer CLI live under
`../../../tools/templates/apps`. The remaining packages are acceptance inputs:
they deliberately exercise one bounded capability or host boundary and are not
recommended as app-author starting points.

Several packages declare multiple target profiles. Use the CLI responsive pass
to check whether a package remains usable on the common wearable shapes:

```powershell
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --build-dir build\desktop-release\Release
```

The public showcase set is deliberately small:

- `watch_weather`: package-local images, host-shaped data updates and compact
  responsive status cards.
- `jelly_controls`: native form controls, focus/pressed states and local UI
  state.
- `jelly_motion_lab`: current paint-safe CSS and scripted animation replay.
- `jelly_route_tabs`: bounded app-local route state without navigation.

All four must remain readable, visually reviewed and deterministic under the
current desktop shell. `doctor --trial` remains free to use focused acceptance
packages where it needs a particular contract.

Acceptance packages:

- `jelly_canvas_smoke`: optional Canvas 2D V0.4 trend/bar-chart sample using bounded canvas-to-canvas drawImage scaling, radial highlights and budgeted quadratic/cubic paths.
- `jelly_canvas_gauges`: optional Canvas 2D gauge/ring sample using `arc`,
  `fill`, `globalAlpha`, Canvas text, linear gradients and bounded path drawing.
- `jelly_service_status`: optional network/audio/location service-boundary
  sample with system events and local storage.
- `jelly_audio_smoke`: package audio resource used by the Win32 host-owned
  audio smoke path.
- `jelly_font_policy`: package font-family and `.jffont` supplement policy
  sample with two runtime families, missing-glyph diagnostics and Win32
  `--use-app-fonts` validation.
- `jelly_static_modules`: package-time static local ES-module graph that becomes
  one classic device script before preview or packaging.
- `jelly_component_recipes`: scroll/dirty-region and component-structure
  regression input; its prose recipes live in `docs/app_author_recipes.md`.
- `jelly_watch_face`: transform/radius/gradient timing regression input.
- `jelly_wearable_launcher`: icon-grid paint regression input.
