# App Component Recipes

> Last updated: 2026-08-15; Applies to: 0.6.0-dev; active development line: 0.6.0

These recipes are copyable starting points for small wearable apps. They use the
documented JellyFrame subset and avoid browser-only behavior.

Use the live sample at `samples/apps/packages/jelly_component_recipes` as the
reference package.

## Page Shell

Keep the page height fixed to the target viewport and put scrollable content in
one explicit child.

```html
<main class="screen">
  <header class="topbar">...</header>
  <section class="content-scroll">...</section>
  <nav class="bottom-nav">...</nav>
</main>
```

```css
* {
  box-sizing: border-box;
}

.screen {
  width: 100%;
  height: 300px;
  padding: 12px;
  overflow: hidden;
}

.content-scroll {
  height: 196px;
  overflow-y: auto;
}
```

## Reserved Slots

Use `visibility: hidden` for an optional badge or action whose space must stay
reserved. Use `display: none` only when following content should close the gap.
A child can explicitly use `visibility: visible` when it must remain visible
inside a hidden wrapper. `collapse` is not supported.

```css
.sync-slot { height: 26px; visibility: hidden; }
.sync-slot.is-ready { visibility: visible; }
```

## Package Image Background

Use a local BMP as a decorative or full-card background without adding a second
`img` node. Keep a solid fallback color because the host can decline decoding
when the target has no matching codec or image budget.

```css
.weather-card {
  width: 100%;
  height: 92px;
  background-color: #12314a;
  background-image: url("/assets/weather-card.bmp");
  background-size: cover;
  background-position: center;
  background-repeat: no-repeat;
  border-radius: 14px;
}
```

This path accepts one package-absolute image. `background-size` is limited to
`cover`, `contain` or `100% 100%`; `background-position` uses the documented
simple image-position subset; only `background-repeat: no-repeat` is accepted.
It intentionally does not accept remote/data URLs, relative paths, tiling,
multiple backgrounds or arbitrary size expressions.

## Static SVG Icons

Keep a simple icon source in the package, reference it from HTML or CSS, then
compile the package with `--rasterize-svg`. The output bundle/debug package has
only a generated BMP and rewritten references, so the target has no SVG parser
or vector allocation cost.

```html
<img class="status-icon" src="assets/status.svg" alt="Ready">
```

```powershell
python tools\jellyframe_cli.py package `
  --root your_app `
  --report build\your_app.report.json `
  --output-bundle build\your_app.jfapp `
  --rasterize-svg --svg-raster-size 32
```

This is for static 16/24/32px-style icons, not SVG UI. Use static HTML/CSS
references only. The converter accepts basic shapes and simple paths, but
rejects SVG text, filters, gradients, transforms, scripts, remote data and arc
path commands. Inspect `staticSvgRasterization` in the package report; replace
an unsupported source with a pre-rasterized bitmap when conversion is rejected.

## Buttons

Prefer short text. Use one or two columns. On narrow targets, keep only the
primary actions visible or use very short labels.

```css
.button-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 7px;
}

.button {
  width: 100%;
  height: 32px;
  border-radius: 16px;
  text-align: center;
}

/* For narrow targets, prefer fewer buttons or shorter labels. */
```

## Fixed Settings Grid

Use explicit rows only when a settings panel needs a stable action area. The
bounded subset accepts two to four fixed/`1fr` rows and positive numeric
placement, not named areas or full browser Grid.

```css
.settings-grid {
  display: grid;
  height: 156px;
  grid-template-columns: 56px 1fr;
  grid-template-rows: 30px 1fr 34px;
  gap: 7px;
}

.settings-title { grid-column: 1 / 3; grid-row: 1; }
.settings-label { grid-column: 1; grid-row: 2; }
.settings-value { grid-column: 2; grid-row: 2; }
.settings-save { grid-column: 1 / 3; grid-row: 3; }
```

## Compact Labels

Use letter spacing for short labels and scalar-safe wrapping for data that may
arrive without spaces. Do not use either as a substitute for a shaping-capable
font backend.

```css
.eyebrow { letter-spacing: 1px; }
.device-name { overflow-wrap: anywhere; }
```

## Cards

Cards should frame repeated items or controls. Avoid cards inside cards.

```css
.card {
  max-width: 100%;
  padding: 11px;
  border: 1px solid rgba(144, 237, 236, 0.64);
  border-radius: 18px;
  overflow: hidden;
}
```

## Gel Surface

Use one base gradient and one translucent radial highlight for depth. This is a
two-layer background, so reserve it for prominent cards rather than every list
row. The shadow has a bounded raster cost and is only emitted when declared.

```css
.gel-card {
  background:
    radial-gradient(circle at 80% 12%, rgba(241, 253, 255, 0.22) 0%, transparent 100%),
    linear-gradient(135deg, #315a7a, #142331);
  border: 1px solid color-mix(in srgb, #b7edff 18%, #315a7a);
  border-radius: 16px;
  box-shadow: 0 6px 10px 1px color-mix(in srgb, rgba(0, 0, 0, 0.42) 76%, rgba(98, 223, 247, 0.26));
}
```

## Status Page

Use a centered status card for empty, offline or error states. Keep the title
short and expose one primary action.

```html
<article class="status-card">
  <span class="status-icon">!</span>
  <h2>Offline</h2>
  <p>Keep failure states short.</p>
  <button class="button primary">Retry</button>
</article>
```

```css
.status-card {
  min-height: 118px;
  text-align: center;
}

.status-icon {
  width: 34px;
  height: 34px;
  margin: 0 auto 7px;
  border-radius: 50%;
}
```

## Control Panel

Control panels should use short labels and fixed-height rows. Do not put many
large controls side by side on narrow screens.

```html
<div class="control-row">
  <span>Bright</span>
  <div class="mini-meter"><span></span></div>
</div>
```

```css
.control-row {
  display: grid;
  grid-template-columns: 58px 1fr;
  gap: 8px;
  align-items: center;
  height: 30px;
}
```

## Scroll List

Use a fixed-height scroll area when content can exceed the viewport. Keep fixed
navigation outside that area.

```css
.content-scroll {
  height: 196px;
  overflow-y: auto;
}

.bottom-nav {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 7px;
  height: 34px;
}
```

In the manifest target gate, set `allowScroll: true` only when scrolling is part
of the design.

Keep one primary vertical scroll area per route. If an inner card also needs to
move, put its content in the outer list or redesign it as a fixed-height
control. Nested overflowing scroll areas compete for touch drag input and are
reported by `check` as `visual-nested-scroll-container` with both DOM paths.

## Local Form Flow

Use a local `submit` event for settings, pairing and account-like flows. Keep
the form state in the page, validate it locally, then call an allowed host
service from JavaScript. Do not set `action` or `method`: JellyFrame does not
navigate, encode multipart payloads or perform browser form POSTs.

```html
<form id="wifi-form">
  <input name="network" required maxlength="32">
  <input name="password" required minlength="8" maxlength="63">
  <button type="submit">Connect</button>
</form>
```

```js
const form = document.getElementById("wifi-form");
form.addEventListener("submit", function (event) {
  event.preventDefault();
  const fields = new FormData(form);
  // Invoke the documented, capability-gated host service here.
  // Do not expect a page navigation or automatic network request.
});
```

`form.checkValidity()` and `form.reportValidity()` return a boolean and dispatch
non-bubbling `invalid` on each invalid control. There is no browser validation
popup. `FormData` supports string entries with `append`, `set`, `delete`, `get`,
`getAll`, `has` and `forEach`. `forEach` supplies `(value, name, formData)` in
entry order; entries appended from its callback are deliberately deferred to the
next call so one bounded app callback cannot grow the active iteration.

## Confirmation Dialog

Use one modal dialog for destructive or permission-gated actions. Open it from
the initiating control and close it explicitly from the dialog action. The host
may map Escape or a hardware Back action to `cancel`; call `preventDefault()`
only when the app must keep the confirmation visible.

```html
<button id="clear-data">Clear data</button>
<dialog id="confirm-clear">
  <p>Clear local data?</p>
  <div class="button-row">
    <button id="keep-data" type="button">Keep</button>
    <button id="confirm-data" type="button">Clear</button>
  </div>
</dialog>
```

```js
const dialog = document.getElementById("confirm-clear");
document.getElementById("clear-data").addEventListener("click", function () {
  dialog.showModal();
});
document.getElementById("keep-data").addEventListener("click", function () {
  dialog.close("keep");
});
document.getElementById("confirm-data").addEventListener("click", function () {
  dialog.close("clear");
});
dialog.addEventListener("close", function () {
  if (dialog.returnValue === "clear") {
    // Start the documented host-owned clear-data operation.
  }
});
```

Only one `showModal()` dialog may be active in a document. There is no nested
modal, click-outside close, browser backdrop, `show()` or `requestClose()`.

## Static Local Modules

For a larger app, keep the device runtime on the documented classic-script path
while using one static local module entry in source. Packaging replaces the
module tag with one generated classic bundle.

```html
<script type="module" src="scripts/app.js"></script>
```

```js
// scripts/app.js
import { formatMinutes } from "./time.js";
document.getElementById("value").textContent = formatMinutes(42);

// scripts/time.js
export function formatMinutes(value) {
  return value + "m";
}
```

Use package-local `.js` files only. Keep one module entry and an acyclic static
graph. `dynamic import()`, remote modules, `modulepreload`, `export *`,
re-export declarations and inline module scripts are rejected or deferred. Keep
`runtime.script` as `"classic"`: it describes the generated device payload, not
the source authoring form.

## App-Local Routes

Use a fragment route for a small settings flow or tab set inside one app. It
updates no host URL and does not create browser navigation history:

```js
function renderRoute() {
  var route = location.hash || "#home";
  document.getElementById("page").textContent = route.slice(1);
}

window.addEventListener("hashchange", renderRoute);
location.hash = "settings";
renderRoute();
```

The supported surface is `location.hash`, `hashchange`, `popstate`,
`onhashchange`, `onpopstate` and fragment-only `history`. `history.back()`,
`forward()`, `go(delta)`, `pushState()` and `replaceState()` retain a bounded
stack of `#fragment` entries; `state` and `title` are not retained.
`location.assign()`, remote navigation and cross-app routing are unavailable.
See `jelly_route_tabs` for a complete package.

## Narrow Targets

For `172x320`, reduce horizontal decisions first:

- Prefer vertical stacks.
- Keep metric values short.
- Reduce padding before reducing readable text.
- Avoid three or more wide buttons in one row.

## Round-First Screen

For `round-300`, reserve the edge area for background and low-priority content.
Put the most important card or state in the visual center, keep bottom nav
compact, and validate the same package on `rect-320x240` and `rect-172x320`.

## Validation

Run this before sharing a package:

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\desktop-release\Release
```

Then inspect `developerAdvice[]` and each target gate decision.
