# Embedded App Subset

WearWeb is now usable for small embedded UI applications that are designed for
its bounded DOM/CSS/script subset. It is not yet a general web page runtime, but
the current core is enough to build useful local apps such as weather panels,
settings screens and calculators without forcing developers into a canvas-only
model.

## Practical Status After M6

Feasible now:

- Weather or dashboard apps with host-provided data, select controls, buttons
  and text updates.
- Calculator-style tools with buttons, input value state and synchronous event
  handlers.
- Settings panels using text inputs, textareas, checkboxes, radios, ranges and
  selects.
- Static or mostly-static information screens with modern but restrained visual
  styling.

- Clock apps can update automatically when the host pumps timers.
- Timer/stopwatch apps can run from `setInterval`, with the host deciding tick
  precision and callback budget.

Not suitable yet:

- Apps that assume browser loading, network, storage, canvas, Web Components,
  modules or selector-heavy frameworks.
- Large arbitrary modern sites that depend on full flex/grid, complex CSSOM,
  DOM ranges, layout observers or asynchronous browser task semantics.

## Supported Authoring Style

HTML:

- Use normal document structure: headings, sections, paragraphs, labels,
  buttons, inputs, textareas and selects.
- Prefer stable IDs for interactive nodes.
- Use local CSS files, embedded `<style>` or shell-provided linked stylesheet
  loading.
- Treat unsupported media or complex widgets as graceful fallbacks.

CSS:

- Use simple selectors: tag, class, id, descendant, child and simple attribute
  selectors.
- Use block and inline-block layout, simplified inline flow, basic flex support,
  margins, padding, borders, colors, font size, line height and text alignment.
- Use modern colors, spacing and hierarchy, but avoid layouts that require full
  browser grid/flexbox behavior.
- Keep control styling simple. The engine paints lightweight native-like
  control affordances and preserves usability when fancy effects are dropped.

JavaScript:

- Use classic scripts loaded explicitly by the host shell for now.
- Use `document.getElementById` instead of selectors.
- Supported DOM operations: `createElement`, `createTextNode`, `appendChild`,
  `removeChild`, `setAttribute`, `getAttribute` and `textContent`.
- Supported events: `addEventListener`, `removeEventListener`, capture/target/
  bubble dispatch, `click`, `input`, `change`, mouse and wheel fields.
- Supported form properties: `value`, `checked` and `selectedIndex` on relevant
  controls.
- Keep application logic mostly synchronous. Timers are available, but promise
  job pumping and browser loading tasks are still outside the subset.

## Developer Cost

The learning cost is moderate and acceptable for embedded app authors if the
subset is documented clearly. The model still feels like small DOM programming:
HTML defines structure, CSS styles it and JavaScript mutates named nodes.

The main difference from ordinary browser development is that authors must avoid
implicit browser services:

- no automatic `<script>` loading yet;
- no selector APIs beyond `getElementById`;
- no network, storage or module loader;
- no framework assumptions about a complete DOM.

This is a real constraint, but it is much smaller than asking application
authors to draw all UI manually on a canvas.

## Example Apps

The `examples/app_cases` directory contains four acceptance-style apps:

- `weather.*`: select-driven weather panel with unit toggle.
- `clock.*`: clock display refreshed through M6 `setInterval`.
- `timer.*`: stateful timer UI using M6 `setInterval`.
- `calculator.*`: button-driven calculator using `input.value` and event
  listeners.

Run one through the scripting pseudo browser:

```powershell
.\build-script\Release\wearweb_pseudo_browser.exe examples\app_cases\weather.html examples\app_cases\weather.css weather.bmp 360 360 --script examples\app_cases\weather.js
.\build-script\Release\wearweb_pseudo_browser.exe examples\app_cases\clock.html examples\app_cases\clock.css clock.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
```

Run one interactively through the Win32 shell:

```powershell
.\build-script\Release\wearweb_win32_browser.exe examples\app_cases\calculator.html examples\app_cases\calculator.css --script examples\app_cases\calculator.js
```

## M7 Readiness Decision

The core is ready to enter M7. The remaining high-impact app gap is not timers
anymore; it is script loading ergonomics. M7 should support inline classic
`<script>` plus a shell-provided local script loader callback while keeping
network loading and modules outside the embedded core.
