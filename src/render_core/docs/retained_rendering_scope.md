# Retained Rendering and Tile/Scanline Scope

> Last updated: 2026-08-14; Applies to: 0.6.0-dev

This document defines the boundary for two deferred areas: retained layout/display-list
diffing for structural changes, and rendering without a full framebuffer. It is a Render Core
contract, not a port acceptance report.

## Current state

- Render Core retains render, layout, layer and display-list structures across frames.
- `FrameUpdatePlan` supports paint-only updates, stable-layout updates and old/new layout dirty
  bounds. `DirtyRegionOptions` can compare old/new layer trees and transient overlay bounds.
- Structural DOM changes conservatively fall back to a full frame. Complete retained layout or
  display-list diffing is not implemented. The value-only v2 local-mutation probe measured a
  promising workload, but remains read-only telemetry rather than a rendering optimization.
- The default software compositor renders into a logical RGBA framebuffer. An embedded adapter
  can convert dirty rectangles into compact strips, but that is not a no-framebuffer renderer.

## Staged plan

Do not begin with arbitrary command reuse. First add an optional, bounded candidate-fingerprint
probe. Its input must cover structure, computed paint/layout output, resource versions and child
order. It must report hits, misses, duplicate keys, resource invalidation, geometry/clip changes
and conservative fallbacks without changing pixels. It must not use raw DOM/layout/command
pointers as persistent identity, and disabled profiles must allocate no cache.

Only a later, opt-in subtree replay slice may reuse commands. It must require unique stable keys,
unchanged bounds/clip/transform/opacity/scroll state, safe occlusion rules, resource generations,
and independent byte/command budgets. Any uncertainty must use the existing rebuild and dirty/full
repaint path. For value-only frames, `project_docs/value_only_frame_retained_diff_replay_rfc.md`
adds the stricter clear-and-replay contract: clear the conservative union of old/new visual bounds,
then redraw every current command intersecting it in paint order. Equal commands are not permission
to skip paint.

Tile/scanline rendering is a separate output-model change. It needs real device evidence that a
full framebuffer cannot fit the target budget or causes a measured failure after dirty/strip
presentation and port-local conversion have been evaluated. The platform-neutral interface may
expose only a bounded tile or scanline target; it must not expose panel, DMA, GRAM or MCU details.
Disabled profiles must allocate no tile scratch and retain the framebuffer fallback. Validation
must include text, rounded geometry, alpha, gradients, images, shadows and scrolling.

For 0.5, keep the existing conservative behavior. For 0.6, measure candidate fingerprints first;
promote subtree replay only after pixel, memory and frame-time evidence. Keep tile/scanline as a
separate milestone so two high-risk changes do not enter firmware together.
