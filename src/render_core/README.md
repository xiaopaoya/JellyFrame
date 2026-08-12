# Render Core

> Last updated: 2026-08-11; Applies to: 0.6.0-dev

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

The target name is `jellyframe_render_core`. The top-level build can configure
this target without the upper layers:

```powershell
cmake -S . -B build\render-core-standalone `
  -DJELLYFRAME_BUILD_APP_RUNTIME=OFF `
  -DJELLYFRAME_BUILD_SCRIPTING=OFF `
  -DJELLYFRAME_BUILD_EXAMPLES=OFF `
  -DJELLYFRAME_BUILD_BENCHMARKS=OFF `
  -DJELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS=OFF
cmake --build build\render-core-standalone --config Release --target jellyframe_render_core_tests
ctest --test-dir build\render-core-standalone -C Release --output-on-failure
```

For a downstream CMake consumer, set `JELLYFRAME_INSTALL_RENDER_CORE=ON` and
run `cmake --install`. The install exports `JellyFrame::jellyframe_render_core`,
the public headers and the generated capability profile. The package is a
build artifact boundary; it does not include App Runtime, JerryScript, ports or
device protocols.

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

The 0.6 modularization introduces feature-family boundaries above this file
layout. `core.document` and `core.paint` are mandatory source families, and
the first optional slices are `graphics.canvas2d`, `css.modern-paint`,
`css.flex-grid` and `forms.advanced`. A family may span parser, computed style,
layout, paint and input files, but it must have an explicit registration point,
dependency list, budget and profile gate. The generated profile records source
ownership for both mandatory families and every separately compiled optional
family; the profile regression rejects gaps or overlaps in the mandatory split. See
`project_docs/render_pipeline_modularity_plan_zh.md` for the build-profile and
manifest contract. Ordinary App packages remain data/code at the declared
runtime level; they do not load native Render Core modules.

The feature catalog is declared in `cmake/render_core_feature_registry.csv`.
Build configuration and desktop package/link-map tools consume the same IDs and
dependency closure; adding a feature requires updating this catalog and its
regression evidence before a profile can advertise it.

The build-time slices are `JELLYFRAME_ENABLE_CANVAS2D`,
`JELLYFRAME_ENABLE_MODERN_PAINT`, `JELLYFRAME_ENABLE_FLEX_GRID` and
`JELLYFRAME_ENABLE_ADVANCED_FORMS`. When Canvas
is enabled, the full Canvas 2D implementation is linked. When disabled, the stable
`Canvas2DRegistry` API is backed by `canvas2d_disabled.cpp`; calls fail safely,
allocate no Canvas surface state and keep existing host consumers linkable. The
generated `jellyframe_render_core_profile.json` in the build directory records
the selected feature set, package version and engine ABI for package/check
integration. Its sibling `jellyframe_render_core_provenance.json` records the
selected provider and consumer lock values for build-report archival without
capturing machine-specific source paths. The profile must also contain the dependency closure: `core.paint` and
`css.flex-grid` and `forms.advanced` depend on `core.document`, while
`css.modern-paint` and `graphics.canvas2d` depend on `core.paint`. The packager
rejects a profile that advertises a family without these prerequisites, rather
than allowing a later runtime failure.

The modern paint family is controlled by `JELLYFRAME_ENABLE_MODERN_PAINT` (ON by
default). It covers the bounded gradient and shadow display commands. When it is
OFF, gradient declarations retain an earlier solid-color fallback, shadow
declarations are rejected without creating shadow layers, and direct gradient
display commands are painted as their first color. This is a build-time family
gate, not an App-loadable native module.

`JELLYFRAME_ENABLE_FLEX_GRID` is also ON by default. It gates the documented
flex/grid parser, computed-style, layout and paint-order paths as one vertical
family. When disabled, matching declarations and `@supports` conditions are
rejected and the document uses the existing block/inline fallback; it does not
allocate a runtime feature registry or permit an App to re-enable the family.
Generated profile IDs are deterministic for every Canvas/modern-paint/flex-grid/
advanced-forms combination and are covered by a configure-only regression test.

Retained rendering boundaries are explicit. The existing layer/display-list trees are retained
storage and dirty-region inputs, not a complete structural display-list diff. Structural mutation
still uses the conservative rebuild/full-frame fallback. The default compositor also still owns a
logical framebuffer; compact dirty-rect/strip output is provided by the embedded adapter. Candidate
fingerprinting and a no-full-framebuffer tile/scanline target are deferred, opt-in 0.6 work with
separate memory, pixel and frame-time gates. See `docs/retained_rendering_scope.md` and
`docs/retained_rendering_scope_zh.md`.

`JELLYFRAME_ENABLE_ADVANCED_FORMS` is ON by default. It owns local constraint
validation, custom validity, bounded `FormData`, `SubmitEvent`, `requestSubmit`
and cancellable reset/default actions. When disabled, normal form controls keep
their base value and input behavior, but those advanced C++ APIs resolve to
safe no-op stubs, input activation does not submit/reset, and JerryScript does
not expose `FormData`, validation accessors or advanced form methods. This is a
firmware build decision; an App cannot enable the family at runtime.

Modern paint prepares rounded-rectangle geometry once per display command and
reuses it across the pixel loop. Shared geometry arithmetic uses saturating
helpers for command offsets, shadow expansion and radius expansion; this keeps
malformed or extreme desktop input from turning into signed-overflow behavior
without adding work to square, unrounded paint paths. The same code is used by
the basic renderer and the optional modern-paint family, so this optimization
does not depend on a panel, DMA engine or MCU instruction set.

The bounded soft-shadow path additionally resolves non-circular rounded-rect
geometry and the y-axis distance once per scanline. It preserves the existing
quadratic falloff, alpha composition and clip behavior; true circular shadows
retain their exact-distance path to avoid reintroducing polygonal rings. The
path owns no cache or extra surface and applies only when `box-shadow` is
already being rasterized.

For opt-in diagnosis of value-frame v2 rounded clips, a host can provide both
`SoftwareRasterizerStatistics` and a monotonic
`SoftwareRasterizerTiming::now_microseconds` callback. The Core then records
three disjoint phases: temporary-surface preparation (including clear),
per-command replay, and rounded-coverage composition back into the target.
Without that callback it performs no clock reads or timing bookkeeping. These
figures attribute one bounded workload; they are not device-FPS claims.

Desktop validation builds also emit `jellyframe_render_core_microbench.map` and
`jellyframe_render_core_tests.map`. Check a map against its generated profile
with:

```powershell
python project_tools\check_render_core_link_map.py `
  --profile build\module-on\generated\jellyframe_render_core_profile.json `
  --map build\module-on\jellyframe_render_core_microbench.map
```

CTest also checks the generated `jellyframe_render_core_tests` map against the
active profile through CMake target metadata, so a default CI build cannot
silently advertise a separately compiled family that its validation executable
failed to link. A scoped map check is still needed for an embedded workload that
intentionally does not exercise every enabled family.

The Render Core microbench reports both legacy `avg_us` smoke metrics and
`modern_paint_*_stats` lines with p50/p95 latency, display-command count and
the measured wearable surface byte count. These are desktop attribution
signals, not MCU frame-rate or flash/RAM claims; those remain port evidence.
The link-map checker can prove separately linked Canvas and modern-paint
objects. Flex/grid is compile-gated inside shared parser/style/layout/layer
units, so its checker entry deliberately reports `not-applicable` rather than
pretending that a shared object marker proves the feature. Use the generated
profile regression plus a real ON/OFF behavior workload for that family.
