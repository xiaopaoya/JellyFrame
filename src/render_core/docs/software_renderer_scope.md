# Software Renderer Scope


JellyFrame now has a CPU-only validation renderer. It is meant to prove the full
pipeline can produce pixels without assuming a GPU, display controller or
embedded windowing system.

Reference points:

- CSS 2 painting order and stacking are the practical basis for display-list
  ordering.
- CSS Compositing and Blending Level 1 defines the normal source-over
  compositing model used here.
- HTML's rendering section is advisory; JellyFrame keeps the implementation small
  and deterministic for wearable targets.

## Pipeline

```text
DOM + CSSOM
  -> computed style
  -> render tree
  -> layout tree
  -> layer tree
  -> display commands
  -> SoftwareRasterizer
  -> SoftwareCompositor
  -> FrameBuffer
  -> BMP / PPM output
```

## Implemented

- Straight-alpha `FrameBuffer` with integer RGBA pixels.
- Optional primary framebuffer pixel budget for `SoftwareCompositor::render()`;
  oversized renders return an empty framebuffer before allocating pixels.
- Source-over alpha compositing.
- Rectangle fills, stroke rectangles, non-layout outline strokes, cheap
  approximate `box-shadow`/`text-shadow` commands and two-color horizontal or
  vertical linear-gradient command support.
- Rounded rectangle clipping for filled backgrounds.
- Text drawing through a Windows GDI CPU mask when available.
- Built-in tiny ASCII fallback text drawing for non-Windows builds.
- Offscreen compositing for opacity/composited layers.
- `transform: translate()/scale()` for composited layers. Translation is rounded
  to integer pixels; scale uses nearest-neighbor sampling around the layer bounds
  center. This is intended for button feedback and card motion, not browser-level
  pixel parity.
- Optional offscreen pixel budget: oversized composited layers degrade to direct
  per-command opacity instead of allocating a large temporary RGBA framebuffer.
- BMP and PPM image writers for pseudo-browser validation.

## Deliberate Cuts

- No GPU surfaces.
- No blend modes beyond normal source-over.
- No filters, backdrop filters or real blur shadows.
- No real text shaping, bidi or font fallback stack.
- No image decode.
- No subpixel layout or antialiased geometry.
- No rotate/skew/matrix/perspective and no full `transform-origin`.

## Current Compatibility Notes

The renderer is good enough to reveal catastrophic pipeline failures: missing
boxes, broken CSS fallback, invalid clipping, empty output and text encoding
problems. It is not yet a pixel-compatible browser renderer.

Recent fixes added UTF-8 text output on Windows, conservative text overhang
padding, basic multiline text drawing, `box-sizing:border-box`, common
`rgb()/rgba()` colors, four-value box edges, minimal flex centering/row layout,
responsive grid-card layout, `aspect-ratio` sizing and cheap rounded
`box-shadow` approximations, plus the first `opacity`/2D-transform compositing
foundation for animation.
