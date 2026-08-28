# JellyFrame Tools for VS Code

> Last updated: 2026-08-29; Applies to: 0.6.0-dev; extension version: 0.4.30; compatibility baseline: 0.5.0

JellyFrame Tools is a VS Code extension for app authors. It brings package
checks, previews, desktop debugging and packaging into the editor, with a
dedicated JellyFrame Activity Bar view, focused context menus and the Command
Palette as entry points.

## Features

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for package-structure validation, render preflight, preview, embedded VS Code debugging,
  external-window debugging, frame-script playback, capture opening and package generation.
- App creation from the built-in blank, weather, clock, timer and calculator templates.
- CLI output in a dedicated `JellyFrame` output channel.
- A `JellyFrame Report` webview that puts CLI `developerAdvice[]` first, then
  summarizes resources, references, warnings and pipeline diagnostics.
- Inline diagnostics for app-author advice, package warnings and pipeline
  diagnostics.
- Explorer status view showing the selected app, build, report diagnostics and
  measured performance summary.
- A one-time author-environment setup that selects an installed JellyFrame SDK
  for independent App workspaces.
- Download and installation of the latest GitHub App Author SDK with SHA-256 verification.
- Automatic discovery of the SDK's desktop shell build, with explicit build
  directory override when required.
- Configurable SDK root, Python executable, default target and font budget.
- A dedicated `JellyFrame` Activity Bar view with app, build, report and
  diagnostic actions, plus focused context-menu actions for
  `jellyframe.app.json` and HTML/CSS files.
- Capability-gated Device OS lifecycle actions that remain hidden until a
  selected provider explicitly declares support for them.

## Using The Extension

The repository currently provides the extension as source; it is not yet listed
on the VS Code Marketplace. To try it with the least setup:

1. Install the extension, then open an independent App workspace.
2. Click **Author environment: Not configured** in the JellyFrame Activity Bar.
   Choose **Download App Author SDK from GitHub** to download the official SDK,
   verify its SHA-256 digest, safely extract it and configure the environment;
   choose **Select an installed JellyFrame SDK** when one is already available.
3. Once configured, **Author environment** displays the SDK version. Click it
   to check for updates, switch SDKs, or open the selected SDK folder.
4. Click the JellyFrame icon in the Activity Bar, or open
   `jellyframe.app.json` or an app HTML/CSS file and use the context menu.

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

The script prefers Node.js `vsce`, `npx` (or an available `pnpm` fallback) for packaging; when none is available,
it uses its built-in VSIX packager. Installation and updates still need the VS Code `code` command on `PATH`; pass
`-CodeCommand` to provide an explicit CLI path (normally `...\Microsoft VS Code\bin\code.cmd`). Passing `Code.exe`
is accepted only when the adjacent CLI is installed, in which case the script redirects to it. If PowerShell blocks local scripts, run
`Set-ExecutionPolicy -Scope Process Bypass` in the current window. You can still
use the Extensions view's `Install from VSIX...` action and select the generated
`.vsix`. When the extension is installed outside the
repository, SDK download accepts only the latest Release from
`https://github.com/xiaopaoya/JellyFrame` and requires a GitHub SHA-256 digest
or a matching `.sha256` asset; missing verification stops installation. The
extension first uses a project `.jellyframe/project.json`, a
configured SDK, `JELLYFRAME_SDK_ROOT`, or an SDK found above the
current workspace. `jellyframe.sdkRoot` is the preferred explicit setting;
`jellyframe.repoRoot` remains a legacy alias. `jellyframe.buildDir` is optional.
When an App command needs an SDK but none is configured, it offers **Configure author
environment** directly instead of running an incomplete command. SDK installation never
overwrites an existing directory: transient Windows access or file-lock failures are retried,
then the extension offers retry, another location, or use of an already-valid SDK.
The official App Author SDK intentionally contains prebuilt `desktop-release` and
`desktop-scripting-release` profiles rather than `CMakeCache.txt`; the extension verifies
those profiles against `sdk-manifest.json` and uses them directly.
The extension prefers `build/desktop-release/Release`, then
`build/desktop-debug/Debug` inside the selected SDK.
For an app whose manifest declares `runtime.script`, the extension uses only
`build/desktop-scripting-release/Release` or `build/desktop-scripting-debug/Debug`
unless `jellyframe.buildDir` is explicitly set. A selected build must have
`JELLYFRAME_BUILD_SCRIPTING=ON`; builds that retain the pre-1.0
`JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME` cache option are rejected with a
reconfigure instruction instead of being run accidentally.
When a compatible desktop build is missing, the error action and the Environment
section both offer **Create compatible desktop build**. After explicit author
selection, it configures and builds the managed Release profile locally; a
checked-out JerryScript source is built first only when its libraries are absent.
The command never downloads third-party source or deletes a custom build path.
For an independent workspace, reports, captures and temporary package output
go to `.jellyframe/build` in the App project instead of into the SDK.

Use `JellyFrame: Show Last Report` to reopen the latest report panel.

`JellyFrame: Validate App Package` is the fast, package-only gate. It checks the
manifest, entry point, local resources, references and declared budgets without
starting Render Core or asking for a viewport, measuring layout, frame time or
device performance. Its report is intentionally limited to package structure.
`JellyFrame: Check App Rendering` runs the package gate first, asks for a target
profile from repository presets and targets declared by the current App manifest,
then adds Render Core preflight, responsive layout and font checks. It
also offers optional `.jfcapture` programmed playback, merging the static
pipeline diagnostics with a multi-page interaction path. Use Preview or desktop
debugging for the actual image and interactive behavior.

The `JellyFrame` Activity Bar uses one level of top-level sections. Each App action, build status and device status appears directly below its section, avoiding misleading multi-level indentation in VS Code's native tree control.
Commands have icons and functional tooltips; build, device and report results remain read-only status entries.
It is always contributed, including when no workspace file is open. After
installing an updated VSIX, run `Developer: Reload Window` once if the old
extension instance is still loaded.
The Explorer context menu is available for `jellyframe.app.json`; the editor
context menu is available while editing an app HTML, CSS or manifest file.
These entries use the same commands as the Command Palette, so either entry
point produces the same report and output-channel behavior.

`New App From Template` uses directory pickers for the destination and offers a
suggested `org.example.*` identifier from the App folder name. Choose `Specify
App ID` only when an organization namespace is needed; custom IDs must start
with a letter or digit and may contain only letters, digits, dots, hyphens and
underscores. The target picker uses only recognized repository presets while
creating a new App, so generated manifests are immediately packageable.

`JellyFrame: Debug App In VS Code` opens an editor tab backed by an isolated,
hidden desktop-shell session. It delivers complete viewport snapshots with
strictly increasing sequence numbers and forwards pointer, drag, wheel and
common-key input only to that session. The viewport bar offers App default and
common device-size presets plus a custom `64..2048` width and height; applying a
size restarts the shell so CSS media queries and layout use the requested size.
The frame's reported size remains the authoritative result. Stop keeps the tab
open and changes its controls to Resume and Restart; closing the tab requests a
clean shell exit. The extension terminates the debug process tree after a short
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

`JellyFrame: Discover Device` uses only an explicitly configured Device OS
provider executable. The extension does not bundle the board-specific provider.
For WS147, install the versioned
`jellyframe-ws147-developer-0.6.0-a2-provider-0.1.1-dev.zip` delivery package.
It does not infer serial or USB endpoints. Configure the absolute path to the
separately installed provider in JellyFrame settings; missing or invalid paths
are reported directly. Run Discover Device first, then use Device Info to
validate the selected endpoint against the configured Developer Image manifest.
Deployment selects the one App target whose viewport matches the attested device
display. Target names may differ from the device profile, but every same-size
target must still be unique. An App without that unambiguous declaration is not
packaged or installed for the device.
List Installed Apps shows the same endpoint's registry generation, version,
state and rollback availability. These three commands are read-only: they do
not install, launch, remove or flash a device.

The original `0.1.0-dev` WS147 provider remains read-only and therefore leaves
lifecycle actions hidden. The delivered `0.1.1-dev` provider declares its
verified lifecycle operations through `capabilities.supportedOperations`, so
the Activity Bar reveals only the matching deploy, launch, stop, rollback,
remove, App-log and recovery actions after discovery. This is an explicit
safety gate, not a promise that every declared action is already accepted on
every device. Deployment and removal always require confirmation, and the
extension records the typed terminal result in the Device status section.
