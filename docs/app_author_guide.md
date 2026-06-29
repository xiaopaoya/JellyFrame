# App Author Guide

This is the short contract for people writing JellyFrame apps. JellyFrame is
not a mini browser. It is a Web-shaped embedded UI runtime: HTML gives
structure, CSS gives a documented small-screen styling subset, JavaScript adds
bounded local interaction, and the manifest declares the target/device services
the app expects.

If you write it like an arbitrary web page, you will hit limits. If you write it
like a small wearable app with explicit targets and budgets, the toolchain can
help you ship predictable UI.

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
app-author view of the lower-level diagnostics.

## What You Can Rely On

Stable authoring pieces today:

- Local package HTML, CSS and classic JavaScript.
- Simple DOM structure, IDs/classes, text, forms and buttons.
- Block/inline layout, simplified flex, a practical grid-card subset, absolute
  positioning for small decorations and fixed bottom bars.
- `box-sizing: border-box`, percentage sizing, `max-width: 100%`, `gap`,
  `aspect-ratio`, single-value percentage `border-radius`, lightweight
  gradients, rounded rectangles, approximate shadows and Canvas 2D V0.3 when
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
  `media.audio.mp3`, `location.position`, `graphics.canvas2d`, and documented
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
- Put long content inside one explicit `overflow: auto` container.
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
  display: flex;
  flex-direction: column;
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

## Common Warnings And Fixes

| Diagnostic | What it usually means | First fix |
| --- | --- | --- |
| `layout-text-overflow` | Text does not fit its box. | Shorten the label, widen the box, reduce font size, or use a narrow-target media rule. |
| `visual-horizontal-overflow` | Paint extends outside the target width. | Add `max-width: 100%`, use `box-sizing: border-box`, stack columns, or put long content in a scroll container. |
| `visual-scroll-needed` | Page is taller than the viewport. | Decide whether scrolling is intended. If yes, use an explicit `overflow: auto` area and allow it in the target gate. |
| `font-family-unmatched` | CSS names a custom font not declared in manifest. | Use `system-ui` or declare a matching `.jffont` family. |
| `font-missing-glyphs` | Target fonts do not cover all text. | Use the generated `*.used_chars.txt` to build and declare an app font supplement. |
| `style-property-unsupported` | CSS property is not in the subset. | Replace it with a documented property or use Canvas/resource art. |
| `target-gate-not-accepted` | A target claimed by the app failed its release gate. | Fix the listed overflow/scroll/diagnostic reasons, or lower the gate to `warn` while experimental. |

## When To Use Canvas

Use Canvas 2D V0.3 for custom gauges, rings, tiny charts and decorations that
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
