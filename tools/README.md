# Tools

> Last updated: 2026-08-02; Applies to: 0.5.0-dev

Desktop developer tools for packaging, validation and editor integration.

- `jellyframe_cli.py`: command-line helper for app/package workflows.
- `package_app.py`: package builder and validator.
- `benchmark_guard.py`: runs selected desktop microbenchmarks and checks broad
  CI regression thresholds. It catches catastrophic slowdowns; it is not a
  release performance baseline.
- `check_render_core_link_map.py`: compares feature-family markers in a desktop
  linker map with the generated Render Core profile. It proves build selection,
  not embedded flash/RAM or panel performance.
- `native/`: C++ inspection tools, pseudo browser, Win32 shell and font-pack
  generator.
- `schemas/`: JSON Schemas for app manifests and desktop/system-shell tool
  inputs such as the installed-app registry mock.
- `vscode-jellyframe/`: VS Code extension helper.

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
