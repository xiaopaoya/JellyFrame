<p><img src="media/jellyframe.png" alt="JellyFrame logo" width="80" height="80"></p>

# JellyFrame Tools for VS Code

> Last updated: 2026-09-20; Applies to: 0.6.0-dev; extension version: 0.4.67; compatibility baseline: 0.5.0

JellyFrame Tools is a VS Code extension for app authors. It brings package
checks, previews, desktop debugging and packaging into the editor, with a
dedicated JellyFrame Activity Bar view, focused context menus and the Command
Palette as entry points.

## Features

- Preview and external-debug startup show progress notifications. Embedded debug
  shows a loading indicator and elapsed time until a frame is decoded. After ten
  seconds the message explicitly says startup is slow and still waiting; this is
  not a failure verdict or an automatic process timeout. The existing Stop control
  remains available for embedded debugging. A frame arriving before the webview
  is ready is retained and delivered after readiness.

- JSON schema association for `jellyframe.app.json`.
- Command palette actions for package-structure validation, render preflight, preview, embedded VS Code debugging,
  external-window debugging, frame-script playback, capture opening and package generation.
- App creation from the built-in blank, weather, clock, timer and calculator templates.
- A constrained visual App editor with drag-and-drop layout, property editing,
  readable HTML/CSS generation and direct handoff to desktop-shell debugging.
- CLI output in a dedicated `JellyFrame` output channel.
- A `JellyFrame Report` webview that puts CLI `developerAdvice[]` first, then
  summarizes resources, references, warnings and pipeline diagnostics.
- A `Render Trace` webview for frame-by-frame inspection of desktop-shell JSONL
  traces, including total time, stage shares, dirty-region metrics, bounded
  dirty-rectangle overlays, ranked paint-command attribution and the matching
  captured frame image when available. Command rows expose owner, raster time,
  candidate pixels and samples, while truncation and incomplete timing remain
  explicit. The frame view also provides a clickable stage-composition bar and
  span timeline with absolute time, frame share, start offset and producer
  runtime source when spans are available, plus a bounded paint-command span
  timeline. Command spans are associated with the stage having the greatest
  time overlap and are aggregated across frames by type, owner and stage with
  count, total time, p95 and candidate pixels; older traces fall back to accumulated stage cost. It does not
  present desktop timing as device FPS. Missing images are reported explicitly
  instead of being inferred as blank frames. When final raster rectangles are
  available, it also lists observed command/dirty-rectangle spatial overlap;
  the evidence is aggregated across frames as well; this is evidence of repaint
  coverage, not a DOM mutation cause. The frame view also compares adjacent
  frames for total, stage, dirty-region and command deltas; command deltas are
  disabled when adjacent frames use different measurement sources.
  The aggregate area also provides p95-based anomaly filtering and a bounded
  timing trend; clicking a trend entry jumps to its frame.
  Selecting an anomalous frame also shows the thresholds and observed values
  that triggered it, the hottest stage and command, the highest-cost owner in
  dirty repaint evidence, and the largest command increase from the previous
  frame. These are correlation and spatial-overlap signals, not a claimed DOM
  mutation cause; missing, truncated, or partial data is listed as a limitation.
- Embedded debugging has an explicit **Trace / Stop trace** control. Sampling is
  off by default; while enabled, the native shell retains at most the latest
  600 rendered frames or 4 MiB in memory and atomically publishes the JSONL on
  stop. The completed trace opens in the existing viewer. Root framebuffer
  scrolling is identified as `present-only / scroll-blit` with its measured
  present span; internal scroll containers retain measured layer, paint,
  present, dirty-region and raster-command evidence.
- Inline diagnostics for app-author advice, package warnings and pipeline
  diagnostics.
- **Generate Performance Report** discovers the latest App report, Render Trace,
  device telemetry and microbench output in the project build directory. One
  multi-select creates a timestamped, self-contained session under
  `.jellyframe/build/performance/`; source inputs remain distinct, are copied
  with SHA-256 identities, and the session manifest does not retain an external
  source file's absolute path. Input snapshots are bounded to 64 MiB each and
  128 MiB per session.
  **Open Performance History** reopens one of the latest 20 complete or failed
  sessions together with its Runtime, Render Core and source-commit identity.
  When the source, Runtime, Core, or SDK version changes, sessions with the same
  source kind, workload, board, viewport, and compatible Core ABI are compared
  against the latest comparable session. Stage p95 changes are marked improved,
  stable, or regressed. Missing or mismatched identity produces no verdict; this
  is a local project trend, not a cross-device or cross-library benchmark.
  **Open Device Performance Trends** groups the latest 20 sessions by device
  case, profile, board, viewport, and Core ABI. It plots Frame, Paint, Present,
  and DMA-wait p95 with a value table and writes bounded JSON/HTML artifacts to
  `.jellyframe/build/performance/trends/`. Missing metrics remain gaps.
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

Official Windows x64 SDKs built with the embedded-runtime packager include Python
and pyserial. SDK extraction uses Windows PowerShell/.NET, not Python. Leave
`jellyframe.pythonPath` empty to use SDK Python for checks, packaging, fonts,
reports, debugging and provider commands. No pip, administrator access or system
PATH changes are needed. Older SDKs (including `app-sdk-v0.6.0-dev.2`) still need
system Python; update the SDK as well as the extension. An explicit Python path
overrides automatic selection. ESP-IDF/framework builds retain their own toolchains.

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
environment** directly instead of running an incomplete command. Author environment status
also shows the SDK Render Core lock. If an App manifest requests a different Runtime/Core
line, validation, check, package, preview, debugging, frame-script playback and device deployment
stop before launching a command and offer
SDK update or environment-selection actions. SDK installation never
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
Use `JellyFrame: Open Render Performance Trace` to select a
`jellyframe.render.trace.v0` JSONL file and inspect stage composition, span
timeline, paint commands, dirty regions and frame captures with the frame
scrubber. Click a stage or command segment to see its absolute time, frame
share, start offset and producer source.
The viewer bounds input size, preserves invalid or non-monotonic frame issues,
and never silently sorts or fabricates records.

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

When manifest `fonts[]` declares a `.jffont` that is absent from the source App,
Check App Rendering, Generate Resource Package, Preview App, and Package and
Deploy App offer to generate and import it from a licensed BDF. The extension
selects the target font when several declarations are missing, accepts only a
`.bdf` file, and forwards `--font-source-bdf` / `--font-resource-id` to the
shared CLI path. The separate `Generate and Import Missing Fonts` command emits
an installable `.jfapp` and report without modifying the source manifest or App
directory. Missing `license.name` / `license.source` stops the operation and
offers to open the manifest; the extension does not download or silently bundle
system fonts.

The `JellyFrame` Activity Bar uses one level of top-level sections. Each App action, build status and device status appears directly below its section, avoiding misleading multi-level indentation in VS Code's native tree control.
Check & Preview, Interactive Debugging and Create & Package separate the authoring
actions; Device Connection, Device Apps and Device status separate device setup,
lifecycle commands and results. Empty action sections are hidden.
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

`JellyFrame: Open Visual Editor` is shown only for Apps with a valid
`.jellyframe/visual-editor.json` model. Ordinary existing HTML/CSS does not get
this entry and is not presented as round-trippable. For eligible Apps it opens a
three-pane designer. Its palette contains only JellyFrame-supported containers, text, buttons,
package images, inputs, progress indicators, dividers, spacers, bounded selects,
short lists, switches and small navigation rows. Elements can be added,
reordered, nested, duplicated and edited at the App's declared viewport or
common device-size presets. Save writes ordinary, readable HTML and CSS;
stable element IDs remain available to hand-authored event listeners. List and
option data is edited through bounded add/remove controls rather than an
unbounded JSON field. The canvas also provides a small icon toolbar for
history, fit, zoom, structure and save, with full text actions retained in the
top bar.
The generated select is the single-select subset and requires the target's
documented `forms.advanced` capability for the core-rendered option overlay;
it is not a browser-native multi-select or navigation control.
The palette also includes three transparent recipes: status card, settings
row and bottom navigation. Selecting one expands it into ordinary editable
nodes, so the recipe is only a starting point and never a private runtime
component.
New blank and device-oriented materials use a black or near-black surface by
default, preserving bright text and accent controls for small round displays.
The inspector reports listeners it can statically recognize for a selected
stable ID in package-local scripts and can copy a minimal event skeleton. It
does not modify JavaScript or attempt to infer application behavior.

The blank template includes a visual model for its `Hello world` entry, so the canvas, source and model agree when it is first opened. Older blank starters without a model are recognized only when they contain this exact minimal structure; arbitrary existing HTML is not guessed. The first save still asks before taking over and backing up the entry page.

The designer is intentionally constrained rather than a browser page builder.
It does not attempt to round-trip arbitrary existing markup. On the first save,
VS Code asks before replacing the entry page's `body`, and writes the original
HTML and CSS to `.jellyframe/visual-editor-backups/<timestamp>/`. Existing
`script` elements and JavaScript files are preserved. Generated regions are
marked explicitly, hand-authored CSS outside the generated region remains
untouched, and the editable model lives in
`.jellyframe/visual-editor.json`. Package images must exist inside the current
App before source can be saved.

The canvas is an authoring approximation, not a second renderer. Use **Save &
debug** to pass the generated source to the real JellyFrame desktop shell and
verify layout, rounded clipping, fonts, animation and interaction before
deployment.

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
`JellyFrame: Install Provider` reads the repository's curated
catalog, lists compatible Windows x64 board packages, downloads the
selected GitHub Release asset, verifies its pinned SHA-256, safely extracts it,
and configures both provider and Developer Image manifest paths. It never scans
serial ports or executes an unlisted archive.
The provider picker is always shown, even for a single entry; dismissing it stops
before choosing an installation folder or downloading a package.
For WS147, install the versioned
`jellyframe-ws147-developer-0.6.2-ws147.2-provider-0.1.1-dev.zip` delivery
package. It does not infer serial or USB endpoints. Run `JellyFrame: Configure
Provider` and select the installed `jellyframe-device.cmd` (or another
provider executable). The command writes its absolute path to the global
setting and automatically selects the only `developer-image/*.manifest.json`
next to the standard provider delivery when one is present. You can still
override both paths in JellyFrame settings. Missing or invalid paths are
reported directly. Run Discover Device first, then use Device Info to validate
the selected endpoint against the configured Developer Image manifest.
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
