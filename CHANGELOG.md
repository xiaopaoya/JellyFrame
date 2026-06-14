# Changelog

All notable changes to WearWeb Engine are tracked here.

The project uses lightweight semantic versioning. See `docs/versioning.md`.

## 0.1.0-dev - 2026-06-13

### Added

- Created the initial C++17 CMake project.
- Added `wearweb_core` with minimal DOM, HTML parsing, CSS parsing, style
  resolving, block layout and display-list generation.
- Added `wearweb_demo`, a console demo that converts HTML/CSS input into
  platform-neutral drawing commands.
- Added the initial roadmap.
- Added bilingual documentation policy and Chinese documentation files.
- Added the initial bilingual HTML tokenizer scope document based on the
  WHATWG HTML Living Standard.
- Added a standalone tolerant HTML tokenizer with start/end tag, attribute,
  doctype, comment, text, raw-text and character-reference handling.
- Refactored `HtmlParser` to consume tokenizer output before building the DOM.
- Added tokenizer regression tests.
- Added a bilingual HTML tree-builder scope document.
- Added resilient DOM construction with synthesized `html/body`, common implied
  end tags, void elements, unmatched-end-tag tolerance and parser resource
  limits.
- Added `wearweb_dom_dump`, a DOM visualization example that prints tokenizer
  output and an ASCII DOM tree.
- Split DOM construction into `HtmlTreeBuilder` and added `HtmlTokenSink` so
  parser construction can stream tokenizer output without storing all tokens.
- Optimized tokenizer input handling to avoid copying when CR normalization is
  unnecessary, and optimized raw-text end-tag matching to avoid temporary
  strings.
- Added bilingual parser architecture notes based on Blink, WebKit and
  html5ever source structure.
- Added a tolerant CSS parser with comment handling, balanced block recovery,
  ordered declarations, selector-list splitting, `@layer` flattening and
  conservative handling for unsupported modern enhancement blocks.
- Changed CSS declarations from an unordered map to an ordered list so fallback
  declarations are preserved.
- Updated style resolution so unsupported property values do not overwrite
  earlier supported fallback values.
- Added CSS parser regression tests and bilingual CSS parser scope documents.
- Added a lightweight CSSOM with `CssStyleSheet`, rule metadata, specificity and
  source order.
- Updated style resolution to use author-style cascade ordering:
  `!important`, specificity and source order.
- Added `wearweb_cssom_dump` and bilingual CSSOM/cascade scope documents.
- Added modern HTML/CSS compatibility samples and a bilingual analysis document
  comparing expected browser behavior with current WearWeb DOM/CSSOM output.
- Added selector matching for descendant selectors, child selectors, simple
  attribute selectors and `:root`.
- Added a small default style layer for common controls and UI elements so forms,
  inputs, buttons, dialogs and media nodes keep usable boxes.
- Extended computed style with display variants, border, radius, min size,
  shadow and overflow fields.
- Added `wearweb_style_dump` for inspecting computed styles of functional UI
  nodes.
- Added a first render tree layer with `RenderTreeBuilder`, `RenderObject` and
  view/block/inline/text render object types.
- Updated layout to consume the render tree instead of walking DOM directly.
- Added `wearweb_render_tree_dump`, render tree tests and bilingual render tree
  scope documents.
- Added box-model-aware layout and rectangle-based border painting.
- Added `wearweb_pipeline_dump` for end-to-end DOM/render/layout/display-list
  inspection.
- Added `wearweb_microbench` and bilingual embedded optimization notes with a
  Release benchmark baseline.
- Corrected default `dialog` behavior so closed dialogs are not rendered unless
  CSS explicitly makes them visible.
- Added browser-like CSS rule indexing by id/class/tag/universal buckets.
- Precomputed selector parts and rule index keys during CSS parsing.
- Reduced Release render-tree microbenchmark time from roughly 2860 us to 810 us
  for the 80-card benchmark case.
- Added bilingual engine architecture documents.
- Added a sparse `LayerTreeBuilder` with root, clip, stacking and composited
  layer nodes.
- Added layer reasons for overflow clipping, opacity, transform, positioned
  content, explicit `z-index`, shadows and rounded clips.
- Added `wearweb_layer_tree_dump` and `wearweb_layer_tree_tests`.
- Routed the end-to-end pipeline through `LayoutBox -> LayerNode -> DisplayList`
  so layout no longer owns painting.
- Added CMake options for library-only embedded builds:
  `WEARWEB_BUILD_EXAMPLES`, `WEARWEB_BUILD_TESTS` and
  `WEARWEB_BUILD_BENCHMARKS`.
- Registered regression tests with CTest.
- Extended computed style parsing for `opacity`, `transform`, `position` and
  `z-index`.
- Added bilingual layer tree scope documents.
- Added CPU `FrameBuffer`, `SoftwareRasterizer` and `SoftwareCompositor`.
- Added source-over alpha compositing and offscreen compositing for opacity
  layers.
- Added BMP/PPM output helpers and `wearweb_pseudo_browser` for full-pipeline
  desktop validation.
- Added Windows GDI-backed UTF-8 text rasterization with a tiny ASCII fallback
  for other platforms.
- Added `wearweb_software_renderer_tests`.
- Added parsing/layout support for common page-building CSS used by the
  validation pages: four-value box edges, `rgb()/rgba()`, conservative
  `linear-gradient()` fallback, `box-sizing:border-box`, `text-align`, minimal
  flex centering/row layout and inherited text color/size/alignment.
- Added conservative text overhang padding and basic multiline text layout to
  avoid clipping line ends.
- Added bilingual software renderer scope documents.
