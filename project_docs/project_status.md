# JellyFrame Project Status And Milestones

Date: 2026-06-16

This document records the mainline project status. It covers JellyFrame's
hardware-neutral core, desktop validation tools, capability tooling and reviewed
port-support code that has been merged into the repository. Real panel drivers,
touch drivers, buttons, power management, ESP-IDF/LVGL board support and other
hardware porting implementation are outside the mainline core scope.

## Scope Boundary

Mainline owns:

- Hardware-neutral HTML/CSS/DOM/layout/render/input/script core code.
- Portable rendering support such as CPU framebuffers, dirty rectangles and the
  embedded framebuffer adapter.
- Text measurement/painting abstractions, the bitmap font backend, font-pack
  generation and capability checking.
- Desktop validation shells such as the pseudo browser and Win32 browser.
- Reviewing external port submissions, removing experimental assumptions,
  normalizing naming and merging reusable support code.
- Documentation, tests, benchmarks and release gates.

Mainline does not own:

- Real board panel drivers, touch drivers, buttons/crowns or power management.
- Long-term ESP-IDF, LVGL, FreeRTOS or BSP maintenance.
- Adding filesystems, networking, font-file loading, GPU APIs or windowing APIs
  inside the core.
- Debugging board-specific timing, DMA, bus bandwidth, backlight, sleep/wake or
  interrupt behavior.

Port packages should enter this repository only when the reusable boundary is
clear and the code does not contaminate the core. Hardware-specific differences
belong in `ports/` or in an external BSP.

## Completed Core Work

Parsing and DOM:

- Tolerant HTML tokenizer/parser for common tags, attributes, text, comments,
  doctypes, raw text, character references and error recovery.
- Lightweight DOM tree with element/text nodes, attributes, parent/child
  ownership, mutation APIs and dirty invalidation.
- Iterative DOM subtree teardown and `textContent` replacement to reduce stack
  pressure on deep generated trees.
- DOM statistics instrumentation for depth and attribute counts.

CSS and style:

- Tolerant CSS parser and lightweight CSSOM.
- Selector subset covering tag/class/id, descendant, child, attribute and
  `:root`.
- Indexed rule buckets and a bounded candidate-rule cache.
- Practical property subset including color, background, margin, padding,
  border, text, simplified flex/grid sizing, gap, aspect-ratio and approximate
  box-shadow.
- Explicitly deferred quirks mode, broad modern selectors, container queries and
  full dynamic CSSOM behavior.

Rendering pipeline:

- Render tree, layout tree, layer tree and display list.
- Simplified block/inline layout, text wrapping and intrinsic form-control
  sizing.
- Responsive grid-card subset and simple fixed grid templates.
- Sparse layers, clipping, opacity, stacking hints and flattening.
- CPU software rasterizer/compositor for rectangles, rounded fills, approximate
  gradients/shadows, text, form controls and BMP/PPM output.
- `FrameBuffer` dirty repaint and `HostFrameSink` presentation.
- `embedded_framebuffer` conversion from RGBA framebuffers into RGB565/BGR565,
  RGB332, Gray8 and 1-bit targets.

Input and events:

- Hit testing.
- DOM-style capture/target/bubble event dispatch.
- `InputController` for pointer, wheel, click synthesis, hover, active and
  focus state.
- Platform-neutral form-control state for text inputs, textareas, checkboxes,
  radios, ranges and selects.
- Keyboard text input, Backspace, focus navigation and focused activation.
- `pointerdown`/`pointerup` and `touchstart`/`touchend` dispatch.

JavaScript runtime:

- Optional `jellyframe_script`, kept outside `jellyframe_core`.
- JerryScript runtime lifecycle, eval and exception reporting.
- DOM bindings for `window`, `document`, `getElementById`, `createElement`,
  `createTextNode`, `appendChild`, `removeChild`, attributes and `textContent`.
- Event bindings for `addEventListener`, `removeEventListener`, event objects,
  default prevention and propagation control.
- Form properties: `value`, `checked`, `selectedIndex` and `select.value`.
- Host-pumped timers: `setTimeout`, `clearTimeout`, `setInterval` and
  `clearInterval`.
- Classic document script loading for inline `<script>` and local
  shell-provided `<script src>`.
- Embedded-app helpers: `dataset`, `children`, `parentElement`, simplified
  `matches`/`closest`, small `element.style`, `hidden` and `disabled`.

Text and fonts:

- `TextMeasureProvider` and `TextPainter` abstractions.
- Win32/GDI validation backend.
- Static bitmap font backend.
- BDF subset generator `jellyframe_font_pack_gen`.
- Font coverage checks through `jellyframe_capability_check --emit-used-chars`
  and `--font-coverage`.
- Visible stable-width fallback boxes for missing bitmap glyphs.

Tools, examples and validation:

- `jellyframe_dom_dump`, `cssom_dump`, `style_dump`, `render_tree_dump`,
  `layer_tree_dump` and `pipeline_dump`.
- `jellyframe_pseudo_browser` for full-pipeline framebuffer output.
- `jellyframe_win32_browser` for desktop interaction, GDI text and screenshots.
- `jellyframe_capability_check` for supported/degraded/unsupported feature
  scanning.
- `jellyframe_embedded_host_demo`, a platform-neutral static resource, RGB565,
  bitmap-font and input smoke.
- Small app acceptance pages for weather, clock, timer and calculator.
- Aggregated regression test executable `jellyframe_core_tests`.
- Microbenchmarks and virtual-board benchmark.

## Merged Port-Support Code

The following code came from external port experiments or exists to support
porting, but has been normalized for mainline:

- `ports/esp32s3-idf` bring-up project as an ESP32-S3/QEMU reference, not a
  promise that mainline owns real hardware port maintenance.
- ESP32-S3 static resource-bundle generator and P2 smoke resources.
- ESP32-S3 8 MB flash partition layout.
- ESP32-S3 RGB565 panel HAL reference: strided flush, packed dirty-rect flush,
  scratch row packing and flush statistics.
- ESP32-S3 P4/P5/P6 smoke support for bitmap font callbacks, a bounded board
  input queue, focus/text/control dispatch and dirty-rectangle RGB565
  presentation. The included bring-up font is a validation resource, not a
  production Chinese font pack.
- QEMU PSRAM gradient benchmark documents and results.
- `ports/virtual_board` desktop estimator.

Mainline keeps these files buildable, consistently named and cleanly bounded.
Real board driver quality, pin mapping, timing, DMA, display chips, touch chips
and RTOS integration belong to the porting side.

## Current Usability

JellyFrame is now suitable for developing and validating small local embedded UI
apps:

- Weather, clock, timer, calculator, settings pages and small dashboards are
  viable.
- Authors can use an HTML/CSS/JS subset instead of drawing every UI manually on
  canvas.
- Modern-looking but bounded UI is possible: grid/gap, cards, buttons, form
  controls, basic interaction, timers and DOM mutation.
- It is not suitable for arbitrary modern websites or full frontend frameworks.

The main remaining risks are:

- Incremental rebuild after dynamic DOM changes is still conservative.
- Full framebuffer remains the default rendering assumption.
- The default bitmap text route exists, but LVGL/vendor adapters do not yet have
  mainline examples.
- App packaging and release workflow are not settled.
- JerryScript is usable as a subset, but long-running resource budgets and
  stability need more testing.

## Next Core Milestones

### M7.5: Modern CSS Authoring Compatibility

Goal: keep common framework-authored CSS from collapsing while preserving the
embedded subset and avoiding expensive browser-complete features.

Status: started. The highest-return report items are now implemented:
custom-property `var()` fallback resolution, bounded conditional `@media`,
dynamic pseudo-class invalidation, `:is()` / `:where()`, sibling selectors and
simplified flex grow/shrink/basis sizing, plus bounded `relative`/`absolute`/
`fixed` positioned layout. The first conservative `@supports`
declaration-query subset is also available.

Remaining tasks:

- Broaden `@supports` tests only for declarations already supported by the
  embedded CSS subset; keep `selector()`, `:has()` and unsafe features false.
- Keep full `:has()`, full `@container`, full animation/filter/image pipelines
  deferred unless a small embedded app proves the need.

### M8: Run Loop And Incremental Update Contract

Goal: turn frame-loop experience from desktop shells and platform-neutral demos
into a clearer core/host contract.

Status: started. `src/core/frame_update.h` provides the first hardware-neutral
update planner plus a `FramePipelineCacheState` snapshot helper, and
`../docs/run_loop_contract.md` records the recommended run loop. Core tests now
cover a small host-frame sequence from first paint through clean, paint-only,
layout-dirty and resized-framebuffer frames. The planner now also has a
second-stage repaint check for the resolved layout height, so hosts can fall
back to a full framebuffer repaint when content height changes after layout.

Tasks:

- Define the recommended order for first paint, input dispatch, timer pumping,
  dirty collection, rebuild/repaint and present.
- Keep non-structural DOM mutation on paint-only or small repaint paths when
  possible.
- Preserve conservative full-viewport rebuild for structural mutation, with
  clear diagnostics.
- Add long-running timer/input smoke tests so queues and dirty flags do not grow
  without bound.

### M9: Finer Invalidation And Subtree Reuse

Goal: reduce unnecessary full-pipeline rebuilds after script-app interaction.

Status: started. Dirty flag clearing and dirty-region traversal now use
explicit work stacks and aggregate-dirty pruning, reducing stack pressure and
unnecessary clean-subtree scans. Dirty-region layout matching now scans
previous/current layout trees once and aggregates dirty-node bounds instead of
repeatedly searching the full layout tree. Structural `DomDirtyTree` updates now
skip previous-layout retention and plan a conservative full-frame repaint.
Layout, style and text changes compare previous/current layouts only after the
second-stage repaint plan confirms the framebuffer still matches the resolved
content size. Full subtree reuse is still future work. Form-control activation
also filters a few no-op paths so unchanged radio/select state does not schedule
paint work.

Tasks:

- Audit tree/style/layout/paint dirty-flag propagation boundaries.
- Reuse unchanged render/layout/layer subtrees where the data model allows it.
- Add tests and diagnostics for dirty layer/display-command invalidation.
- Keep the fallback simple: complex structural changes may still rebuild the
  full viewport.

### M10: Text Backend Adapters And Font Workflow

Goal: make Chinese and small-screen text more stable for production apps.

Tasks:

- Keep the bitmap font backend as the lowest-cost default route.
- Add a platform-neutral LVGL/vendor text backend adapter example or interface
  note.
- Expand tests for font coverage, missing glyphs, bold, wide glyphs,
  punctuation and scaling.
- Make the capability checker report font-pack size and missing-glyph impact
  more directly.

### M11: Resource Bundle And App Packaging

Goal: turn example resource loading into a repeatable local app packaging flow.

Tasks:

- Define a manifest for HTML/CSS/classic script/font/resources.
- Specify resource size limits, path resolution, caching and missing-resource
  behavior.
- Chain the capability checker, font-pack generator and resource-bundle
  generator into one desktop build flow.
- Keep the core filesystem-free and network-free; I/O remains host-provided.

### M12: Memory And Allocator Work

Goal: reduce small-object allocation and heap fragmentation further.

Tasks:

- Prototype `DomOwner` and detached-node instrumentation.
- Evaluate arena or pool strategies for mutable/scripted DOM.
- Expand small-vector/compact-list usage where it helps without harming
  readability.
- Add more stability tests for budget-exceeded paths.

### M13: Optional Tiled/Scanline Presentation

Goal: support devices that cannot keep a full framebuffer or target buffer.

Tasks:

- Evaluate a tile renderer or scanline compositor.
- Define which display commands can be tiled and which require fallback.
- Keep the current full-framebuffer path as the reliable default.

This milestone should move earlier only if target hardware cannot afford the
full-framebuffer path.

## Recommended Next Order

Mainline priority:

1. M8: deepen tests around the run-loop and dirty-update contract.
2. M9: implement finer invalidation and subtree reuse.
3. M10: improve text backend adapters and font workflow validation.
4. M11: define app packaging.
5. M12: continue memory and allocator optimization.
6. M13: decide on tiled presentation based on real hardware pressure.

Hardware porting can continue in parallel, but it should not block mainline core
work. When the porting side finds a missing core capability, it should provide a
minimal reproduction and clear requirement boundary; mainline can then decide
whether it belongs in one of the milestones above.
