# Samples

> Last updated: 2026-09-04; Applies to: 0.6.0-dev; Render Core baseline: 0.6.2

Root samples are for JellyFrame apps and app-package lifecycle validation.
Native desktop tools live in `../tools/native`.

## Choose The Right Sample

| Need | Directory |
| --- | --- |
| Copy a new app author starting point | `../tools/templates/apps/` |
| Readable installable app showcase and acceptance packages | `apps/packages/` |
| Privileged launcher/system-shell examples | `apps/system/` |
| Render Core-only HTML/CSS page | `../src/render_core/samples/pages/` |
| Reproduce a narrow contract or failure | `../tests/fixtures/apps/` |

Samples under this directory are validation and showcase inputs. They are not
the full browser compatibility contract; use `../docs/developer_capability_matrix.md`
and the searchable support tables before relying on a syntax feature.

- `apps/packages/`: complete JellyFrame source packages with
  `jellyframe.app.json`. The directory holds both a deliberately small public
  showcase set and focused acceptance packages; its README identifies which is
  which.
- `apps/system/`: privileged system-app samples such as the sample launcher used
  by the Win32 app-manager host path and the 172x320 Band System Shell.
Public showcase packages are `watch_weather`, `jelly_controls`,
`jelly_motion_lab` and `jelly_route_tabs`. The privileged
`apps/system/band_system_shell` remains a 172x320 system-shell acceptance
sample, not an app-author starting point.

Render-core-only pages now live under `../src/render_core/samples`.
JerryScript probes now live under `../src/script/samples`.
App-author starting points belong in `../tools/templates/apps`; samples are for
validation, screenshots and regressions. The historical loose-file visual
probes were removed: package manifests and deterministic capture scripts are
the current validation form.
