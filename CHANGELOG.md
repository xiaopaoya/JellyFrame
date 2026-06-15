# Changelog

All notable changes to JellyFrame Engine are tracked here.

The project uses lightweight semantic versioning. See `docs/versioning.md`.

## Unreleased

### Added

- Renamed the project to `JellyFrame`; `WearWeb` is now documented only as the
  early codename.
- Added platform-neutral `TextMeasureProvider` so layout can use host text
  metrics while keeping font APIs outside `jellyframe_core`.
- Added minimal text paint semantics to display commands: horizontal alignment
  and single-line versus wrapped text.
- Added Win32/GDI text measurement injection alongside the existing GDI text
  painter for more faithful UTF-8/Chinese desktop validation.
- Added bilingual text backend documentation covering measurement/painting
  contracts and fallback limits.
- Added font coverage support to `jellyframe_capability_check`: it can emit
  non-ASCII used characters and verify them against a UTF-8 font coverage file.
- Added button/crown-friendly focus navigation on `InputController`:
  `focus_next()`, `focus_previous()` and `activate_focused()`.
- Added bilingual embedded HAL API documentation for board ports, including
  ESP32-S3 mapping notes.
- Added bilingual porting work guides for ESP32-S3/RTOS/LVGL ports, covering
  phased tasks, implementation requirements, acceptance checks and boundaries
  that require core work first.
- Added a `ports/virtual_board` desktop virtual-board benchmark and normalized
  the ESP32-S3/QEMU experiment into a `ports/esp32s3-idf` bring-up project.
- Added a bounded ESP32-S3 static resource bundle hook for local HTML/CSS and
  classic-script assets, including a generated C++ table and P2 smoke resources.
- Added bilingual ESP32-S3 QEMU PSRAM gradient benchmark documentation with
  4M/8M/16M/32M timing data and memory-capacity recommendations.
- Added a platform-neutral static bitmap font backend with measurement and
  painting callbacks for generated embedded font packs.
- Added `jellyframe_font_pack_gen`, a desktop BDF subset generator that emits C++
  `BitmapFont` headers for embedded builds.
- Added `jellyframe_embedded_host_demo`, a platform-neutral static-resource demo
  that wires HTML/CSS parsing, bitmap text, focus activation and RGB565
  framebuffer presentation without Win32, files or hardware I/O.
- Added first host device capability structs for board ports, covering display,
  input, memory, budgets and optional host services.
- Added `core/budget.h` helpers that map `HostBudgets` into HTML/CSS parser,
  render/layout/layer/display-list, dirty-rectangle and JerryScript
  timer/listener limits.
- Replaced per-node DOM attribute `std::unordered_map` storage with a compact
  sequential `AttributeList`, reducing heap overhead for small embedded UI nodes
  while keeping the existing map-like call shape.
- Added a core `MonotonicArena` memory utility with block based linear
  allocation, reverse-order destruction and full arena reset as the base for
  future DOM/render/layout/layer lifetime allocation.
- Added an arena-backed render tree build path and exercised it from microbench,
  virtual board and ESP32-S3 benchmarks to validate document-lifetime
  allocation.
- Added an arena-backed layout tree build path, switched embedded-oriented
  benchmarks to it and covered it with core regression tests.
- Added an arena-backed layer tree build path, switched embedded-oriented
  benchmarks to it and covered it with layer-tree regression tests.
- Added a bounded `StyleResolver` candidate-rule cache for repeated id/class/tag
  patterns while preserving per-node selector matching and cascade semantics.
- Added iterative DOM subtree teardown and whole-subtree `textContent`
  replacement to reduce stack pressure on deeply nested generated documents.
- Added bilingual DOM arena feasibility notes documenting why direct DOM arena
  allocation is deferred for mutable/scripted documents.
- Added iterative `compute_dom_statistics()` instrumentation and surfaced DOM
  depth/attribute counts from pipeline diagnostics.
- Added paint-only DOM dirty state for form-control value/checked/selection
  changes, enabling the Win32 shell to reuse render/layout and repaint bounded
  dirty rectangles for common control interaction.
- Added platform-neutral linked stylesheet collection through a callback-based
  `document_style` API. Core code still performs no file or network I/O; example
  tools and the Win32 shell provide local-file loading for validation.
- Added usable default styling for common HTML5 semantic/content elements:
  `a`, `mark`, `blockquote`, `summary`, `details`, `address`, `hgroup`,
  `progress` and `meter`.
- Added simple software painting for `progress` and `meter` value bars.
- Added `jellyframe_win32_browser --capture` to render a page through the Win32/GDI
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
- Added optional `jellyframe_script` JerryScript runtime shell, gated behind
  `JELLYFRAME_BUILD_SCRIPTING=OFF` by default so `jellyframe_core` remains independent
  from JerryScript headers and libraries.
- Added initial `jellyframe_pseudo_browser --script` support for scripting builds:
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
- Added `jellyframe_pseudo_browser --pump-timers ms` for timer-driven script smoke
  tests without an interactive window.
- Added bilingual memory management review documents covering current ownership,
  embedded risks and allocator/container priorities.
- Added a single aggregate `jellyframe_core_tests` executable for platform-neutral
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
- Added `font-weight` parsing/inheritance and display-list propagation, with
  approximate bold rendering in the core fallback and native weight selection
  in the Win32/GDI text path.
- Added lightweight list marker support: `list-style`/`list-style-type`,
  native-lite `ul`/`ol` markers and a tiny `::before content: counter(...)`
  path for common custom ordered lists.
- Added simple fixed grid column templates such as
  `grid-template-columns: 120px 1fr` for definition lists and settings-style
  structured data.
- Added dirty-rectangle framebuffer repaint through
  `SoftwareCompositor::render_into` and `HostFrameSink` presentation helpers.
- Added `dirty_region`, the first automatic dirty-rectangle source for direct
  text, attribute and form-control mutations. Tree mutations remain
  conservatively full-viewport.
- Added `embedded_framebuffer`, a platform-neutral `HostFrameSink` adapter that
  converts dirty rectangles into caller-owned RGBA8888/BGRA8888, RGB565/BGR565,
  RGB332, Gray8 or 1-bit monochrome display buffers.
- Added ESP32-S3 P3 display bring-up support: an 8 MB flash partition layout,
  RGB565 packed dirty-rectangle flush callbacks, scratch-buffer row packing and
  a QEMU display smoke path that exercises full-frame and partial dirty
  presentation.
- Added embedded-app JavaScript helpers: `children`, `parentElement`,
  simple-selector `matches`/`closest`, existing-attribute `dataset` snapshots,
  a small writable `element.style` object and boolean `hidden`/`disabled`
  reflection.
- Added mouse-like `pointerdown`/`pointerup` and `touchstart`/`touchend` event
  dispatch for wearable press feedback.
- Added `jellyframe_capability_check`, a desktop HTML/CSS/JS scanner for supported
  subsets, degraded features and unsupported browser APIs.
- Added conservative modern length function support for `min()`, `max()`,
  `clamp()` and simple `calc(A +/- B)` when arguments resolve to supported
  lengths.
- Added simplified `flex-wrap` row wrapping for common card/box layouts.
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
- Improved core text fallback to measure and paint UTF-8 by codepoint instead
  of treating every non-ASCII byte as a separate glyph.
- Improved text wrapping heuristics so single unbreakable symbols are not
  treated as multi-line text when their measured width slightly exceeds a small
  control.
- Improved grid layout so auto-width grid items are laid out against their
  assigned track width, preserving centered button text after stretch.
- Preserved explicit grid item heights and margins during grid placement.
- Updated pseudo/Win32 browser shells to use body/html background as the canvas
  clear color instead of always clearing to white.
- Updated the watch calculator example to use the supported grid/gap subset
  instead of relying on inline-block whitespace.
- Updated scripting and roadmap documentation to treat M7 script loading as
  available and shift the next major work toward host presentation and dirty
  rectangles.
- Fixed child-combinator selector parsing with whitespace around `>`, so rules
  such as `.list > li` no longer accidentally match deeper descendants.
- Fixed form-control state changes so text input, select, range and other
  control interactions mark DOM dirty and trigger Win32 shell rerendering.
- Improved keyboard behavior for interactive controls: datalist-backed text
  inputs can accept the first matching candidate with Tab/Enter, and selects
  can move through options across `optgroup` boundaries with Up/Down.
- Added Win32-shell hash anchor scrolling for `<a href="#id">` validation
  pages.
- Updated `jellyframe_pseudo_browser` to present through `HostFrameSink` while
  preserving BMP/PPM validation output.
- Updated the Win32 browser shell to reuse its framebuffer and repaint only
  computed dirty rectangles after non-structural DOM changes.
- Added embedded framebuffer backend documentation and updated host/roadmap
  docs to make platform text and wearable navigation the next priorities.
- Implemented `hidden` rendering semantics and disabled form-control behavior
  for pointer/text/control activation paths.

### Notes

- `jellyframe_pseudo_browser` still uses the tiny built-in bitmap font when no
  platform `TextPainter` is injected, so non-ASCII text appears as fallback
  glyphs in BMP smoke-test output. The Win32 browser shell uses GDI text
  measurement and painting for readable UTF-8/Chinese validation.
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
- Added `jellyframe_pseudo_browser` for full-pipeline framebuffer validation.
- Added core `Event`, `MouseEvent`, `WheelEvent` and `EventTarget` support.
- Added DOM-style capture, target and bubble event dispatch with
  `preventDefault`, propagation stopping and one-shot listeners.
- Added layer-aware hit testing over layout/layer geometry, including z-index
  ordering, overflow clipping and text-node normalization to element targets.
- Added platform-neutral `InputController` for pointer move/down/up, click
  synthesis, wheel dispatch and hover/active/focus tracking.
- Added Windows-only `jellyframe_win32_browser`, an interactive validation shell
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

- `jellyframe_core` remains platform-neutral. Windows libraries are linked only by
  Windows-specific example tools.
- The core text fallback is intentionally tiny and portable; the Win32 browser
  uses native GDI text for readable UTF-8/Chinese validation.

## 0.1.0-dev - 2026-06-13

### Added

- Created the initial C++17 CMake project and `jellyframe_core` library.
- Added tolerant HTML tokenizer/parser support with start/end tags, attributes,
  doctype, comments, text, raw-text and character references.
- Added resilient DOM construction with synthesized `html/body`, common implied
  end tags, void elements, unmatched-end-tag tolerance and parser resource
  limits.
- Added `jellyframe_dom_dump` for tokenizer output and ASCII DOM visualization.
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
- Added pipeline inspection tools: `jellyframe_style_dump`,
  `jellyframe_render_tree_dump`, `jellyframe_layer_tree_dump` and
  `jellyframe_pipeline_dump`.
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
