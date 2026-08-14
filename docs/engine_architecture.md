# Engine Architecture

> Last updated: 2026-08-14; Applies to: 0.6.0-dev


JellyFrame is structured after the broad shape used by Blink, WebKit and Gecko, but
with smaller data structures and explicit feature cuts for wearable targets.

The source tree is split into three hardware-neutral subprojects:

- `src/render_core` / `jellyframe_render_core`: the HTML/CSS/DOM/rendering
  subset. It has no JerryScript, app-install, filesystem, network or OS
  dependency.
- `src/app_runtime` / `jellyframe_app_runtime`: installable-app lifecycle and
  optional host-service queues. It depends on `render_core` for shared host
  capability and budget types.
- `src/script` / `jellyframe_script`: optional JerryScript bridge. It can be
  left out of embedded builds.

The repository is currently still a source monorepo, but the Render Core target
already has an extraction boundary. Configure with
`JELLYFRAME_BUILD_APP_RUNTIME=OFF`, `JELLYFRAME_BUILD_SCRIPTING=OFF` and the
upper-layer examples disabled to build only Render Core. With
`JELLYFRAME_INSTALL_RENDER_CORE=ON`, CMake exports a versioned
`JellyFrame::jellyframe_render_core` package for the future standalone
`jellyframe-render-core` repository. This staged approach keeps local cross-layer
development cheap without making a Git submodule part of the public workflow.

The same checkout can create a self-contained, reproducible source archive:

```powershell
python project_tools\package_render_core_source.py --output-dir build\dist
tar -xzf build\dist\jellyframe-render-core-0.6.0.tar.gz -C build\unpacked
cmake -S build\unpacked\jellyframe-render-core-0.6.0 -B build\core-from-archive
cmake --build build\core-from-archive --config Release --parallel
ctest --test-dir build\core-from-archive -C Release --output-on-failure
```

The archive contains only Render Core sources, its shared CMake boundary,
tests, standalone README and license. It contains neither Runtime nor
JerryScript, ports, device contracts, examples or app resources. The packer
normalizes member order and archive metadata, writes a SHA-256 sidecar, and CI
extracts, builds, tests, installs and consumes the resulting package through
the Runtime package-provider configuration. This is the current extraction
gate; it is not yet a separately governed Git repository or signed release.

The App Runtime can consume that installed package without compiling the Render
Core sources from this checkout:

```powershell
cmake -S . -B build\framework-external-core `
  -DJELLYFRAME_RENDER_CORE_PROVIDER=package `
  -DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=C:\path\to\render-core-install `
  -DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF
cmake --build build\framework-external-core --config Release `
  --target jellyframe_app_runtime_tests jellyframe_device_runtime_contracts_tests
```

The accepted package version and engine ABI are pinned in
`cmake/jellyframe_dependency_lock.cmake`. Package mode verifies both values and
copies the package capability profile, including its Core package version, into
the Runtime build tree. The default
`in-tree` mode remains the correct choice when changing Core and Runtime
together; package mode is the boundary test for an independently released Core.
Every configuration also writes
`generated/jellyframe_render_core_provenance.json`. It identifies the selected
provider, Core package version, ABI, profile filename, Runtime lock values and
the deterministic SHA-256 source identity without embedding workstation paths.
An installed Core package exports a matching source manifest; package consumers
validate and copy that manifest into their generated directory. Archive both
with a Runtime or port build report. The content hash identifies exactly which
Core source set was consumed, including a source archive or local override; it
does not replace a release signature, a reviewed version lock or publishing
authority.
The Device Runtime, JFDP protocol, launcher and hardware ports are not part of
this package boundary.

For cross-repository development, `JELLYFRAME_RENDER_CORE_SOURCE_DIR` can point
the in-tree provider at a checked-out Render Core source tree. It is a local
development override, not a second public dependency mechanism; package mode
and source override are mutually exclusive. The override must provide the
Render Core `cmake/` boundary and `src/render_core/` tree, and the normal Core
feature profile and source-ownership checks still apply.

## Planned Repository Boundaries

The current source monorepo is a development convenience, not the final
ownership model:

| Future repository | Owns | Release rhythm | Current state |
| --- | --- | --- | --- |
| `jellyframe-render-core` | HTML/CSS/DOM, layout, paint, input and opt-in feature families | Frequent optimization and compatibility releases | Install/export, deterministic source archive and package consumer are verified in this repository |
| `jellyframe` | App Runtime, Japp format, JerryScript binding, desktop shell and author tools | Slower releases with App compatibility discipline | Current Runtime source boundary; accepts a locked Core package or in-tree Core |
| `jellyframe-device-os` | Launcher, registry, Device Runtime, JFDP, board ports and official images | Experimental hardware-driven releases | Not physically extracted; D0 contracts remain in transitional locations |
| JerryScript | Third-party scripting engine | Upstream commit/tag cadence | Optional dependency, locked by the Runtime build/port owner |

`src/app_runtime/device_install_transaction.*` and
`src/app_runtime/device_runtime_protocol.*` are a deliberate D0 exception. They
are hardware-neutral contracts, but their meaning is device installation and
JFDP rather than App Runtime behavior. They must not enter the Render Core
package and must eventually move to `jellyframe-device-os` or a small
`device_runtime_contracts` package. D0 already builds them as the independent
`jellyframe_device_runtime_contracts` target and runs its tests without App
Runtime or Render Core implementation objects. The current source path remains
transitional so the protocol is not silently duplicated by a port while the
full reference-host loop is completed.

The physical split is gated by three conditions: an independently buildable
Core source archive/package, a Runtime consumer with a locked Core version/ABI,
and a Device OS reference host that consumes the same Runtime contracts without
importing Core implementation details. Git submodules are not required for any
of these gates.

```text
HTML bytes/string
  -> HtmlTokenizer
  -> HtmlTreeBuilder
  -> DOM

CSS bytes/string
  -> CssParser
  -> CssStyleSheet / CssRule
  -> indexed rule set inside StyleResolver

Platform-neutral input
  -> HitTester
  -> InputController
  -> Event / MouseEvent / WheelEvent
  -> EventTarget dispatch on DOM nodes

Host async services
  -> decode/network/install workers
  -> bounded completion queue
  -> UI/main task event dispatch or dirty marking

DOM + StyleResolver
  -> RenderTreeBuilder
  -> RenderObject tree
  -> LayoutEngine
  -> LayoutBox tree
  -> LayerTreeBuilder
  -> LayerNode tree
  -> DisplayList
  -> SoftwareRasterizer / SoftwareCompositor
  -> FrameBuffer / platform renderer
  -> HostFrameSink present / panel flush completion
```

## Browser-Like Layers

- `HtmlTokenizer`: tolerant token stream generation.
- `HtmlTreeBuilder`: resilient DOM construction with open-elements stack.
- `CssParser`: CSS Syntax-inspired rule/declaration parser with recovery.
- `CssStyleSheet`: lightweight CSSOM rule list.
- `StyleResolver`: cascade, selector matching and indexed rule collection.
- `RenderTreeBuilder`: filters non-rendered DOM and attaches computed style.
- `LayoutEngine`: produces geometry from render objects.
- `LayerTreeBuilder`: groups paint commands into sparse clip/stacking/composite
  layers and can flatten them for simple backends.
- `DisplayList`: simple rectangle/text command list for framebuffer-oriented
  backends.
- `SoftwareRasterizer` / `SoftwareCompositor`: CPU validation renderer using
  source-over alpha compositing, optional platform text painting and BMP/PPM
  output.
- `HostFrameSink`: display submission boundary for the frame. Embedded hosts
  should allow framebuffer/target-buffer reuse only after the panel flush is
  complete or the pixels have been safely handed to driver-owned memory.
- `HitTester`: maps viewport coordinates to DOM event targets through layout and
  layer geometry.
- `InputController`: turns platform-neutral pointer/wheel input into mouse-like
  events, hover/active/focus state and click synthesis.
- `EventTarget`: stores C++ listeners and dispatches DOM-style capture, target
  and bubble phases.
- `Host async services`: optional `app_runtime` services for image/audio/
  lightweight video, network data requests and installable bundles. They do not
  own DOM or framebuffers; they return to the UI/main task through bounded
  completion events.
- `PipelineStatistics`: optional read-only accounting for DOM, render, layout,
  layer, display-list, framebuffer, resource and arena usage. It is meant for
  validation shells and benchmarks, not for the hot render path.

## Rule Indexing

Modern engines build rule sets so style resolution does not scan every rule for
every element. JellyFrame now keeps rule buckets by the rightmost simple selector:

- id bucket
- class bucket
- tag bucket
- universal bucket

Each `CssRule` stores:

- selector text
- parsed selector parts
- specificity
- source order
- index key
- ordered declarations

During style resolution, the resolver collects only relevant buckets, sorts by
source order, then runs selector matching and cascade comparison.

## Current Tradeoffs

- Rule indexing is intentionally simple and allocation-light.
- Selector support is limited but useful: compound, descendant, child,
  adjacent/general sibling, attribute, `:root`, selected dynamic pseudo-classes,
  and the documented `:is()` / `:where()` subset.
- Unsupported modern selectors are skipped before CSSOM insertion when possible.
- Render objects keep a compact block/inline/text shape; layout adds small
  dedicated paths for common flex rows and responsive grid-card patterns.
- The `css.flex-grid` profile family now owns the independent flex paint-order
  helper. The remaining flex/grid layout algorithms stay in `layout.cpp` because
  they call the shared recursive layout, geometry and budget paths; extracting
  them is deferred until a narrow internal interface can preserve those contracts
  without adding runtime indirection.
- Render, layout and layer tree builders expose heap and `MonotonicArena`
  allocation paths; embedded benchmarks use the arena path to reduce small heap
  churn.
- Layer tree supports sparse clipping, opacity boundaries, positioned stacking
  hints and conservative compositing boundaries.
- Display list uses bounded rectangle, gradient, text and image-surface-handle
  commands. Canvas output enters the same path as a bounded image surface.
- Dirty-region planning can repaint paint-only changes against the existing
  frame, compare previous/current layout for bounded layout-dirty frames, and
  expose display-invalidation diagnostics over affected layers and commands.
  Full retained display-list diffing is still deferred.
- Text layout accepts `TextMeasureProvider`; text output accepts `TextPainter`.
  Core fallback is tiny, while the Win32 browser uses GDI for both measurement
  and painting.
- Event dispatch is platform-neutral. Core users can attach C++ callbacks;
  optional JerryScript builds also bind the documented `addEventListener` and
  `on*` handler subset through the same event path.

## Deferred Engineering Areas

The current public contract is the architecture described above. The following
areas are not required knowledge for app authors or port authors today, and
should not be treated as available behavior until they appear in the capability
matrix:

- Dedicated selector-module internals.
- Finer retained subtree and retained display-list diffing.
- Computed-style sharing for repeated class patterns.
- A broader DOM ownership/arena policy.
- Tile or scanline presentation paths for targets that cannot keep the chosen
  framebuffer representation.
- A further physical split of the flex/grid layout algorithms. It requires an
  interface and allocation audit first; the current paint-order split is the
  accepted low-risk boundary.
