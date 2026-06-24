# Embedded Optimization Notes


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
- Style resolution caches bounded id/class/tag candidate rule sets inside
  `StyleResolver`; final selector matching still runs per node, so descendant,
  child and attribute selector semantics remain correct.
- DOM attributes use compact sequential `AttributeList` storage instead of a
  per-node hash map.
- DOM event listener storage is allocated lazily, so nodes without listeners do
  not carry an empty listener table.
- DOM dirty bits propagate to ancestors, so root dirty checks are O(1), clean
  subtrees are skipped during dirty clearing, and unchanged `textContent`
  assignments do not trigger rerenders. Updating an element that already has a
  single text child keeps that child in place, avoiding structural dirty work for
  counters and timer labels.
- DOM subtree teardown and whole-subtree `textContent` replacement use an
  explicit work list instead of recursive child destruction, reducing stack risk
  on very deep generated documents.
- Script timers are host-pumped with a callback budget and explicit JerryScript
  reference release paths.
- Platform text painting is injected through an optional callback; the core
  renderer keeps a portable bitmap fallback and does not link Win32/GDI.
- Opaque rectangle fill uses direct row fills in the software rasterizer.
- Offscreen compositing clips source/destination rectangles before iterating
  pixels.
- `embedded_framebuffer` converts only clipped dirty rectangles into
  caller-owned display buffers and does not allocate, retain or flush device
  memory itself.
- `HostFrameSink::present` is a frame-lifetime boundary. If the underlying panel
  path uses asynchronous DMA, the host must make buffers reusable before
  returning, or the UI loop must wait for a flush-done event before starting the
  next render.
- `FrameScratch` and `AppFrameScratch` provide reusable frame-scratch storage.
  Dirty-region bounds, dirty rectangles, animation style overrides and host
  completion batch/accepted lists can keep capacity across frames and be cleared
  each frame. Sleep, app switching and memory-pressure paths can call
  `release()` to return that capacity.
- The responsive grid subset is computed with bounded integer auto-placement,
  clamped spans and compact per-row occupancy bit masks rather than a full
  track-sizing engine.
- `MonotonicArena` is available for document-lifetime allocation. Render,
  layout and layer tree builders expose arena-backed paths for embedded
  benchmarks.
- Arena accounting reports both used bytes and block capacity, so benchmark
  logs can distinguish live pipeline data from block-allocation slack.

## Memory Guidance

- Replace recursive destructors/traversals if target stack is very small.
- Evaluate whether DOM nodes should move to a document arena.
- Keep `AttributeList` unless measurements show a need for tiny id/class
  indexes; compact UI nodes are smaller without hash buckets.
- CSS rules are already indexed by id/class/tag/universal buckets and reuse a
  bounded candidate cache; full computed-style sharing remains deferred until
  inheritance and mutation invalidation can be kept simple.
- Keep layer/display-list output bounded or tile it by dirty region on small RAM
  systems.
- Do not pin a full-screen RGB565 target in internal RAM unless measurements
  prove the product can afford it. Prefer PSRAM for persistent framebuffers and
  targets, or use a small DMA-capable strip buffer per dirty region.
- Separate retained and per-frame memory. DOM, stylesheets, form state, decoded
  surface caches, persistent framebuffers and retained layout/layer trees may
  live across frames. Parser scratch, dirty-rect lists, host completion scratch,
  offscreen compositing buffers and strip conversion buffers should be released
  or reused at frame boundaries.
- A UI loop should normally own one `FrameScratch` and one `AppFrameScratch`.
  Call `begin_frame()` / `end_frame()` on regular frames to reuse storage, and
  call `release()` on screen-off, app exit, system-shell switches or low heap
  watermarks.
- JellyFrame can release or reuse the pure software scratch containers above,
  but it cannot safely free real panel DMA buffers, driver-owned bounce buffers
  or memory still being read by a display controller. Those objects must remain
  port-owned and obey the flush-done boundary.

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
- Keep display buffers host-owned and convert through `embedded_framebuffer`
  when the panel is RGB565, RGB332, grayscale or monochrome.
- If the panel driver only accepts internal DMA buffers, write a custom
  `HostFrameSink` that converts from the RGBA framebuffer by strip, waits for
  each strip flush to complete and reuses a small scratch buffer. Do not keep a
  full-screen internal RGB565 target just to use the generic adapter.

## Release Microbenchmark Baseline

Command:

```powershell
.\build\Release\jellyframe_render_core_microbench.exe 80 1000
```

Result on this Windows build machine after the responsive grid/aspect-ratio
layout subset:

```text
html_parse avg_us=990.255
css_parse avg_us=35.898
render_tree avg_us=1054.36
layout avg_us=259.59
layer_tree avg_us=117.187
flatten_layers avg_us=24.529
full_pipeline avg_us=2228.91
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
- Responsive grid cards and `aspect-ratio` add measurable layout work but keep
  the cost bounded and buy a large amount of embedded-app UI expressiveness.
- Full pipeline time is still dominated by HTML parse and style/render work.
- The next performance upgrade should target computed-style sharing for
  repeated class patterns and tile/scanline presentation if hardware memory
  pressure proves the full framebuffer path too expensive.
