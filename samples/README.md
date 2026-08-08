# Samples

> Last updated: 2026-07-14; Applies to: 0.5.0

Root samples are for JellyFrame apps and app-package lifecycle validation.
Native desktop tools live in `../tools/native`.

- `apps/packages/`: complete JellyFrame source packages with
  `jellyframe.app.json`.
- `apps/system/`: privileged system-app samples such as the sample launcher used
  by the Win32 app-manager host path and the 172x320 Band System Shell.
- `apps/loose/`: small loose-file app fixtures used for focused runtime,
  scripting and rendering checks.

Current visual-system samples:

- `apps/packages/jelly_controls`: installable Jelly UI controls package.
- `apps/packages/jelly_wearable_launcher`: round-300 icon-first wearable launcher package.
- `apps/system/band_system_shell`: rect-172x320 wearable system-shell visual
  and input acceptance sample.
- `apps/loose/jelly_motion.html`: transition/keyframe motion fixture.
- `apps/loose/jelly_launcher_mock.html`: launcher grid visual fixture.

Render-core-only pages now live under `../src/render_core/samples`.
JerryScript probes now live under `../src/script/samples`.
App-author starting points belong in `../tools/templates/apps`; samples are for
validation, screenshots and regressions.
