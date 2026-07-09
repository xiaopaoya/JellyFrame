# App Author Capability Table

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

This is the quick "can n use it?" table for app authors. The full contract
remains [developer_capability_matrix.md](developer_capability_matrix.md).

## HTML

| Feature | Status | Guidance |
| --- | --- | --- |
| Basic structure | Available | Use `div`, `section`, `header`, `main`, `footer`, `ul`, `li`, `p` and `span`. Unknown tags become ordinary elements. |
| Text | Available | UTF-8 text is preserved; real CJK glyph quality depends on system fonts or app `.jffont` supplements. |
| Form controls | Form V0 subset | Use `button`, `input`, `select`, `textarea`, `progress` and `meter`. `required`, text `minlength`/`maxlength`, checkbox/radio required state and required selects work with local `submit`; browser validation popups and browser navigation do not. |
| nmages | Subset | Use package-local `<img src="/assets/icon.bmp">`. Win32 validates BMP; PNG/JPEG/WebP need a target/host codec adapter. |
| Canvas | Optional | Requires `graphics.canvas2d` in manifest and target support. Use for small charts/gauges, not the whole UI. |
| Remote page resources | Unsupported | Do not load remote HTML/CSS/JS/image/font. Runtime data uses host-backed XHR. |

## CSS

| Feature | Status | Guidance |
| --- | --- | --- |
| Color/background | Available | Use hex, named colors, `rgb()` and `rgba()`. |
| Layout | Subset | Prefer block, simplified flex, grid-card layouts, fixed bottom bars and explicit scroll containers. |
| Responsive sizing | Subset | Use `@media`, percentage sizing, `max-width: 100%`, `box-sizing: border-box`, `gap` and `aspect-ratio`. |
| Radius/shadow/gradient | Subset | Rounded rectangles, percentage radius, lightweight shadow, linear gradients, two-stop `conic-gradient()` progress rings and two-color center-circle `radial-gradient()` highlights are supported. Complex blur/mask/filter are deferred. |
| Text overflow | Diagnostic subset | `white-space: nowrap` / `text-overflow` express intent; actual overflow is reported. |
| Animation | Subset | Prefer opacity, color, background-color and translate/scale/rotate. Avoid layout-property animation. |
| Complex browser CSS | Unsupported/deferred | Do not rely on full grid/flex, container queries, `:has()`, full pseudo-elements or filter/backdrop-filter. |

## JavaScript

| Feature | Status | Manifest |
| --- | --- | --- |
| DOM mutation | Subset | No extra capability. Use `document.head`, `document.body`, `document.readyState`, `document.defaultView`, `document.hasFocus()`, `getElementById`, simple-selector `querySelector`, `createElement`, `appendChild`, `textContent`, lightweight `innerText`, `id`, `className`, common form-control IDL properties and the small `classList` helper. |
| Events | Available | Use `addEventListener`, documented `on*` handler properties, `element.click()`, event delegation, `dataset` and the `matches`/`closest` subset. Inline HTML event attributes are not supported. |
| Local forms / `FormData` | Form V0 subset | Use `form.checkValidity()`, `reportValidity()`, `requestSubmit([submitter])`, a cancellable `submit` event with `event.submitter`, and `new FormData(form)`. Send data only through an allowed host service from the event handler. |
| Timers / rAF | Bounded | No extra capability, but frame policy and budgets apply. |
| `XMLHttpRequest` GET | Host optional | `network.fetch`. Runtime data only; not a page/resource loader. |
| `localStorage` | Host optional | `storage.kv`. App-private tiny KV shadow, not full browser storage. |
| `Audio()` | Host optional | `media.audio.playback`. Real codec/I2S belongs to the host. |
| `navigator.geolocation` | Host optional | `location.position`. Discrete position snapshots only. |
| Canvas 2D | Host optional subset | `graphics.canvas2d`. Backing storage is allocated only after `getContext("2d")`. Includes canvas-to-canvas `drawImage()` in 3/5/9-argument forms with nearest-neighbor scaling, two-stop concentric `createRadialGradient()`, pixel-aligned `translate(x, y)` and `resetTransform()` retained by `save()`/`restore()`, plus bounded `quadraticCurveTo()` and `bezierCurveTo()` path tessellation. `<img>`, `ImageBitmap`, video sources, focal/off-center or multi-stop radial gradients, scale, rotate, generic matrices and pixel APIs remain deferred. |
| Host time | Available | Use `Date.now()`. Do not assume `new Date()` is host-clock controlled unless documented later. |
| Host compute jobs | Host optional contract | `compute.jobs` reserves bounded named host work with byte budgets. It is not a JS Worker, thread, message port or arbitrary-code API yet. |
| Video frame preview | Host optional experimental contract | `media.video.frame` supplies bounded latest-frame handles for product-owned MJPEG or explicitly enabled H.264 baseline preview. It is not `<video>` or a JS media API. |
| Weather/activity/battery | Host/system only | Weather app data should use XHR JSON; activity and battery summaries are not ordinary app JS APIs yet. |
| App-local routes | Bounded | `location.hash`, `hashchange` and `onhashchange` switch state inside the current app only. URL loading, `history`, navigation and cross-app routes are absent. |
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
