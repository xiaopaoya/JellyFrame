# App Author Guide

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This is the short contract for people writing JellyFrame apps. JellyFrame is
not a mini browser. It is a Web-shaped embedded UI runtime: HTML gives
structure, CSS gives a documented small-screen styling subset, JavaScript adds
bounded local interaction, and the manifest declares the target/device services
the app expects.

If you write it like an arbitrary web page, you will hit limits. If you write it
like a small wearable app with explicit targets and budgets, the toolchain can
help you ship predictable UI.

For the clean-room trial gate, including packaging, launcher and hardware
evidence boundaries, see
[external_developer_trial_readiness.md](external_developer_trial_readiness.md).

## Start Here

Use this loop for every app:

```powershell
python tools\jellyframe_cli.py new `
  --template weather `
  --output build\my_weather `
  --id org.example.weather `
  --name Weather `
  --target round-300

python tools\jellyframe_cli.py check `
  --root build\my_weather `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\my_weather.report.json `
  --build-dir build\Release

.\build\Release\jellyframe_win32_browser.exe --app build\my_weather
```

Then read the final `developerAdvice[]` section in the JSON report. It is the
app-author view of the lower-level diagnostics. Package reports also include
`animationDiagnostics`, which catches common costly or unsupported CSS animation
patterns before the runtime parser evaluates the stylesheet. Some advice entries include a
`recipe` field that points at a copyable pattern in
[app_author_recipes.md](app_author_recipes.md).

Advice created for a responsive target also includes `targetViewport` and, when
the target is gated, `targetGate.decision` / `targetGate.reasons`. Font advice
includes `font.missingNonAsciiSample`, `font.targetFontProfile` or unmatched
CSS families when those details are available. Editors and report viewers can
show the affected target and exact font decision without re-parsing diagnostics.
Every advice entry also carries `diagnosticContext`: the emitting stage, a
best-effort field/property/element subject when the diagnostic provides one,
and a bounded source excerpt. This is especially useful for a newly introduced
or otherwise unclassified degradation: the report remains actionable before a
more specific advice template exists. High display-command density additionally
reports the command count, viewport pixel area and density heuristic rather
than guessing which DOM node caused it.

If the page feels slow, inspect `performanceSummary.bottlenecks[]` and
`performanceAdvice[]` next. These fields quantify preflight complexity:
DOM/render/layout object counts, layer and display-command counts, framebuffer
bytes, estimated pipeline heap, resource budget use and full-frame present
scale. When the pseudo browser ran during `check`, the same summary also
includes desktop tool-side stage timings such as parse, layout, paint and
present microseconds. These timings help answer "where did the time go?", but
they are still not device FPS. Use Win32 frame-script capture or device
telemetry for actual frame time, DMA wait and panel flush time. Add
`--runtime-log path\to\capture.log` to `check` / `package` for saved Win32 logs,
or `--port-telemetry path\to\port.log` for real port logs. A port log can start
as one text line:

```text
port_telemetry case=scroll_benchmark_cumulative workload=full frames=60 frame_ms_avg=24.5 frame_ms_max=38.0 dma_wait_ms_avg=2.1 flush_done_ms_avg=7.4 internal_ram_peak=180000
```

The CLI merges these into `runtimeMetrics` / `portTelemetry`,
`performanceSummary.bottlenecks[]` and `performanceAdvice[]`, which helps
separate page complexity, dirty area, panel flush, DMA wait and port-side
internal-RAM pressure. When comparing controlled device runs, include stable
`case=` and `workload=` labels; the CLI preserves them in `portTelemetry.summary`
and surfaces them as measured-port metadata rather than mixing incomparable
captures.

For a port that reports an opt-in panel-scroll path, preserve
`panel_scroll_mode`, `panel_scroll_steps`, `panel_scroll_fallbacks`,
`panel_scroll_wraps` and `panel_scroll_cpu_blits_elided` with frame/present
p95. The report then distinguishes a slow panel transfer from CPU-side scroll
work that remains after the panel path is fast; do not treat a zero-fallback
result as proof of physical ring-wrap recovery without the wrap evidence.

For a repository-wide trial pass, `doctor` accepts the same measurements as
explicit sample mappings. This prevents an unrelated log from being attached to
the wrong package:

```powershell
python tools\jellyframe_cli.py doctor `
  --runtime-log jelly_motion_lab=build\motion.capture.log `
  --port-telemetry jelly_scroll_container=build\scroll.port.log
```

The summary prints `measured=` so reviewers can distinguish static preflight
from a report that includes Win32 or device evidence.

Diagnostic titles and explanations try to reuse Web/CSS vocabulary when it
matches the failure: parse error, invalid declaration, unsupported value,
overflow, clipping, deferred API and similar terms. JellyFrame-specific `code`
values remain stable machine-readable identifiers for tools.

## What You Can Rely On

Stable authoring pieces today:

- Local package HTML, CSS and classic JavaScript.
- Simple DOM structure, IDs/classes, text, forms and buttons.
- Block/inline layout, simplified flex, a practical grid-card subset, absolute
  positioning for small decorations and fixed bottom bars.
- `box-sizing: border-box`, percentage sizing, `max-width: 100%`, `gap`,
  `aspect-ratio`, single-value percentage `border-radius`, lightweight
  gradients, rounded rectangles, approximate shadows and Canvas 2D V0.4 when
  declared.
- DOM events, click/pointer/touch aliases, focus, basic form state, timers and
  bounded `requestAnimationFrame`.
- Optional XHR GET, tiny `localStorage`, Audio V0 and geolocation V0 when both
  manifest and host target allow them.
- Package-local BMP images in the Win32 shell, plus a host image codec adapter
  boundary for product PNG/JPEG/WebP decoders.
- App `.jffont` supplements and default font subset preflight.

Check the full matrix before relying on details:
[developer_capability_matrix.md](developer_capability_matrix.md).
For a quick "can I use it?" table, start with
[app_author_capability_table.md](app_author_capability_table.md).
For copyable small-screen component patterns, read
[app_author_recipes.md](app_author_recipes.md).

## What Not To Assume

Do not assume browser behavior for:

- Remote HTML/CSS/script/image loading.
- Browser-wide `fetch`, cookies, IndexedDB, Cache API or general filesystem.
- Full CSS Grid/Flexbox, selector engine, pseudo-elements, filters, SVG, full
  Canvas or video.
- Browser font matching, `@font-face`, vector shaping, italic/style/stretch
  matching or automatic global font fallback.
- Hardware services just because an API name appears in JavaScript.
- Page-level natural scrolling like a phone browser.

JellyFrame apps are local, bounded, target-aware UI packages.

## Manifest Rules

The manifest is part of the app, not boilerplate.

- Declare services in `capabilities`: `network.fetch`, `storage.kv`,
  `media.audio.playback`, `location.position`, `graphics.canvas2d`, and documented
  sensor names.
- A declaration is a request, not a guarantee. The selected target profile and
  product host must also provide the service.
- Use `targets[id].gate` for release decisions. Use `reject` for devices the app
  claims to support, and `warn` for experimental or optional targets.
- Keep budgets realistic. Raising budgets without measuring just moves the
  failure to the device.
- If CSS uses a custom `font-family`, add a matching `.jffont` manifest entry or
  use `system-ui`/`sans-serif`.

## Small-Screen Layout Recipes

Good defaults for wearable apps:

- Design the first screen for `300x300` round, then verify `320x240` and
  `172x320`.
- Use `box-sizing: border-box` globally.
- Use `max-width: 100%` on cards, rows, images and canvas elements.
- Prefer vertical stacks on narrow targets.
- Keep labels short. Use `Hourly`, `Daily`, `Air`; avoid long tab text.
- Put long content inside one explicit `overflow-y: auto` container.
- Keep fixed bottom navigation outside the scroll container.
- Avoid many side-by-side buttons on `172x320`.
- Use `@media (max-width: ...)` and `@media (max-height: ...)` to reduce padding,
  card count, columns and font size.
- Use `gap`, not margin piles, for repeated cards.

Minimal shell:

```css
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  width: 100%;
  height: 100%;
  font-family: system-ui, sans-serif;
}

.screen {
  width: 100%;
  height: 100%;
  padding: 14px;
}

.stack {
  display: grid;
  grid-template-columns: 1fr;
  gap: 10px;
}

.card {
  max-width: 100%;
  border-radius: 18px;
  padding: 12px;
}

@media (max-width: 200px) {
  .screen {
    padding: 8px;
  }

  .card {
    padding: 9px;
  }
}
```

For fuller button, card, scroll-list and fixed bottom-nav recipes, see
[app_author_recipes.md](app_author_recipes.md) and the live package at
`samples/apps/packages/jelly_component_recipes`.

## Common Warnings And Fixes

| Diagnostic | What it usually means | First fix |
| --- | --- | --- |
| `layout-text-overflow` | Text does not fit its box. Reports usually include `text`, `node`, `path`, measured width and available width. | Shorten the label, widen the box, reduce font size, or use a narrow-target media rule. |
| `visual-horizontal-overflow` | Paint extends outside the target width. Reports include `paintBounds`, viewport and overflow pixels; when a layout box can be blamed, `node`, `path` and `boxOverflow*` fields point to it. | Add `max-width: 100%`, use `box-sizing: border-box`, stack columns, or put long content in a scroll container. |
| `visual-vertical-paint-overflow` | Paint extends above or below the target height. Reports can include `node`, `path`, `boxTop`, `boxBottom`, `boxOverflowTop` and `boxOverflowBottom`. | Move fixed/absolute elements back inside the viewport, reduce vertical spacing, or turn long content into an explicit scroll container. |
| `visual-scroll-needed` | Page is taller than the viewport. | Decide whether scrolling is intended. If yes, use an explicit `overflow-y: auto` area and allow it in the target gate. |
| `visual-scroll-container` | An internal scroll area clips content. Reports usually include `node`, `path`, `boxHeight`, `contentHeight` and `overflowY`. | Verify the reported container is reachable by touch/wheel/key input and keep fixed navigation outside it. |
| `visual-nested-scroll-container` | An inner and outer scroll area both clip vertical content. | Keep one primary vertical scroll area per route. Move the inner content into the outer list, or make the inner region fixed-height without overflow. |
| `font-family-unmatched` | CSS names a custom font not declared in manifest. | Use `system-ui` or declare a matching `.jffont` family. |
| `font-missing-glyphs` | Target fonts do not cover all text. | Use the generated `*.used_chars.txt` to build and declare an app font supplement. |
| `style-property-unsupported` | CSS property is not in the subset. | Replace it with a documented property or use Canvas/resource art. |
| `html-node-limit` / `html-depth-limit` / `html-attribute-limit` | Parsed markup exceeded a bounded DOM budget, so later nodes, descendants or attributes were dropped. | Flatten wrapper-heavy markup, virtualize long lists, and keep state in a small number of attributes. Raise a DOM budget only with device-memory evidence. |
| `css-rule-limit` / `css-declaration-limit` | The CSS parser skipped later rules, selector items, keyframes or declarations. | Merge repeated rules, remove unused variants, split oversized rules and measure style cost before raising parser budgets. |
| `layout-box-limit` / `layout-depth-limit` / `render-object-limit` | Part of the visible hierarchy was skipped before layout or rendering. | Reduce visible nesting and repeated controls; virtualize lists rather than raising structural budgets blindly. |
| `layer-limit` / `display-command-limit` | Layer or paint-command generation was capped, which can reduce fidelity or omit paint. | Reduce overlap, clips, shadows, gradients and repeated decoration; keep composited motion to a small number of elements. |
| `resource-budget-exceeded` / `font-budget-exceeded` | Total package resources or runtime font supplements exceed their configured budget. | Compress/remove assets, subset `.jffont` glyphs and unused weights, then raise limits only after flash/RAM measurement. |
| `css-animation-layout-property` | A keyframe changes size, position, spacing or another layout property. | Animate `transform` or `opacity` on a fixed-size layer; make actual layout changes discrete app states. |
| `css-animation-timing-function-unsupported` | A stylesheet uses parameterized or step easing. | Use `linear`, `ease`, `ease-in`, `ease-out` or `ease-in-out`. |
| `css-animation-keyframe-property-unsupported` | A keyframe targets a property outside the low-cost animation subset. | Limit keyframes to `opacity`, `transform`, `color`, `background` or `background-color`. |
| `script-capability-missing` | JavaScript uses a host-backed API, but manifest does not request the matching capability. | Declare the reported capability and keep a visible fallback for hosts that deny it. |
| `script-api-deferred` | JavaScript uses a browser API outside the current runtime subset, such as `fetch`, `WebSocket`, `DataTransfer`, workers or dynamic import. | Use the documented V0 substitute, usually XHR GET or a host-owned service, or remove the browser-only path. |
| `script-api-subset` | JavaScript uses a supported API outside JellyFrame's subset, commonly complex `querySelector`. | Keep selectors simple or use explicit IDs/classes and stored element references. |
| `html-element-unsupported` | Markup uses a browser/platform element such as `iframe`, `object`, `embed`, `slot` or image-map tags. | Replace it with package-local images, explicit controls, Canvas, or a host-owned service. |
| `html-form-submit-deferred` | A `<form>` asks for browser action/method submission. | Handle the control in app JavaScript and request the required host capability, such as `network.fetch`, when needed. |
| `target-gate-not-accepted` | A target claimed by the app failed its release gate. | Fix the listed overflow/scroll/diagnostic reasons, or lower the gate to `warn` while experimental. |

## When To Use Canvas

Use Canvas 2D V0.4 for custom gauges, rings, tiny charts and decorations that
would otherwise require many DOM nodes. Declare `graphics.canvas2d` and keep the
canvas small. Do not use Canvas to rebuild the whole UI unless the app is truly
graphics-first.

## Before Sharing An App

Run:

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\Release
```

Then verify:

- `developerAdvice[]` is empty or contains only accepted tradeoffs.
- Supported targets have `gate.decision: "accept"`.
- No unexpected text overflow on the Win32 shell.
- Font report covers every visible character.
- Any host service used by JavaScript is declared in manifest and supported by
  the target profile.
