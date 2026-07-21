# Render Core

> Last updated: 2026-07-22; Applies to: 0.5.0-dev

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
