# Samples

Root samples are for JellyFrame apps and app-package lifecycle validation.
Native desktop tools live in `../tools/native`.

- `apps/packages/`: complete JellyFrame source packages with
  `jellyframe.app.json`.
- `apps/system/`: privileged system-app samples such as the sample launcher used
  by the Win32 app-manager host path.
- `apps/loose/`: small loose-file app fixtures used for focused runtime,
  scripting and rendering checks.

Current visual-system samples:

- `apps/packages/jelly_controls`: installable Jelly UI controls package.
- `apps/loose/jelly_motion.html`: transition/keyframe motion fixture.
- `apps/loose/jelly_launcher_mock.html`: launcher grid visual fixture.

Render-core-only pages now live under `../src/render_core/samples`.
JerryScript probes now live under `../src/script/samples`.
App-author starting points belong in `../tools/templates/apps`; samples are for
validation, screenshots and regressions.
