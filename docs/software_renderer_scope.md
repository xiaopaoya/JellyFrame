# Software Renderer Scope

Date: 2026-06-14

WearWeb now has a CPU-only validation renderer. It is meant to prove the full
pipeline can produce pixels without assuming a GPU, display controller or
embedded windowing system.

Reference points:

- CSS 2 painting order and stacking are the practical basis for display-list
  ordering.
- CSS Compositing and Blending Level 1 defines the normal source-over
  compositing model used here.
- HTML's rendering section is advisory; WearWeb keeps the implementation small
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
- Source-over alpha compositing.
- Rectangle fills, stroke rectangles, cheap approximate `box-shadow` fills and
  simple vertical linear-gradient command support.
- Rounded rectangle clipping for filled backgrounds.
- Text drawing through a Windows GDI CPU mask when available.
- Built-in tiny ASCII fallback text drawing for non-Windows builds.
- Offscreen compositing for opacity/composited layers.
- BMP and PPM image writers for pseudo-browser validation.

## Deliberate Cuts

- No GPU surfaces.
- No blend modes beyond normal source-over.
- No filters, backdrop filters or real blur shadows.
- No real text shaping, bidi or font fallback stack.
- No image decode.
- No subpixel layout or antialiased geometry.

## Current Compatibility Notes

The renderer is good enough to reveal catastrophic pipeline failures: missing
boxes, broken CSS fallback, invalid clipping, empty output and text encoding
problems. It is not yet a pixel-compatible browser renderer.

Recent fixes added UTF-8 text output on Windows, conservative text overhang
padding, basic multiline text drawing, `box-sizing:border-box`, common
`rgb()/rgba()` colors, four-value box edges, minimal flex centering/row layout,
responsive grid-card layout, `aspect-ratio` sizing and cheap rounded
`box-shadow` approximations.
