# App Author Capability Table

> Last updated: 2026-07-22; Applies to: 0.5.0-dev

This is the quick "can I use it?" table for app authors. The full contract
remains [developer_capability_matrix.md](developer_capability_matrix.md).

## HTML

| Feature | Status | Guidance |
| --- | --- | --- |
| Basic structure | Available | Use `div`, `section`, `header`, `main`, `footer`, `ul`, `li`, `p` and `span`. Unknown tags become ordinary elements. |
| Text | Available | UTF-8 text is preserved; real CJK glyph quality depends on system fonts or app `.jffont` supplements. |
| Form controls | Form V0 subset | Use `button`, `input`, `select`, `textarea`, `progress` and `meter`. `required`, text `minlength`/`maxlength`, checkbox/radio required state and required selects work with local `submit`; browser validation popups and browser navigation do not. |
| Confirmation dialog | Bounded modal subset | Use one `<dialog>` with `showModal()` and `close(returnValue)`. In the Win32 shell, Escape becomes a cancellable `cancel` and then `close`; focus and hit testing stay inside the dialog while it is open. No nested dialogs, backdrop/light-dismiss, `show()`, `requestClose()` or browser top-layer behavior. |
| Images | Subset | Use package-local `<img src="/assets/icon.bmp">` or one CSS `background-image: url("/assets/image.bmp")`. `border-radius` clips either image form with antialiased corners. CSS backgrounds fill the element and retain `background-color` as fallback; no remote/data URL, repeat, position or size syntax. Win32 validates BMP; PNG/JPEG/WebP need a target/host codec adapter. |
| Browser image/media markup | Deferred | Do not rely on `picture`/`source`/`srcset`, `<video>`, `<track>` or `<audio>` markup. Select one package-local image in app state; use `Audio()` V0 or the host frame-provider contract only when their capabilities are declared. |
| Tables, ruby and templates | Deferred | Browser table measurement, ruby/bidi layout and detached `template.content` semantics are absent. Use documented flex/grid subsets, localized plain text and explicit DOM creation. |
| Rich-text editing | Deferred | `contenteditable`, Selection and Range are absent. Use bounded `input`/`textarea`, or a product-owned system editor. |
| Canvas | Optional | Requires `graphics.canvas2d` in manifest and target support. Use for small charts/gauges, not the whole UI. |
| Remote page resources | Unsupported | Do not load remote HTML/CSS/JS/image/font. Runtime data uses host-backed XHR. |

## CSS

For exhaustive CSSWG lookup before writing styles, use
[csswg_support_table.md](csswg_support_table.md). It uses the same status
labels as the HTML support table; `partial` means the documented property/value
subset, not complete browser behavior.

| Feature | Status | Guidance |
| --- | --- | --- |
| Color/background | Available subset | Use hex, named colors, `rgb()` / `rgba()` and bounded sRGB `hsl()` / `hsla()`. For a package image background, use `background-color` plus `background-image: url("/assets/image.bmp")`; package reports identify invalid or missing paths. |
| Layout | Subset | Prefer block, simplified flex (including signed integer `order` for direct flex children), and bounded grid-card layouts. Grid supports 2-4 fixed/`1fr` rows plus positive numeric `grid-column`/`grid-row` start/end/span placement; it is not full Grid. Use fixed bottom bars and explicit scroll containers. Use `visibility: hidden` when a layout slot must remain; use `display: none` when it should collapse. |
| Responsive sizing | Subset | Use `@media`, percentage sizing, LTR `inline-size` / `block-size`, `max-width: 100%`, `box-sizing: border-box`, `gap` and `aspect-ratio`. Use `overflow-y: auto` for the documented fixed-height vertical scroll-container subset. |
| Radius/shadow/gradient | Subset | Rounded rectangles, percentage radius, lightweight shadow, non-layout `outline-offset`, linear gradients, two-stop `conic-gradient()` progress rings and two-color center-circle `radial-gradient()` highlights are supported. Complex blur/mask/filter are deferred. |
| Text layout and overflow | Bounded subset | Use `letter-spacing` only for short labels/numbers and `overflow-wrap: anywhere` to break an otherwise unbreakable UTF-8 label at scalar boundaries. `text-wrap: wrap` and `text-wrap: nowrap` are aliases for `white-space: normal` and `white-space: nowrap`; use `white-space: nowrap; text-overflow: ellipsis` for a UTF-8-safe prefix followed by `...` when text overflows. No hyphenation, balanced wrapping or complex-script shaping. |
| Animation | Subset | Keyframes support opacity, color, background/background-color and documented transform forms. Timing supports linear/ease/ease-in/ease-out/ease-in-out; package checks flag layout animation and unsupported easing before runtime. |
| CSS nesting | Explicit single-level subset | Use explicit `&` only, such as `.card { &:hover { ... } & .label { ... } }`. Do not use implicit nesting, nested at-rules or nesting deeper than one level. |
| Complex browser CSS | Unsupported/deferred | Do not rely on full grid/flex, container queries, `:has()`, full pseudo-elements or filter/backdrop-filter. |

## JavaScript

| Feature | Status | Manifest |
| --- | --- | --- |
| DOM mutation | Subset | No extra capability. Use `document.head`, `document.body`, `document.readyState`, `document.defaultView`, `document.hasFocus()`, `getElementById`, simple-selector `querySelector`, `createElement`, `appendChild`, `append`, `prepend`, `textContent`, lightweight `innerText`, `id`, `className`, common form-control IDL properties and the small `classList` helper. |
| Element geometry | Frame-snapshot subset | `element.getBoundingClientRect()` returns a read-only numeric client-relative rectangle from the last completed host layout frame. It does not force layout, hold a live DOMRect, or include transform/nested-scroll geometry. |
| Events | Available | Use `addEventListener`, documented `on*` handler properties, `element.click()`, event delegation, the bounded read/write `dataset` subset and `matches`/`closest`. Inline HTML event attributes are not supported. |
| Local forms / `FormData` | Form V0 subset | Use `form.checkValidity()`, `reportValidity()`, `requestSubmit([submitter])`, a cancellable `submit` event with `event.submitter`, and `new FormData(form)`. Send data only through an allowed host service from the event handler. |
| `HTMLDialogElement` | Bounded modal subset | `dialog.open`, `returnValue`, `showModal()` and `close([returnValue])` are available in scripting builds. Use `cancel`/`close` listeners; only one modal is active per document and host back/Escape policy may request cancellation. |
| Timers / rAF | Bounded | No extra capability, but frame policy and budgets apply. |
| `XMLHttpRequest` GET | Host optional | `network.fetch`. Runtime data only; not a page/resource loader. |
| `localStorage` | Host optional | `storage.kv`. App-private tiny KV shadow, not full browser storage. |
| Browser sessions and messaging | Deferred | `sessionStorage`, cookies, storage events and MessageChannel/MessagePort are unavailable. Keep state app-local or use an explicit host service. |
| `Audio()` | Host optional | `media.audio.playback`. Real codec/I2S belongs to the host. |
| `navigator.geolocation` | Host optional | `location.position`. Discrete position snapshots only. |
| Canvas 2D | Host optional subset | `graphics.canvas2d`. Backing storage is allocated only after `getContext("2d")`. Includes canvas-to-canvas `drawImage()` in 3/5/9-argument forms with nearest-neighbor scaling, two-stop concentric `createRadialGradient()`, pixel-aligned `translate(x, y)` and `resetTransform()` retained by `save()`/`restore()`, plus bounded `quadraticCurveTo()` and `bezierCurveTo()` path tessellation. `<img>`, `ImageBitmap`, video sources, focal/off-center or multi-stop radial gradients, scale, rotate, generic matrices and pixel APIs remain deferred. |
| Host time | Available | Use `Date.now()`. Do not assume `new Date()` is host-clock controlled unless documented later. |
| Host compute jobs | Host optional contract | `compute.jobs` reserves bounded named host work with byte budgets. It is not a JS Worker, thread, message port or arbitrary-code API yet. |
| Video frame preview | Host optional experimental contract | `media.video.frame` supplies bounded latest-frame handles for product-owned MJPEG or explicitly enabled H.264 baseline preview. It is not `<video>` or a JS media API. |
| Battery/weather/activity summary | Host optional | Declare `system.battery`, `system.weather` or `system.activity`, then read the approved low-frequency snapshot with `navigator.jellyframe.getSnapshot()`. No polling, subscription or raw device handles are exposed. |
| App-local routes | Bounded | `location.hash`, `hashchange`, `popstate`, `onhashchange` and `onpopstate` switch state inside the current app only. Bounded `history.length`, `back()`, `forward()`, `go()`, `pushState()` and `replaceState()` retain fragment-only entries; URL loading, browser history state, navigation and cross-app routes are absent. |
| Static local modules | Package-time subset | One external `type="module"` entry and package-local static `.js` imports are bundled to classic script at package time. Dynamic `import()`, remote modules and `modulepreload` are deferred. |
| Promise/fetch/innerHTML | Deferred | Do not rely on them yet. |
| querySelector/querySelectorAll | Subset | Simple tag, `.class`, `#id`, `[attr]`, `[attr=value]` and same-compound combinations only; complex selectors are diagnosed. |

## Resources And Fonts

| Feature | Status | Guidance |
| --- | --- | --- |
| Package-local resources | Available | Use `/assets/a.bmp` or relative paths. Schemes, `//host` and app-root escapes are rejected. |
| `.jfapp` bundle | V0 available | Generate with CLI `package --output-bundle`. |
| Character scan | Default | `check`/`package`/`preview`/`install` emit `*.used_chars.txt` and a `fontSubset` plan by default. |
| `.jffont` | Available | Can be generated from a licensed BDF; still declare it explicitly in manifest `fonts[]`. |
| Font antialiasing | Optional | `.jffont` V1 supports 2bpp/4bpp coverage at higher byte and paint cost. |

## Minimum Release Check

```powershell
python tools\jellyframe_cli.py check `
  --root your_app `
  --target round-300 `
  --targets round-300,rect-320x240,rect-172x320 `
  --report build\your_app.report.json `
  --build-dir build\Release
```

Read `developerAdvice[]` first. It points to missing manifest capabilities,
target-specific overflow and resource/font issues.
