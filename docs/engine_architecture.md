# Engine Architecture


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
  attribute and `:root`.
- Unsupported modern selectors are skipped before CSSOM insertion when possible.
- Render objects keep a compact block/inline/text shape; layout adds small
  dedicated paths for common flex rows and responsive grid-card patterns.
- Render, layout and layer tree builders expose heap and `MonotonicArena`
  allocation paths; embedded benchmarks use the arena path to reduce small heap
  churn.
- Layer tree supports sparse clipping, opacity boundaries, positioned stacking
  hints and conservative compositing boundaries.
- Display list uses rectangles, gradients and text only.
- Display invalidation diagnostics can map dirty rectangles to affected layers
  and display commands, but retained display-list reuse is still deferred.
- Text layout accepts `TextMeasureProvider`; text output accepts `TextPainter`.
  Core fallback is tiny, while the Win32 browser uses GDI for both measurement
  and painting.
- Event dispatch is platform-neutral and currently uses C++ callbacks, not
  JavaScript functions.

## Next Professionalization Steps

1. Move selector parsing into a dedicated `selector.*` module.
2. Tighten the run-loop and dirty-update contract.
3. Use dirty-region and display-invalidation diagnostics to decide which
   retained render/layout/layer subtrees are worth adding.
4. Add style sharing or computed-style cache for repeated class patterns.
5. Evaluate DOM node allocation policy through a `DomOwner` prototype and
   detached-node instrumentation.
6. Organize local resource bundles, app packaging and release artifacts.
7. Continue allocator work and refine tile/scanline presentation only when real
   hardware pressure proves the extra ownership complexity is worthwhile.
