# Layer Tree Scope


There is no WHATWG layer-tree standard. This part is implementation-defined, so
JellyFrame follows the practical structure used by modern engines while cutting the
model down for framebuffer-class hardware.

Reference points:

- Blink/Chromium separates layout, paint chunks, paint property trees and
  compositing. See Chromium's RenderingNG architecture notes:
  https://developer.chrome.com/docs/chromium/renderingng-architecture
- WebKit has long used `RenderLayer` and compositing layers around rendering
  objects:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/rendering
- Gecko/WebRender consumes display lists and stacking contexts before sending
  retained scene data to the renderer:
  https://firefox-source-docs.mozilla.org/gfx/webrender/

## JellyFrame Model

```text
LayoutBox tree
  -> LayerTreeBuilder
  -> LayerNode tree
  -> flattened DisplayList
  -> platform renderer
```

The layer tree is a retained paint organization structure. It is not a GPU API
and it does not require textures. A platform backend can either flatten it into
rect/text commands or later map selected layers to hardware surfaces.

## Implemented Layer Reasons

- Root document layer.
- `overflow: hidden`, `overflow: clip`, `overflow: auto` and `overflow: scroll`
  create a clipping layer.
- `opacity` below 1 creates a composited layer and is applied during flattening.
- `transform` creates a composited layer, but transform math is deferred.
- `position` and explicit `z-index` create stacking layers.
- `box-shadow` and rounded overflow clips create layer boundaries. Current
  painting emits a cheap rounded translucent shadow fill; real blur can later
  replace it without changing tree shape.

## Degradation Policy

- Unsupported transform values do not crash or move boxes incorrectly; they only
  mark a compositing boundary for now.
- Rounded clipping is detected but approximated by rectangular clipping.
- `box-shadow` blur is approximated as rectangle expansion and translucent fill.
- `z-index` ordering is layer-local and useful for positioned children, but true
  CSS stacking-context rules are not complete yet.
- The layer tree can always be flattened to the simple `DisplayList`, preserving
  the existing low-end renderer path.

## Embedded Constraints

- Layer creation is sparse. Normal boxes paint into their parent layer.
- Sorting is only performed among child layers, not all layout boxes.
- Rectangular clipping is integer based and allocation-light.
- The model is designed so future dirty-rectangle repaint can invalidate one
  layer subtree instead of repainting the full document.

## Deferred

- Full CSS stacking-context algorithm.
- Transform matrices and transformed clipping.
- Filters, backdrop filters and blend modes.
- Retained display-list diffing.
- Texture allocation and GPU compositing.
