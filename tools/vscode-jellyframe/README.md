# JellyFrame Tools for VS Code

This is a thin developer extension for JellyFrame app packages. It does not
implement its own parser or packer; every command delegates to
`tools/jellyframe_cli.py`.

## Features

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for validate, check, preview, interactive Win32
  browser launch and package generation.
- App creation from the built-in weather, clock, timer and calculator templates.
- CLI output in a dedicated `JellyFrame` output channel.
- A `JellyFrame Report` webview that summarizes the latest package report,
  resources, references, warnings and pipeline diagnostics.
- Inline diagnostics for actionable package warnings and pipeline diagnostics.
- Configurable repository root, Python executable, default target and font
  budget.

## Development Use

Open this folder in VS Code extension development mode, or point
`jellyframe.repoRoot` at the JellyFrame repository when running from another
location. The extension expects the normal JellyFrame build output under
`build/Release`.

Use `JellyFrame: Show Last Report` to reopen the latest report panel.

Use `JellyFrame: Open In Win32 Browser` for interactive app debugging. Use
`JellyFrame: Preview Package` for deterministic pseudo-browser capture; preview
also writes a JSON report and updates the report panel.
