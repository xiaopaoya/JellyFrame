# JellyFrame Tools for VS Code

> Last updated: 2026-08-10; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

JellyFrame Tools is a VS Code extension for app authors. It brings package
checks, previews, desktop debugging and packaging into the editor, with a
JellyFrame menu, focused context menus and the Command Palette as entry points.

## Features

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for package-structure validation, render preflight, preview, embedded VS Code debugging,
  external-window debugging, frame-script playback, capture opening and package generation.
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
- A `JellyFrame` menu in the VS Code menubar, plus focused context-menu
  actions for `jellyframe.app.json` and HTML/CSS files.

## Using The Extension

The repository currently provides the extension as source; it is not yet listed
on the VS Code Marketplace. To try it with the least setup:

1. Build a Release configuration from the JellyFrame repository root so that
   `build/Release` exists.
2. Open `tools/vscode-jellyframe` in VS Code.
3. Press `F5` to launch an Extension Development Host. Open a JellyFrame
   repository there, or set `jellyframe.repoRoot` to the repository root.
4. Open `jellyframe.app.json` or an app HTML/CSS file, then use the `JellyFrame`
   menubar menu or the context menu.

To package, install or update it like a regular local extension, run the helper
script in the extension folder:

```powershell
.\manage-extension.ps1
```

The default action repackages the current source and force-updates the installed
extension. The individual actions are also available:

```powershell
.\manage-extension.ps1 -Action Package
.\manage-extension.ps1 -Action Install
.\manage-extension.ps1 -Action Update
```

The script requires Node.js `npx` (or an available `pnpm` fallback) and the VS
Code `code` command on `PATH`; pass `-NpxCommand` or `-CodeCommand` to provide
an explicit executable path. If PowerShell blocks local scripts, run
`Set-ExecutionPolicy -Scope Process Bypass` in the current window. You can still
use the Extensions view's `Install from VSIX...` action and select the generated
`.vsix`. When the extension is installed outside the
repository, set `jellyframe.repoRoot`; `jellyframe.buildDir` is optional. The
extension prefers `build/Release`, then `build/Debug`.
For an app whose manifest declares `runtime.script`, the extension prefers an
existing `build/scripting-ci-local` or other scripting build unless
`jellyframe.buildDir` is explicitly set.

Use `JellyFrame: Show Last Report` to reopen the latest report panel.

`JellyFrame: Validate App Package` is the fast, package-only gate. It checks the
manifest, entry point, local resources, references and declared budgets without
starting Render Core or asking for a viewport, measuring layout, frame time or
device performance. Its report is intentionally limited to package structure.
`JellyFrame: Check App Rendering` runs the package gate first, asks for a target
viewport, then adds Render Core preflight, responsive layout and font checks. It
also offers optional `.jfcapture` programmed playback, merging the static
pipeline diagnostics with a multi-page interaction path. Use Preview or desktop
debugging for the actual image and interactive behavior.

The top-level `JellyFrame` menu groups package, debugging and report actions.
The Explorer context menu is available for `jellyframe.app.json`; the editor
context menu is available while editing an app HTML, CSS or manifest file.
These entries use the same commands as the Command Palette, so either entry
point produces the same report and output-channel behavior.

`JellyFrame: Debug App In VS Code` opens an editor tab backed by an isolated,
hidden desktop-shell session. It delivers complete viewport snapshots with
strictly increasing sequence numbers and forwards pointer, drag, wheel and
common-key input only to that session. Both Stop and closing the tab request a
clean shell exit; the extension terminates the debug process tree after a short
grace period if needed. It does not share a framebuffer, capture path or process
with external-window debugging. Use `JellyFrame: Debug App In External Window`
when native-window behavior itself is relevant.

The embedded debugger also includes a Record button for building a semantic
<code>.jfcapture</code>. Start recording, interact with the app, then stop
recording and choose a save location. During recording, Live log switches to
Events and records stable control actions rather than pixel coordinates:

~~~text
event 3 click-id notifications
event 3 set-checked notifications 1
event 7 click-id brightness
event 7 set-value brightness 72
~~~

Give every button, input and select intended for recording a unique ASCII
<code>id</code>. This keeps a capture valid when spacing, scale or layout
changes. The wizard intentionally leaves scrolling, freeform canvas gestures
and controls without a stable id to hand-authored pointer/wheel events. Open
the saved file with Run Frame Script, or select it for programmed playback in
Check App Rendering.

Use `JellyFrame: Run Frame Script` for deterministic playback. `JellyFrame: Open
Capture` opens the last or a selected BMP/PPM capture. `JellyFrame: Preview
Package` runs package preflight, writes a separate JSON report and automatically
opens the generated capture. Validation, checking and preview reports are kept
separate so one command does not overwrite another command's result.
