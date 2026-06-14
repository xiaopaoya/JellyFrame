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

## Memory Guidance

- Replace recursive destructors/traversals if target stack is very small.
- Add arena allocation for DOM/render/layout objects once object lifetimes are
  tied to a document.
- Index CSS rules by id/class/tag before loading real-world large stylesheets.
- Keep layer/display-list output bounded or tile it by dirty region on small RAM
  systems.

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

Result on this Windows build machine after software renderer integration,
text fixes and broader CSS fallback support:

```text
html_parse avg_us=854.436
css_parse avg_us=35.76
render_tree avg_us=940.971
layout avg_us=211.725
layer_tree avg_us=82.919
flatten_layers avg_us=24.454
full_pipeline avg_us=1976.64
```

Interpretation:

- Debug numbers are not useful for performance decisions.
- Rule indexing reduced render-tree/style-resolution cost substantially.
- Fixed cascade slots removed per-node cascade hash-map setup.
- Layer tree adds a small explicit cost, but keeps paint organization ready for
  clipping, ordering and later dirty-layer repaint.
- Broader fallback CSS and inherited text properties increased style/render
  work, but avoid catastrophic visual failures on common modern pages.
- Full pipeline time is still dominated by HTML parse and style/render work.
- The next performance upgrade should be arena allocation and computed-style
  sharing for repeated class patterns.
