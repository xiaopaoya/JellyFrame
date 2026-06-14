# Embedded Optimization Notes

Date: 2026-06-14

The exact target CPU, memory map, display controller and instruction set are not
known yet, so current optimizations focus on portable constraints that matter on
small wearable devices.

## Current Choices

- No exceptions on hot parser paths.
- Parser stages are linear scans with explicit limits.
- DOM, CSS and diagnostic file inputs are bounded.
- DOM parsing streams tokenizer output into the tree builder.
- Render tree filters non-visual nodes before layout.
- Layout only computes geometry; paint organization is handled by the sparse
  layer tree.
- Layer tree creation is sparse: ordinary boxes paint into their parent layer.
- Display list uses simple rectangle and text commands after layer flattening.
- Borders are emitted as fill rectangles.
- Unsupported modern CSS is skipped at block/rule boundaries, avoiding recovery
  loops.
- Style cascade slots use a fixed array instead of a per-node hash map.
- DOM event listener storage is allocated lazily, so nodes without listeners do
  not carry an empty listener table.
- Script timers are host-pumped with a callback budget and explicit JerryScript
  reference release paths.
- Platform text painting is injected through an optional callback; the core
  renderer keeps a portable bitmap fallback and does not link Win32/GDI.
- Opaque rectangle fill uses direct row fills in the software rasterizer.
- Offscreen compositing clips source/destination rectangles before iterating
  pixels.

## Memory Guidance

- Replace recursive destructors/traversals if target stack is very small.
- Add arena allocation for DOM/render/layout objects once object lifetimes are
  tied to a document.
- Replace small per-node attribute hash maps with compact small-vector
  attributes before targeting tiny heaps.
- Index CSS rules by id/class/tag before loading real-world large stylesheets.
- Keep layer/display-list output bounded or tile it by dirty region on small RAM
  systems.
- See `docs/memory_management.md` for the current ownership and allocation
  review.

## CPU/Instruction-Set Guidance

- Favor branch-predictable ASCII fast paths in tokenizer and CSS parser.
- Keep UTF-8 decoding isolated to entity/codepoint conversion until text shaping
  exists.
- Avoid SIMD until target ISA is known; Cortex-M, Cortex-A, RISC-V and x86 have
  very different payoffs.
- Prefer integer geometry and fixed-point style units.

## I/O Guidance

- Avoid synchronous network/font/image decode in the render pipeline.
- Stream resources into bounded buffers.
- Defer image decoding until layout has established a visible box.
- Use dirty rectangles for display flushes.

## Release Microbenchmark Baseline

Command:

```powershell
.\build\Release\wearweb_microbench.exe 80 1000
```

Result on this Windows build machine after M6 host-pumped timers and the current
form/control layout changes:

```text
html_parse avg_us=949.728
css_parse avg_us=36.025
render_tree avg_us=1058.55
layout avg_us=224.273
layer_tree avg_us=109.392
flatten_layers avg_us=24.534
full_pipeline avg_us=2163.95
```

Interpretation:

- Debug numbers are not useful for performance decisions.
- Rule indexing reduced render-tree/style-resolution cost substantially.
- Fixed cascade slots removed per-node cascade hash-map setup.
- Lazy event listener storage prevents event support from increasing the common
  no-listener DOM node footprint.
- Layer tree adds a small explicit cost, but keeps paint organization ready for
  clipping, ordering and later dirty-layer repaint.
- Broader fallback CSS and inherited text properties increased style/render
  work, but avoid catastrophic visual failures on common modern pages.
- Extra wrapped-text line-height padding avoids clipping with native text
  backends at a small layout cost.
- Embedded `<style>` collection and broader length/property support improve
  common static pages with a small style/render cost.
- Full pipeline time is still dominated by HTML parse and style/render work.
- The next performance upgrade should be arena allocation and computed-style
  sharing for repeated class patterns.
