# Changelog

All notable changes to WearWeb Engine are tracked here.

The project uses lightweight semantic versioning. See `docs/versioning.md`.

## Unreleased

### Added

- Added platform-neutral linked stylesheet collection through a callback-based
  `document_style` API. Core code still performs no file or network I/O; example
  tools and the Win32 shell provide local-file loading for validation.
- Added usable default styling for common HTML5 semantic/content elements:
  `a`, `mark`, `blockquote`, `summary`, `details`, `address`, `hgroup`,
  `progress` and `meter`.
- Added simple software painting for `progress` and `meter` value bars.
- Added `wearweb_win32_browser --capture` to render a page through the Win32/GDI
  text path and write a BMP/PPM image for visual inspection.
- Added a lightweight platform-neutral form-control state layer for common
  embedded-app controls: text inputs, textareas, checkboxes, radios, ranges and
  selects.
- Added core input APIs for UTF-8 text input, simple key handling and stateful
  control activation.
- Added DOM mutation primitives for the JerryScript bridge: child insertion and
  removal, attribute changes, `textContent` updates and dirty flags for
  tree/attribute/text/style/layout invalidation.
- Added bilingual JerryScript integration planning documents covering runtime
  lifecycle, binding ownership, milestones, risks and the first interactive demo
  target.
- Added optional `wearweb_script` JerryScript runtime shell, gated behind
  `WEARWEB_BUILD_SCRIPTING=OFF` by default so `wearweb_core` remains independent
  from JerryScript headers and libraries.
- Added initial `wearweb_pseudo_browser --script` support for scripting builds:
  it evaluates one external JavaScript file and reports the result or exception.
- Added `examples/script_cases/runtime_probe.*` as the first scripting
  acceptance page.
- Added M3 minimal DOM bindings for JerryScript: `window`, `document`,
  `getElementById`, `createElement`, `createTextNode`, `appendChild`,
  `removeChild`, `setAttribute`, `getAttribute` and `textContent`.
- Added `examples/script_cases/dom_mutation_probe.*` to validate script-driven
  DOM mutation through the pseudo browser.
- Added M4 JavaScript event bindings for `addEventListener`,
  `removeEventListener`, event objects, default prevention and propagation
  control.
- Added scripting support to the Win32 browser shell so desktop native input can
  dispatch into JavaScript listeners and rerender dirty DOM mutations.
- Added `examples/script_cases/event_probe.*` for interactive event bridge
  validation.
- Added M5 JavaScript form-control properties for app UI: `value`, `checked`,
  `selectedIndex` and `select.value`.
- Added app-style acceptance examples for weather, clock, timer and calculator
  under `examples/app_cases`.
- Added bilingual embedded app subset documentation describing what can be
  built after M6 and which browser assumptions are intentionally absent.
- Added M6 host-pumped timers: `setTimeout`, `clearTimeout`, `setInterval` and
  `clearInterval`.
- Added `wearweb_pseudo_browser --pump-timers ms` for timer-driven script smoke
  tests without an interactive window.
- Added bilingual memory management review documents covering current ownership,
  embedded risks and allocator/container priorities.
- Added a single aggregate `wearweb_core_tests` executable for platform-neutral
  regression coverage, replacing the many standalone test executables in normal
  builds.
- Added `JERRYSCRIPT_ROOT` CMake support for local official JerryScript source
  trees such as `third_party/jerryscript`.
- Added a responsive grid-card layout subset for embedded apps:
  `display:grid`, `repeat(auto-fit, minmax(<length>, 1fr))`, `gap`,
  `grid-auto-rows: minmax(<length>, auto)` and `grid-column`/`grid-row:
  span N`.
- Added `aspect-ratio` sizing for visual/media boxes.
- Added cheap approximate `box-shadow` painting as rounded translucent fills.
- Added developer-facing capability matrix documentation covering supported,
  degraded, lazy and deferred HTML/CSS/DOM/script/rendering features.
- Added physical edge CSS longhands for `margin-*`, `padding-*` and
  `border-*-width`.
- Added M7 classic document script loading: scripting builds collect and execute
  inline `<script>` and local external `<script src>` through shell callbacks.
- Added `document_script` helpers for platform-neutral script collection.
- Added a first host abstraction draft and `src/core/host.h` with resource,
  clock, frame sink and budget structs.
- Added `examples/script_cases/inline_loading_probe.*` for automatic document
  script loading validation.
- Added regression coverage for linked stylesheet merging, semantic fallback
  styles, inline highlight painting, DOM mutation invalidation and form-control
  fallback behavior. Scripting builds also add JerryScript runtime lifecycle and
  exception-path coverage.

### Improved

- Improved inline layout so runs of text, links, highlights and inline controls
  flow horizontally and wrap by available width instead of stacking every inline
  node vertically.
- Preserved parent `text-align` for inline text runs in the simplified layout
  engine.
- Shrunk inline background/border painting to child text bounds so `mark` and
  similar inline elements do not fill an entire line.
- Treated common replaced controls/media nodes as leaf render objects so
  `select` options and unsupported media fallback text do not spill into the
  page layout.
- Improved default form-control sizing and support for `border: none`, keeping
  buttons shrink-wrapped while making unstylized inputs/selects more usable.
- Painted native-lite control affordances in the display list, including range
  tracks/thumbs, checked checkbox/radio marks, select arrows and text
  value/placeholder content.
- Forwarded Win32 character and Backspace input into the core control model and
  rerendered the existing DOM so desktop validation reflects live control
  changes.
- Replaced hash-table event listener storage with compact per-type listener
  groups, reducing ordinary embedded-page listener overhead while preserving the
  public event API.
- Gave form controls an intrinsic content line height in layout so selects and
  empty inputs remain readable without author-specified heights.
- Limited script form accessors to actual form controls, reducing wrapper
  property setup on ordinary DOM nodes.
- Updated the clock and timer app cases to use M6 `setInterval` instead of
  manual-only refresh.
- Improved simplified flex row layout to honor `column-gap`.
- Improved dirty rerender paths: root dirty checks are O(1), dirty clearing
  skips clean branches, unchanged `textContent` avoids invalidation and the
  Win32 shell no longer rebuilds the pipeline after clean input callbacks.
- Updated scripting and roadmap documentation to treat M7 script loading as
  available and shift the next major work toward host presentation and dirty
  rectangles.

### Notes

- `wearweb_pseudo_browser` still uses the tiny built-in bitmap font when no
  platform `TextPainter` is injected, so non-ASCII text appears as fallback
  glyphs in BMP smoke-test output. The Win32 browser shell uses GDI text
  painting for readable UTF-8/Chinese validation.
- Local linked stylesheets are resolved by the example/Win32 helper relative to
  the CSS path passed on the command line. Missing linked files are ignored
  conservatively, matching the engine's graceful-degradation policy.
- `@container` and `object-fit` remain deferred. Container queries need bounded
  style/layout feedback handling; object-fit should wait for real image decode.

## 0.2.0-dev - 2026-06-15

### Added

- Added CPU framebuffer rendering with `FrameBuffer`, `SoftwareRasterizer` and
  `SoftwareCompositor`.
- Added source-over alpha compositing, opacity-layer offscreen compositing and
  BMP/PPM image output helpers.
- Added `wearweb_pseudo_browser` for full-pipeline framebuffer validation.
- Added core `Event`, `MouseEvent`, `WheelEvent` and `EventTarget` support.
- Added DOM-style capture, target and bubble event dispatch with
  `preventDefault`, propagation stopping and one-shot listeners.
- Added layer-aware hit testing over layout/layer geometry, including z-index
  ordering, overflow clipping and text-node normalization to element targets.
- Added platform-neutral `InputController` for pointer move/down/up, click
  synthesis, wheel dispatch and hover/active/focus tracking.
- Added Windows-only `wearweb_win32_browser`, an interactive validation shell
  that renders through the core pipeline, blits the framebuffer with GDI,
  injects native text painting and forwards mouse/wheel input into
  `InputController`.
- Added viewport scrolling to the Win32 browser shell. Wheel events are still
  dispatched through the core input controller before the shell performs the
  desktop default scroll action.
- Added `document_style` helpers that collect embedded `<style>` element text
  and merge it into author CSS for end-to-end tools and the Win32 shell.
- Added lightweight support for common static-page CSS: fractional lengths,
  `rem`/`em`, `max-width`, horizontal `margin: auto`, `line-height` and
  `text-indent`.
- Added regression tests for events, hit testing, input synthesis, embedded
  styles and wrapped-text layout.
- Added bilingual events/hit-testing scope documents and updated architecture,
  optimization and README documentation.

### Optimized

- Changed `EventTarget` listener storage to allocate lazily so ordinary DOM
  nodes do not carry an empty listener table.
- Split native text painting out of the core software renderer through an
  optional `TextPainter` callback. The core keeps a pure C++ bitmap fallback and
  no longer links Win32/GDI.
- Optimized opaque rectangle fill with direct row fills.
- Clipped offscreen compositing before iterating pixels.
- Hardened framebuffer resize pixel-count calculation against integer
  multiplication overflow.
- Increased wrapped-text line-height padding to avoid clipping with native
  desktop text metrics.

### Notes

- `wearweb_core` remains platform-neutral. Windows libraries are linked only by
  Windows-specific example tools.
- The core text fallback is intentionally tiny and portable; the Win32 browser
  uses native GDI text for readable UTF-8/Chinese validation.

## 0.1.0-dev - 2026-06-13

### Added

- Created the initial C++17 CMake project and `wearweb_core` library.
- Added tolerant HTML tokenizer/parser support with start/end tags, attributes,
  doctype, comments, text, raw-text and character references.
- Added resilient DOM construction with synthesized `html/body`, common implied
  end tags, void elements, unmatched-end-tag tolerance and parser resource
  limits.
- Added `wearweb_dom_dump` for tokenizer output and ASCII DOM visualization.
- Added a tolerant CSS parser with comments, balanced block recovery, ordered
  declarations, selector-list splitting, `@layer` flattening and conservative
  recovery for unsupported enhancement blocks.
- Added lightweight CSSOM rule metadata, specificity, source order and cascade
  ordering.
- Added selector matching for tag, class, id, descendant, child, simple
  attribute selectors and `:root`.
- Added default style boxes for common controls and UI nodes such as forms,
  inputs, buttons, dialogs and media nodes.
- Added render tree, box-model layout, sparse layer tree and display-list
  generation.
- Added pipeline inspection tools: `wearweb_style_dump`,
  `wearweb_render_tree_dump`, `wearweb_layer_tree_dump` and
  `wearweb_pipeline_dump`.
- Added modern HTML/CSS compatibility samples and bilingual analysis documents.
- Added microbenchmarks, CTest registration and CMake options for examples,
  tests and benchmarks.
- Added bilingual documentation policy, roadmap, versioning, architecture and
  feature-scope documents.

### Optimized

- Streamed tokenizer output into DOM construction without storing a full token
  stream.
- Avoided tokenizer input copies when CR normalization is unnecessary.
- Indexed CSS rules by id/class/tag/universal buckets and precomputed selector
  parts during parsing.
- Used fixed cascade slots instead of per-node cascade hash maps.
- Kept layer creation sparse: ordinary boxes paint into their parent layer until
  clipping, stacking or compositing boundaries require a layer.
