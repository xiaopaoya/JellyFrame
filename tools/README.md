# Tools

> Last updated: 2026-07-07; Applies to: 0.5.0-dev

Desktop developer tools for packaging, validation and editor integration.

- `jellyframe_cli.py`: command-line helper for app/package workflows.
- `package_app.py`: package builder and validator.
- `benchmark_guard.py`: runs selected desktop microbenchmarks and checks broad
  CI regression thresholds. It catches catastrophic slowdowns; it is not a
  release performance baseline.
- `native/`: C++ inspection tools, pseudo browser, Win32 shell and font-pack
  generator.
- `vscode-jellyframe/`: VS Code extension helper.

Tools may use Python, Node.js or desktop file I/O. The embedded runtime must not
depend on them.

Useful developer entry points:

```powershell
python tools\jellyframe_cli.py doctor --build-dir build\Release
python tools\jellyframe_cli.py check --root samples\apps\packages\watch_weather --targets round-300,rect-320x240,rect-172x320 --report build\watch_weather.report.json --build-dir build\Release
python tools\jellyframe_cli.py preview --root samples\apps\packages\watch_weather --target round-300 --output build\watch_weather.bmp --build-dir build\Release
python tools\benchmark_guard.py --build-dir build\Release --report build\benchmark_guard.report.json
```

`doctor` is the broad repository smoke check for trial users. It validates every
complete sample package, runs the render-core preflight path over common
wearable targets and leaves JSON reports under `build/doctor_reports`.
