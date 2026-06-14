# Engine Architecture

Date: 2026-06-14

WearWeb is structured after the broad shape used by Blink, WebKit and Gecko, but
with smaller data structures and explicit feature cuts for wearable targets.

```text
HTML bytes/string
  -> HtmlTokenizer
  -> HtmlTreeBuilder
  -> DOM

CSS bytes/string
  -> CssParser
  -> CssStyleSheet / CssRule
  -> indexed rule set inside StyleResolver

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
  source-over alpha compositing and BMP/PPM output.

## Rule Indexing

Modern engines build rule sets so style resolution does not scan every rule for
every element. WearWeb now keeps rule buckets by the rightmost simple selector:

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
  attribute and `:root`.
- Unsupported modern selectors are skipped before CSSOM insertion when possible.
- Render/layout remain block-like for flex/grid until those algorithms exist.
- Layer tree supports sparse clipping, opacity boundaries, positioned stacking
  hints and conservative compositing boundaries.
- Display list uses rectangles, gradients and text only.
- Text output uses Windows GDI in the desktop pseudo browser, with a tiny ASCII
  fallback for non-Windows builds.

## Next Professionalization Steps

1. Move selector parsing into a dedicated `selector.*` module.
2. Add arena allocation for DOM/render/layout trees.
3. Add style sharing or computed-style cache for repeated class patterns.
4. Add dirty tree flags for incremental style/layout.
5. Add dirty layer invalidation and rectangle flush.
6. Add a deployable embedded framebuffer backend.
