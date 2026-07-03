# App Author Capability Table

This is the quick "can I use it?" table for app authors. The full contract
remains [developer_capability_matrix.md](developer_capability_matrix.md).

## HTML

| Feature | Status | Guidance |
| --- | --- | --- |
| Basic structure | Available | Use `div`, `section`, `header`, `main`, `footer`, `ul`, `li`, `p` and `span`. Unknown tags become ordinary elements. |
| Text | Available | UTF-8 text is preserved; real CJK glyph quality depends on system fonts or app `.jffont` supplements. |
| Form controls | Subset | Use `button`, `input`, `select`, `textarea`, `progress` and `meter`. Complex browser-native UI is not guaranteed. |
| Images | Subset | Use package-local `<img src="/assets/icon.bmp">`. Win32 validates BMP; PNG/JPEG/WebP need a target/host codec adapter. |
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
| DOM mutation | Subset | No extra capability. Use `getElementById`, `createElement`, `appendChild`, `textContent` and `className`. |
| Events | Available | Use `addEventListener`, event delegation, `dataset` and the `matches`/`closest` subset. |
| Timers / rAF | Bounded | No extra capability, but frame policy and budgets apply. |
| `XMLHttpRequest` GET | Host optional | `network.fetch`. Runtime data only; not a page/resource loader. |
| `localStorage` | Host optional | `storage.kv`. App-private tiny KV shadow, not full browser storage. |
| `Audio()` | Host optional | `media.audio.playback`. Real codec/I2S belongs to the host. |
| `navigator.geolocation` | Host optional | `location.position`. Discrete position snapshots only. |
| Canvas 2D | Host optional | `graphics.canvas2d`. Backing storage is allocated only after `getContext("2d")`. |
| Host time | Available | Use `Date.now()`. Do not assume `new Date()` is host-clock controlled unless documented later. |
| Weather/activity/battery | Host/system only | Weather app data should use XHR JSON; activity and battery summaries are not ordinary app JS APIs yet. |
| Promise/fetch/modules/querySelector/innerHTML | Deferred | Do not rely on them yet. |

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
