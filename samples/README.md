# Samples

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Root samples are for JellyFrame apps and app-package lifecycle validation.
Native desktop tools live in `../tools/native`.

## Choose The Right Sample

| Need | Directory |
| --- | --- |
| Copy a new app author starting point | `../tools/templates/apps/` |
| Readable installable app and UI examples | `apps/packages/` |
| Privileged launcher/system-shell examples | `apps/system/` |
| Small loose-file visual or scripting probe | `apps/loose/` |
| Render Core-only HTML/CSS page | `../src/render_core/samples/pages/` |
| Reproduce a narrow contract or failure | `../tests/fixtures/apps/` |

Samples under this directory are validation and showcase inputs. They are not
the full browser compatibility contract; use `../docs/developer_capability_matrix.md`
and the searchable support tables before relying on a syntax feature.

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
