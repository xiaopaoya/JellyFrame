# Memory Management Review

WearWeb currently favors explicit ownership and small, predictable containers.
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
- Timer callbacks are retained explicitly as JerryScript references and released
  on `clearTimeout`, `clearInterval`, document rebinding and runtime shutdown.
- The M6 timer queue is host-pumped with a callback budget, so a burst of due
  timers cannot monopolize a frame indefinitely.

## Embedded Risks

- DOM attributes use `std::unordered_map`, which is simple but heavy for small
  elements. Most embedded UI nodes have only a few attributes.
- DOM/render/layout/layer trees allocate many small objects independently. This
  is clear and safe, but can fragment small heaps.
- Several tree operations are recursive. Very small stacks may need iterative
  traversal for parsing, dirty-flag scans and teardown.
- Framebuffer memory is linear in viewport size. A 390x640 RGBA buffer is about
  1 MiB before any offscreen layer.
- Offscreen compositing can allocate temporary framebuffers for opacity or
  transformed layers.
- Text strings are stored as `std::string`; future text shaping or large pages
  will need stricter string lifetime and deduplication policy.
- Script wrappers are short-lived and allocated on demand. This avoids stale
  ownership, but repeated hot-path wrapper creation may cost memory churn.

## Recommended Next Optimizations

1. Replace per-node attribute hash maps with a small-vector attribute list, then
   optionally add a tiny indexed lookup for `id` and `class`.
2. Add optional arena allocation for document-lifetime objects: DOM nodes,
   render objects, layout boxes and layer nodes.
3. Keep a hard framebuffer policy for embedded targets: one primary framebuffer,
   optional dirty rectangles and tightly bounded offscreen buffers.
4. Convert recursive tree walks to iterative walks when a target stack budget is
   known to be small.
5. Add host-configurable budgets for maximum DOM nodes, CSS rules, display
   commands, timers and retained JS event listeners.
6. Consider wrapper caching only after measuring script-heavy apps; it saves
   repeated wrapper creation but retains more JerryScript objects.

## Current Decision

The project is safe enough to continue M6/M7 development, but it is not yet
heap-optimal for tiny MCUs. The biggest future win is a document arena plus
small-vector attributes; both preserve readability while removing many small
heap allocations.
