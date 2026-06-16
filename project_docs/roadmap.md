# Roadmap

For the complete current status, scope boundary and next milestone definitions,
see `project_status.md`. This document keeps only the high-level route.

## Milestone 1: Static document core

- HTML subset: document nodes, text, common block and inline tags
- CSS subset: simple selectors and a small property set
- Layout: block flow and text measurement abstraction
- Rendering: sparse layer tree, clipping, and display list with rectangles, text
  and image placeholders

Status: mostly complete, with a broader app-oriented subset than originally
planned.

## Milestone 2: Embedded rendering backend

- Software framebuffer backend: available for validation
- Dirty rectangle repaint: first automatic `dirty_region` subset is available
  for non-structural DOM changes; finer layer/display-command invalidation is
  still needed
- Embedded framebuffer adapter: available for caller-owned RGBA8888/BGRA8888,
  RGB565/BGR565, RGB332, Gray8 and monochrome buffers
- Platform text measurement/painting backend: API exists, Win32/GDI validation
  backend exists, and the first static embedded bitmap backend plus BDF pack
  generator are available; LVGL/vendor adapters are still needed
- Pointer/touch input routing: pointer/wheel core exists; button/crown focus
  navigation has a first core API; board adapters are still needed
- Platform-neutral board bring-up shape: first static-resource/RGB565 demo is
  available through `jellyframe_embedded_host_demo`

## Milestone 3: App runtime

- JerryScript integration: optional scripting build available
- DOM mutation APIs: available
- Timer/event loop: host-pumped timers available
- Classic document script loading: available in scripting example shells
- Resource abstraction: callback-based local stylesheet/classic script loading is
  available to shells; network/fetch remains deliberately absent
- Device capability APIs: first `HostDeviceCapabilities` contract available;
  deeper automatic adaptation is deferred
- Centralized host budgets: wired into parser, render, layout, layer,
  display-list, dirty-region and scripting limits

## Milestone 4: Wearable UI features

- Small-screen viewport model
- Focus/navigation model for crown/buttons/touch: first core focus traversal and
  activation API is available
- Power-aware animation scheduling
- App packaging format

## Compatibility Short Track: Modern CSS authoring subset

- Highest-return compatibility items from the modern syntax report are now
  implemented: `var()` fallback resolution, bounded conditional `@media`,
  dynamic pseudo-classes, `:is()` / `:where()`, sibling selectors and
  simplified flex grow/shrink/basis sizing
- Conservative `@supports (property: value)` query flattening is available
- Bounded `relative`/`absolute`/`fixed` positioned layout is available for
  common app overlays
- Remaining work on this track is mostly incremental tests around already
  supported declarations
- Still deferred: full `:has()`, full `@container`, full animation/filter/image
  pipelines and browser-complete layout algorithms

## Recommended Next Order

1. Tighten the core run-loop and dirty-update contract, with long-running
   timer/input smoke coverage.
2. Implement finer invalidation and subtree reuse to reduce full pipeline
   rebuilds after script interaction.
3. Improve text backend adapters and font workflow validation while keeping the
   bitmap font backend as the low-cost default.
4. Finish local resource bundle tooling and app packaging.
5. Continue memory and allocator work, including a `DomOwner` prototype and
   detached-node instrumentation.
6. Move tiled/scanline presentation forward only when target hardware needs it.
