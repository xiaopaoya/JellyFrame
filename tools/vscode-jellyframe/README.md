# JellyFrame Tools for VS Code

> Last updated: 2026-08-10; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This is a thin developer extension for JellyFrame app packages. It does not
implement its own parser or packer; every command delegates to
`tools/jellyframe_cli.py`.

## Features

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for validate, check, preview, desktop-shell debugging,
  frame-script playback, capture opening and package generation.
- App creation from the built-in weather, clock, timer and calculator templates.
- CLI output in a dedicated `JellyFrame` output channel.
- A `JellyFrame Report` webview that puts CLI `developerAdvice[]` first, then
  summarizes resources, references, warnings and pipeline diagnostics.
- Inline diagnostics for app-author advice, package warnings and pipeline
  diagnostics.
- Explorer status view showing the selected app, build, report diagnostics and
  measured performance summary.
- Automatic discovery of `build/Release`, `build/Debug` and the desktop shell,
  with a setting for an explicit build directory.
- Configurable repository root, Python executable, default target and font
  budget.

## Development Use

Open this folder in VS Code extension development mode, or point
`jellyframe.repoRoot` at the JellyFrame repository when running from another
location. The extension prefers `build/Release`, then `build/Debug`; both can
be overridden in settings.

Use `JellyFrame: Show Last Report` to reopen the latest report panel.

Use `JellyFrame: Debug App In Desktop Shell` for interactive app debugging and
`JellyFrame: Run Frame Script` for deterministic playback. `JellyFrame: Open
Capture` opens the last or a selected BMP/PPM capture. `JellyFrame: Preview
Package` remains the package preflight/pseudo-browser path and writes a JSON
report linked to diagnostics.

Audience: app authors who want editor entry points, not extension developers
reimplementing the runtime. All capability and packaging behavior remains
owned by `tools/jellyframe_cli.py`.
