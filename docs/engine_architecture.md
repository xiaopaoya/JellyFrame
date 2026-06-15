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

Platform-neutral input
  -> HitTester
  -> InputController
  -> Event / MouseEvent / WheelEvent
  -> EventTarget dispatch on DOM nodes

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
  source-over alpha compositing, optional platform text painting and BMP/PPM
  output.
- `HitTester`: maps viewport coordinates to DOM event targets through layout and
  layer geometry.
- `InputController`: turns platform-neutral pointer/wheel input into mouse-like
  events, hover/active/focus state and click synthesis.
- `EventTarget`: stores C++ listeners and dispatches DOM-style capture, target
  and bubble phases.

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
- Render objects keep a compact block/inline/text shape; layout adds small
  dedicated paths for common flex rows and responsive grid-card patterns.
- Layer tree supports sparse clipping, opacity boundaries, positioned stacking
  hints and conservative compositing boundaries.
- Display list uses rectangles, gradients and text only.
- Text layout accepts `TextMeasureProvider`; text output accepts `TextPainter`.
  Core fallback is tiny, while the Win32 browser uses GDI for both measurement
  and painting.
- Event dispatch is platform-neutral and currently uses C++ callbacks, not
  JavaScript functions.

## Next Professionalization Steps

1. Move selector parsing into a dedicated `selector.*` module.
2. Add arena allocation for DOM/render/layout trees.
3. Add style sharing or computed-style cache for repeated class patterns.
4. Reuse render/layout/layer subtrees from existing dirty flags instead of
   rebuilding the full pipeline after every mutation.
5. Add dirty layer invalidation and rectangle flush.
6. Add embedded bitmap/LVGL/vendor text backend examples beyond Win32/GDI.
