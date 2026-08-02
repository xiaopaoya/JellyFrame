# Render Core

> Last updated: 2026-08-02; Applies to: 0.5.0-dev

`render_core` is JellyFrame's platform-neutral Living Standard/CSS subset and
software rendering pipeline.

It owns:

- HTML tokenizer, parser and tree builder.
- Lightweight DOM, events, input, form controls and dirty flags.
- CSS parser, CSSOM data, style resolution and diagnostics for the documented
  small-screen subset: responsive Grid/flex, typography/overflow controls,
  bounded gradients, shadows, image backgrounds and explicit CSS nesting.
- Bounded animation timeline support for paint/compositor-safe CSS transition
  and `@keyframes` / `animation-*` subsets.
- Render tree, layout tree, layer tree, display list and frame update planning.
- CPU software rasterization/compositing, antialiased rounded geometry and
  embedded framebuffer conversion with optional RGB565/BGR565 dithering.
- Dirty-rect and scroll-blit planning for host-owned incremental presentation.
- Text measurement/painting contracts and bitmap font helpers.
- Neutral host contracts such as device capabilities, frame sink callbacks,
  resource request hooks and memory/time budgets.

It must not depend on JerryScript, app installation, registries, networking,
filesystems, OS APIs, RTOS APIs or vendor display/input libraries.

The target name is `jellyframe_render_core`.

## Source layout

The directory is intentionally a flat C++ module today. The files are split by
pipeline responsibility rather than by HTML tag or CSS property:

| Area | Files | Responsibility |
|---|---|---|
| Document | `html_tokenizer.*`, `html_parser.*`, `html_tree_builder.*`, `dom.*`, `dom_owner.*` | Bounded HTML input and DOM lifetime |
| Style | `css_parser.*`, `style.*`, `document_style.*`, `style_repaint.*` | CSS subset, cascade, computed style and invalidation |
| Layout | `layout.*`, `render_tree.*`, `hit_test.*`, `form_control.*` | Geometry, render nodes, hit regions and controls |
| Paint/compositor | `layer_tree.*`, `software_renderer.*`, `modern_paint.*`, `raster_primitives.h`, `embedded_framebuffer.*` | Display commands, shared coverage/blend primitives, optional modern-paint rasterization and host output |
| Frame scheduling | `frame_loop.*`, `frame_update.*`, `frame_scratch.h`, `dirty_region.*`, `display_invalidation.*`, `animation_*.*` | Dirty work, animation and per-frame planning |
| Input/events | `input.*`, `event.*`, `scroll_gesture.h`, `scroll_blit.*` | Platform-neutral input, dispatch and scroll planning |
| Text/resources | `text_*.*`, `bitmap_font*.*` | Text backend contracts, scanning, bitmap fonts and `.jffont` data |
| Contracts/telemetry | `host.h`, `budget.h`, `diagnostics.h`, `geometry.h`, `pipeline_statistics.*`, `arena.*` | Host boundary, budgets, diagnostics and bounded storage |

The flat layout is not a claim that all code belongs in one inseparable binary.
It reflects the current include graph and keeps the 0.5 public headers stable
while the pipeline is still being validated. In particular, `style.cpp`,
`layout.cpp`, `layer_tree.cpp`, `software_renderer.cpp` and `css_parser.cpp`
are currently large cross-cutting units; moving them into arbitrary folders
would not by itself reduce firmware size or runtime cost.

The planned 0.6 modularization will introduce feature-family boundaries above
this file layout. The first candidates are `graphics.canvas2d`,
`css.modern-paint` and `css.flex-grid`. A family may span parser, computed style,
layout and paint files, but it must have an explicit registration point,
dependency list, budget and profile gate. See
`project_docs/render_pipeline_modularity_plan_zh.md` for the build-profile and
manifest contract. Ordinary App packages remain data/code at the declared
runtime level; they do not load native Render Core modules.

The first build-time slices are `JELLYFRAME_ENABLE_CANVAS2D` and
`JELLYFRAME_ENABLE_MODERN_PAINT`. When Canvas is enabled, the
full Canvas 2D implementation is linked. When disabled, the stable
`Canvas2DRegistry` API is backed by `canvas2d_disabled.cpp`; calls fail safely,
allocate no Canvas surface state and keep existing host consumers linkable. The
generated `jellyframe_render_core_profile.json` in the build directory records
the selected feature set and engine ABI for package/check integration. The
profile must also contain the dependency closure: `core.paint` and
`css.flex-grid` depend on `core.document`, `css.modern-paint` depends on
`core.paint`, and `graphics.canvas2d` depends on `core.paint`. The packager
rejects a profile that advertises a family without these prerequisites, rather
than allowing a later runtime failure.

The modern paint family is controlled by `JELLYFRAME_ENABLE_MODERN_PAINT` (ON by
default). It covers the bounded gradient and shadow display commands. When it is
OFF, gradient declarations retain an earlier solid-color fallback, shadow
declarations are rejected without creating shadow layers, and direct gradient
display commands are painted as their first color. This is a build-time family
gate, not an App-loadable native module.

Modern paint prepares rounded-rectangle geometry once per display command and
reuses it across the pixel loop. Shared geometry arithmetic uses saturating
helpers for command offsets, shadow expansion and radius expansion; this keeps
malformed or extreme desktop input from turning into signed-overflow behavior
without adding work to square, unrounded paint paths. The same code is used by
the basic renderer and the optional modern-paint family, so this optimization
does not depend on a panel, DMA engine or MCU instruction set.

Desktop validation builds also emit `jellyframe_render_core_microbench.map` and
`jellyframe_render_core_tests.map`. Check a map against its generated profile
with:

```powershell
python tools\check_render_core_link_map.py `
  --profile build\module-on\generated\jellyframe_render_core_profile.json `
  --map build\module-on\jellyframe_render_core_microbench.map
```

The Render Core microbench reports both legacy `avg_us` smoke metrics and
`modern_paint_*_stats` lines with p50/p95 latency, display-command count and
the measured wearable surface byte count. These are desktop attribution
signals, not MCU frame-rate or flash/RAM claims; those remain port evidence.
