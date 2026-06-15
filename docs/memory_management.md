# Memory Management Review

JellyFrame currently favors explicit ownership and small, predictable containers.
That is the right baseline for embedded targets, but several allocations are
still desktop-convenient rather than MCU-tight.

## What Is Already Suitable

- DOM, render tree, layout tree and layer tree objects use `std::unique_ptr`, so
  ownership is single-path and teardown is deterministic.
- Core builds do not link JerryScript, Win32, file I/O loaders or platform font
  code unless optional targets ask for them.
- Event listener storage is lazy: nodes without listeners do not allocate a
  listener table.
- Form-control state is lazy: ordinary elements do not carry control state.
- Parser and example file inputs are bounded.
- `HostBudgets` now maps into HTML parser, CSS parser, render tree, layout tree,
  layer tree, flattened display-list, dirty-rectangle, JerryScript timer and
  script event-listener limits.
- Timer callbacks are retained explicitly as JerryScript references and released
  on `clearTimeout`, `clearInterval`, document rebinding and runtime shutdown.
- The M6 timer queue is host-pumped with a callback budget, so a burst of due
  timers cannot monopolize a frame indefinitely.
- `MonotonicArena` is now available as a core memory utility. It supports block
  based linear allocation, reverse-order destruction and full arena reset.
  Render, layout and layer trees now have arena-backed build paths, used by
  microbench, virtual board and ESP32-S3 benchmarks; DOM nodes still use the
  original ownership model.

## Embedded Risks

- DOM attributes now use a compact sequential `AttributeList` instead of a
  per-node `std::unordered_map`. Most embedded UI nodes have only a few
  attributes, so linear scans are more memory-predictable than hash buckets.
- DOM nodes still allocate many small objects independently. This is clear and
  safe, but can fragment small heaps.
- DOM subtree teardown is iterative, including whole-subtree `textContent`
  replacement. Some other tree walks remain recursive, so very small stacks may
  still need iterative traversal for parsing and dirty-flag scans.
- Framebuffer memory is linear in viewport size. A 390x640 RGBA buffer is about
  1 MiB before any offscreen layer.
- Embedded presentation can avoid a second RGBA-sized display buffer by using
  `embedded_framebuffer` to convert dirty rectangles into a host-owned RGB565,
  grayscale or monochrome buffer.
- Offscreen compositing can allocate temporary framebuffers for opacity or
  transformed layers; this path still needs a strict host budget.
- Text strings are stored as `std::string`; future text shaping or large pages
  will need stricter string lifetime and deduplication policy.
- `StyleResolver` keeps a bounded candidate-rule cache. It trades a small,
  configurable amount of resolver-owned memory for fewer repeated bucket merges
  on class-heavy UI trees, while still recalculating cascade results per node.
- Script wrappers are short-lived and allocated on demand. This avoids stale
  ownership, but repeated hot-path wrapper creation may cost memory churn.

## Recommended Next Optimizations

1. Evaluate whether DOM nodes should move to a document arena. This is higher
   risk than render/layout/layer because parsing, mutation and scripting all
   observe node ownership.
2. Keep a hard framebuffer policy for embedded targets: one primary framebuffer,
   optional dirty rectangles, one host-owned converted display buffer when
   needed and tightly bounded offscreen buffers.
3. Convert recursive tree walks to iterative walks when a target stack budget is
   known to be small.
4. Plumb budgets into remaining expensive paths: offscreen compositing,
   resource aggregation and future image/font decoders.
5. Consider full computed-style sharing only after the candidate-rule cache is
   measured on real apps; full sharing must account for inherited values and
   mutation invalidation.
6. Consider wrapper caching only after measuring script-heavy apps; it saves
   repeated wrapper creation but retains more JerryScript objects.

## Current Decision

The project is now safer for bounded embedded bring-up because major pipeline
stages consume `HostBudgets`, and DOM attributes no longer use per-node hash
maps. It is still not heap-optimal for tiny MCUs. Render, layout and layer
trees now have arena-backed build paths, and DOM teardown no longer recursively
walks child destructors. The biggest remaining allocation question is whether
DOM nodes should move to a document arena without hurting mutation/script
readability.
