# Tools

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Desktop developer tools for packaging, validation and editor integration.

Choose by task before opening the directory:

| Goal | Start here | Audience |
| --- | --- | --- |
| Create, check, preview or package an app | `jellyframe_cli.py`, `package_app.py`, `templates/` | App author |
| Inspect pixels, layout, events or a frame script | `native/README.md` | App author, UI reviewer |
| Validate installed apps and launcher recovery | `jellyframe_cli.py`, `app_registry.py`, `schemas/` | Host/runtime developer |
| Check build slicing, link ownership or desktop speed | `render_core_feature_registry.py`, `check_render_core_link_map.py`, `benchmark_guard.py` | Render Core maintainer |
| Refresh HTML/CSS audit tables | `generate_html_support_table.py`, `generate_css_support_table.py`, `import_css_support_crosswork.py` | Compatibility maintainer |
| Work in VS Code | `vscode-jellyframe/README.md` | App author, extension maintainer |
| Validate a board | `ports/<port>/README.md` and `docs/porting_work_guide.md` | Port maintainer |

## Tool Groups

### App Author Workflow

- `jellyframe_cli.py`: `new`, `check`, `preview`, `package`, `doctor`, `trial`
  and registry operations. This is the normal public entry point.
- `package_app.py`: lower-level package builder/validator used by the CLI and
  CI. Use it directly when diagnosing manifest or resource projection.
- `templates/`: small app-author starting points; see its README before copying
  a template.
- `presets/targets/`: conservative target shape, budget and host-service
  profiles consumed by `check`, `preview` and `package`.
- `schemas/`: machine-readable manifest and registry contracts; these are input
  validation assets, not runtime code.

### Desktop Inspection And Acceptance

The C++ programs under `native/` are built as desktop executables. Use its
README's tool table to choose between the pseudo browser, Win32 shell and
individual dumpers. They provide desktop evidence only; panel, DMA, MCU timing
and real font-backend claims remain port-owned.

### Core Build And Audit

- `render_core_feature_registry.py`: shared feature-family catalog used by CMake
  and package/profile checks.
- `check_render_core_link_map.py`: verifies that a generated profile matches
  link-map-visible family ownership; it does not measure firmware performance.
- `benchmark_guard.py`: broad desktop regression guard for catastrophic changes,
  not an MCU release baseline.

### Compatibility And Editor Data

- `generate_html_support_table.py` and `generate_css_support_table.py` generate
  searchable standards snapshots.
- `import_css_support_crosswork.py` imports the CSS audit input; review the
  capability matrix before changing a status.
- `vscode-jellyframe/` delegates to the CLI and does not implement a second
  parser or packer.

Tools may use Python, Node.js or desktop file I/O. The embedded runtime must not
depend on them.

Useful developer entry points:

```powershell
python tools\jellyframe_cli.py doctor --build-dir build\Release
python tools\jellyframe_cli.py trial --build-dir build\Release --output-dir build\external_trial --clean
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --report build\watch_weather.report.json --build-dir build\Release
python tools\jellyframe_cli.py check --root samples\apps\packages\jelly_canvas_smoke --target round-300 --report build\canvas.report.json --build-dir build\Release --render-core-profile build\Release\generated\jellyframe_render_core_profile.json
python tools\jellyframe_cli.py preview --root samples\apps\packages\watch_weather --target round-300 --output build\watch_weather.bmp --build-dir build\Release
python tools\benchmark_guard.py --build-dir build\Release --report build\benchmark_guard.report.json
python tools\check_render_core_link_map.py --profile build\Release\generated\jellyframe_render_core_profile.json --map build\Release\jellyframe_render_core_microbench.map
```

For an embedded benchmark that does not call every enabled family, add one or
more `--used-feature` arguments together with `--scope-used-features`. Enabled
but unexercised families are then reported as `not-tested`, because linker
garbage collection may remove them. For a workload that calls no optional
family, use `--scope-used-features` by itself. The feature smoke workload must
still pass its own symbol check. Without this option the checker remains strict
and requires every profile-enabled marker.

`doctor` is the broad repository smoke check for trial users. It validates every
complete sample package, runs the render-core preflight path over common
wearable targets and leaves JSON reports under `build/doctor_reports`.
Use `--sample name` or `--exclude-sample name` when you only want a focused
subset; both options may be repeated or passed comma-separated names.

`trial` is the reproducible Windows release-evidence flow, not an app-author
command. It requires an explicitly empty output directory (or `--clean`) and
writes a portable `external_trial.report.json` plus all generated reports,
bundles and previews there. It verifies the strict official sample set, a
template create/check loop, a three-target showcase, one intentional capability
rejection, and install/update/rollback followed by a Win32 launch. It adds no
runtime feature or embedded target cost.

Use `--runtime-log` to merge Win32 frame-script/capture counters into a package
report, and `--port-telemetry` to merge real board data such as frame time, DMA
wait, flush-done time and internal-RAM peaks. Both stay in desktop tooling and do
not add embedded runtime cost.

`package`, `check` and `preview` accept `--rasterize-svg --svg-raster-size 32`
for the documented static icon subset. The package step converts static local
HTML/CSS SVG references to generated BMP resources; it does not add an SVG
parser or decoder to the target runtime.

When `--render-core-profile` is supplied, `package_app.py` validates the
profile's feature dependency closure before checking an App. For example,
`graphics.canvas2d` requires `core.paint`, which in turn requires
`core.document`; an incomplete or stale profile is rejected before resources
are read. Apps still declare only the feature family they directly require.
