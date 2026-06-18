# Roadmap

For the complete current status, scope boundary and next milestone definitions,
see `project_status.md`. This document keeps only the high-level route.

## Milestone 1: Static document core

- HTML subset: document nodes, text, common block and inline tags
- CSS subset: simple selectors and a small property set
- Layout: block flow and text measurement abstraction
- Rendering: sparse layer tree, clipping, and display list with rectangles, text
  and image placeholders

Status: mostly complete, with a broader app-oriented subset than originally
planned.

## Milestone 2: Embedded rendering backend

- Software framebuffer backend: available for validation
- Dirty rectangle repaint: first automatic `dirty_region` subset is available
  for non-structural DOM changes; display invalidation diagnostics now count
  dirty rectangle coverage across layers/display commands, while retained
  display-list reuse is deferred
- Embedded framebuffer adapter: available for caller-owned RGBA8888/BGRA8888,
  RGB565/BGR565, RGB332, Gray8 and monochrome buffers
- Platform text measurement/painting backend: API exists, Win32/GDI validation
  backend exists, and the first static embedded bitmap backend plus BDF pack
  generator are available; LVGL/vendor engines should be wrapped only as thin
  text/panel/input hooks, not as the main JellyFrame renderer
- Pointer/touch input routing: pointer/wheel core exists; button/crown focus
  navigation has a first core API; board adapters are still needed
- Platform-neutral board bring-up shape: first static-resource/RGB565 demo is
  available through `jellyframe_embedded_host_demo`

## Milestone 3: App runtime

- JerryScript integration: optional scripting build available
- DOM mutation APIs: available
- Timer/event loop: host-pumped timers available
- Classic document script loading: available in scripting example shells
- Resource abstraction: callback-based local stylesheet/classic script loading is
  available to shells; network/fetch remains deliberately absent
- Device capability APIs: first `HostDeviceCapabilities` contract available;
  deeper automatic adaptation is deferred
- Centralized host budgets: wired into parser, render, layout, layer,
  display-list, dirty-region and scripting limits

## Milestone 4: Wearable UI features

- Small-screen viewport model
- Focus/navigation model for crown/buttons/touch: first core focus traversal and
  activation API is available
- Power-aware animation scheduling

## Milestone 4 Packaging Track

M11 is the packaging part of the wearable app runtime. The target developer
experience is CLI first, then VS Code integration. The standalone IDE question
stays deferred until the CLI and preview shell are stable.

Completed or first-cut:

- `jellyframe.app.json` V0 manifest shape for identity, entry, runtime,
  viewport, budgets, targets, permissions and capabilities
- local-only package resource policy; runtime network capability is declared
  separately for future host APIs
- top-level `tools/jellyframe_cli.py` developer CLI for validate/package/preview
  and package-scoped capability checks
- `tools/package_app.py` packer that validates packages and emits a generated
  C++ resource table plus JSON report
- `schemas/jellyframe.app.schema.json` for editor and CI manifest validation
- built-in target presets for `round-300`, `rect-320x240` and
  `esp32s3-round-300`
- pseudo-browser `--app` source-package preview path
- first package sample under `examples/apps/watch_weather`
- ESP32-S3 bring-up resources now use the top-level packer

Next packaging steps:

1. Extend the developer CLI with font-pack generation.
2. Add package templates for weather, clock, timer and calculator apps.
3. Add more target presets as hardware requirements become concrete.
4. Build a VS Code extension on top of the CLI: schema association, one-click
   preview/package, report panel and inline capability warnings.
5. Consider a standalone visual tool only after the CLI/plugin workflow proves
   insufficient for non-programmer app authors.

## Compatibility Short Track: Modern CSS authoring subset

- Highest-return compatibility items from the modern syntax report are now
  implemented: `var()` fallback resolution, bounded conditional `@media`,
  dynamic pseudo-classes, `:is()` / `:where()`, sibling selectors and
  simplified flex grow/shrink/basis sizing
- Conservative `@supports (property: value)` query flattening is available
- Bounded `relative`/`absolute`/`fixed` positioned layout is available for
  common app overlays
- Remaining work on this track is mostly incremental tests around already
  supported declarations
- Still deferred: full `:has()`, full `@container`, full animation/filter/image
  pipelines and browser-complete layout algorithms

## Compatibility Short Track: HTML parser and DOM expectations

The HTML Living Standard degradation audit highlights several gaps that matter
more to app authors than old browser compatibility modes. Accepted items should
improve developer intuition and document correctness without adding quirks mode,
`document.write()`, speculative parsing or the full adoption-agency algorithm.

Accepted first batch:

- Correct non-void self-closing slash handling so `<div/>` is treated as an HTML
  start tag, while real void elements remain leaf nodes.
- Stop folding ordinary text during tree construction; preserve text nodes and
  let layout/rendering handle whitespace according to the supported CSS subset.
- Treat `textarea` and `title` as RCDATA-like content so character references
  are decoded, while `script` and `style` remain raw-text.
- Expand named character references for common HTML entities and tighten
  semicolon, attribute-context and numeric-reference recovery rules.
- Add parser degradation diagnostics for node/depth/attribute budget limits,
  keeping the current graceful truncation behavior but making it observable.
- Introduce a minimal document metadata model for doctype and optional comments,
  with a path toward `Document`/`Comment`/`DocumentType` nodes when it does not
  destabilize existing DOM ownership.

Accepted second batch:

- Define a minimal insertion-mode subset for before-html, before-head, in-head,
  after-head and in-body routing.
- Add fragment parsing for `innerHTML`, template fragments and component
  snippets.
- Add `template.content`-style inert fragment ownership.
- Broaden common implied-end-tag behavior, including `p`, `select`/`option` and
  `optgroup` cases.
- Add a bounded table tree-construction subset for
  `table`/`tbody`/`thead`/`tfoot`/`tr`/`td`/`th`.
- Improve classic-script raw-text boundaries and make unsupported module
  scripts explicit diagnostics instead of silent surprises.

Accepted but cautious:

- Minimal inline SVG/foreign-content boundary detection is useful, but full
  foreign-content parsing is deferred.
- Complete quirks/limited-quirks, parser-reentrant `document.write()`,
  speculative parsing, full adoption-agency behavior and full table foster
  parenting remain out of scope unless the project changes direction toward a
  general-purpose browser.

## Recommended Next Order

1. Land the first HTML parser/DOM compatibility batch where it is cheap and
   directly reduces app-author surprises.
2. Tighten the core run-loop and dirty-update contract, with long-running
   timer/input smoke coverage.
3. M9 bounded invalidation/diagnostics are complete; add retained subtree reuse
   later only where real diagnostics justify the ownership complexity.
4. M10 text backend adapter and font workflow work is complete for the current
   mainline scope.
5. Finish local resource bundle tooling and app packaging.
6. Continue memory and allocator work, including a `DomOwner` prototype and
   detached-node instrumentation.
7. Move tiled/scanline presentation forward only when target hardware needs it.
