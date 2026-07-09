# App Component Recipes

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

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
  overflow: auto;
}
```

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
  overflow: auto;
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
`getAll` and `has`.

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
  --build-dir build\Release
```

Then inspect `developerAdvice[]` and each target gate decision.
