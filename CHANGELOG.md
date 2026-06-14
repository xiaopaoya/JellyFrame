# Changelog

All notable changes to WearWeb Engine are tracked here.

The project uses lightweight semantic versioning. See `docs/versioning.md`.

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
