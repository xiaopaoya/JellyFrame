# JellyFrame Tools for VS Code

This is a thin developer extension for JellyFrame app packages. It does not
implement its own parser or packer; every command delegates to
`tools/jellyframe_cli.py`.

## Features

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for validate, capability check, preview and package.
- App creation from the built-in weather, clock, timer and calculator templates.
- CLI output in a dedicated `JellyFrame` output channel.
- Configurable repository root, Python executable, default target and font
  budget.

## Development Use

Open this folder in VS Code extension development mode, or point
`jellyframe.repoRoot` at the JellyFrame repository when running from another
location. The extension expects the normal JellyFrame build output under
`build/Release`.
