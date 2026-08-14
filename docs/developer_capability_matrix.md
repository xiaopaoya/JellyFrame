# Developer Capability Matrix

> Last updated: 2026-08-15; Applies to: 0.6.0-dev; active development line: 0.6.0


This document is the practical contract for application authors using JellyFrame.
It describes what the engine can do today, what it deliberately degrades, and
what should not be relied on yet. The target reader is a developer building a
small embedded UI or a desktop validation page for a future wearable device.

JellyFrame is not a general browser. It is a small HTML/CSS/DOM/script runtime
that keeps the common application model while cutting browser services that are
expensive, hard to bound, or not useful on constrained devices.

## Syntax Contract

App-authored HTML, CSS and JavaScript should be a documented subset of the Web
platform. JellyFrame-specific configuration belongs in `jellyframe.app.json`,
CLI/tool options, frame scripts, package reports or host/port interfaces, not in
private page syntax. Use package-local standard paths such as `/assets/icon.bmp`,
`/data/weather.json` or relative URLs inside pages; private URL schemes are not
part of the app syntax contract.

## Status Labels

- **Works**: implemented and covered by the current design.
- **Subset**: usable, but only the documented part should be used.
- **Stored**: parsed or kept in style data, but not fully executed visually.
- **Lazy**: skipped or simplified as a unit without corrupting following input.
- **Deferred**: intentionally absent; do not depend on it.
- **Shell-only**: available in desktop examples, not in platform-neutral core.

## HTML Living Standard Support Table

This matrix is the readable contract for important JellyFrame subsets and
degradation behavior. For exhaustive lookup before using a tag, DOM API or
browser behavior, use the full HTML Living Standard support table:

- Human-readable: [html_living_standard_support_table.md](html_living_standard_support_table.md)
- Machine-readable: [html_living_standard_support_table.csv](html_living_standard_support_table.csv)

The full table is intentionally exhaustive and searchable. This matrix stays
shorter and explains the behaviors that app authors are most likely to depend
on.

Status values in the full table mean:

- `supported`: usable in the documented JellyFrame subset.
- `partial`: supported only as a subset, fallback or ordinary-element
  preservation; check this matrix for details.
- `host_dependent`: requires a manifest capability, target profile, host
  service, codec, text backend or budget.
- `unsupported`: do not rely on it in JellyFrame apps.
- `out_of_scope`: specification prose, legacy browser compatibility machinery or
  browser-scale behavior outside the app runtime contract.

The first follow-up passes moved low-cost tail items into the supported subset:
`details` / `summary` disclosure, `title` / `lang` / `dir` reflection,
`document.title`, `document.dir`, `readOnly`, `maxLength` and range `min` /
`max` / `step` reflection, plus `on*` handler properties for events JellyFrame
actually dispatches. Browser-scale systems such as navigation/history, browsing
contexts, Workers, Worklets, full media, Shadow DOM, Custom Elements lifecycle,
Microdata export and XML/XHTML syntax are explicit non-goals unless a future
product profile creates a separate host-owned capability.

## CSSWG Support Table

Before using a CSS property, function, selector, value or at-rule, search the
full CSSWG support table. It uses the same status vocabulary as the HTML table;
`partial` is especially important because CSS value details are only usable in
their documented owning property or value position.

- Human-readable: [csswg_support_table.md](csswg_support_table.md)
- Machine-readable: [csswg_support_table.csv](csswg_support_table.csv)

## Best Fit

JellyFrame works best for:

- Weather, clock, timer, calculator and settings apps.
- Small dashboards with cards, text, form controls and host-provided data.
- Local embedded applications that want HTML/CSS/JS authoring instead of canvas
  drawing.
- Desktop validation through `jellyframe_pseudo_browser` for the
  platform-neutral pipeline or `jellyframe_desktop_shell` for interactive app
  behavior.

JellyFrame is not ready for:

- Arbitrary modern websites.
- Frameworks that assume a complete DOM, selector API, browser loader, network,
  storage, modules, canvas or Web Components.
- Pixel-compatible browser rendering.
- Large pages that depend on full flexbox/grid, container queries, image decode,
  font loading or advanced text shaping.

## Core Boundary

| Area | Status | Contract |
| --- | --- | --- |
| Platform-neutral core | Works | Core code performs no file, network, windowing or hardware I/O. |
| Desktop pseudo browser | Shell-only | Runs the full pipeline and writes BMP/PPM. Uses tiny built-in fallback text unless a platform text painter is injected. |
| Win32 browser shell | Shell-only | Opens a desktop window, uses GDI text measurement/painting, forwards mouse/wheel/keyboard input, supports capture output and optional scripting builds. |
| Embedded backend boundary | Port-owned subset | The platform-neutral engine exposes framebuffer conversion, dirty rects, scroll-blit planning, frame sink boundaries, input-controller hooks, text callbacks and host-service pumps. Real display flush, touch/key drivers, panel DMA, sleep/wake and storage/network/audio/sensor devices remain port-owned. |
| Linked CSS loading | Shell/host subset | Hosts may load local `<link rel="stylesheet">` through the core callback helper; remote loading remains absent. A document accepts only its host-configured stylesheet count and aggregate CSS byte budget (`max_document_stylesheets` and `max_resource_bytes`); a complete later stylesheet is skipped with `css-document-resource-limit`, never truncated. |
| Classic script collection | Host subset | Classic inline and package-local external scripts are collected in document order when scripting is enabled. A document accepts only its host-configured script count and aggregate source byte budget (`max_document_scripts` and `max_resource_bytes`); a complete later script is skipped with `script-document-resource-limit`, never truncated. Module loading and remote scripts remain absent. |
| Network loading | Host-optional XHR V0 | Core still has no HTTP, WebSocket or remote resource loading, and remote HTML/CSS/script/image resources cannot enter the page loader. `NetworkFetchMock` provides a fixture/handle/completion contract; `JerryScriptRuntime` exposes an async `XMLHttpRequest` GET subset only when the host binds a service admitted by `network.fetch` and its host profile. Network rejection/completion failures classify into stable diagnostics such as `capability-denied`, `invalid-url`, `resource-not-found`, `offline`, `response-budget-exceeded`, `response-handle-budget-exceeded`, `request-timeout` and `request-cancelled`. Real networking belongs to host services/workers, and JS callbacks run only after UI/main task completion pumping. |
| Storage | Host-optional localStorage V0 | No cookies, IndexedDB or filesystem API in core. `AppPrivateKvStorageMock` provides app-id-isolated async KV semantics and budget checks. `JerryScriptRuntime` exposes a tiny `localStorage` subset only when the host binds a non-blocking `AppLocalStorageShadow` admitted by `storage.kv` and its host profile; otherwise it is absent. Storage failures classify into stable diagnostics such as `capability-denied`, `invalid-key`, `value-budget`, `quota-exceeded`, `not-found`, `handle-budget-exceeded`, `operation-timeout` and `operation-cancelled`. `AppStorageLifecyclePolicy` and `apply_app_storage_lifecycle(...)` define when hosts flush, drop, delete or retain pending/persistent app storage during suspend, exit, crash, uninstall, update and memory pressure, with stable system-shell diagnostics such as `storage-flush-ok`, `storage-flush-failed`, `storage-drop-pending`, `storage-delete-data` and `storage-retain-data`. |
| App distribution | Shell/host contract V0 | `.jfapp` bundles can be generated by the CLI and installed into the desktop registry mock. The bundle carries the normalized manifest summary produced by package validation; registry installation and the native Win32 bundle loader both reject missing/type-invalid summary shape, non-normalized entry paths or capability projection drift before interpreting app policy. Registry entries now carry product-oriented state fields such as `status`, `enabled`, `updatedAtUtc`, optional `rollback` and optional `failure` records. `status` is limited to `installed`, `disabled` and `failed`; `rollback-ready` is derived from rollback metadata and documented in `tools/schemas/jellyframe.installed_apps.registry.schema.json`. Updating an app keeps the previous bundle as a rollback target; installing a lower `versionCode` is rejected by default and requires explicit `--allow-downgrade`. `jellyframe_cli.py install --candidate` and the Win32 shell `--install-candidate PATH` accept a host-prepared local candidate JSON after the host has downloaded and verified the bundle; before committing, they require the declared SHA-256 to match, a `trusted` signature result, and recorded user approval. `tools/app_registry.py state` emits a launcher-friendly derived state report. `tools/app_registry.py rollback` and the Win32 shell `--rollback-app ID` validate the normal rollback path. `enable` / `disable` keep bundle and data while gating launchability; enabling a failed app clears its failure record. The sample Win32 shell requires a separate host-owned confirmation before clearing data or removing an app. Removing an app deletes both the current and rollback bundle unless data retention is explicitly requested. This is still a desktop/system-shell contract, not a complete app store, signature authority, payment flow or JavaScript package-management API. Real products still own download, signature verification, user approval, firmware-safe staging and persistent registry storage. |
| Authorized file access | Host broker design V0 | Ordinary apps have no general filesystem access and cannot affect runtime files, system components or another app's data. System components, file managers or user-approved apps should declare `file.read`, `file.write` or `file.manage` and go through a host-owned file broker. Core provides `AuthorizedFilePolicy`, `AuthorizedFileRequest` and validation helpers for capability gates, user approval, normalized logical paths, byte budgets and stable errors such as `user-approval-required`, `capability-denied`, `invalid-path`, `traversal-rejected` and `byte-budget-exceeded`. The Win32 shell exposes `--authorized-file-smoke DIR` to validate denied writes, traversal rejection, staged commit/rollback and manage-operation gates. Real product I/O and permission UX remain host work; no JavaScript file API is exposed yet. Apps still must not receive raw filesystem, flash partition or block-device handles. |
| Video frame preview | Host-optional experimental contract | `media.video.frame` is a bounded host-owned latest-frame contract for product-specific preview surfaces. The V0 runtime accepts MJPEG and only explicitly enabled H.264 baseline, permits one in-flight request per source, allocates a bounded RGB565 frame handle, retains the prior frame if a replacement cannot allocate, and drops stale frames after replacement. It has no HTML `<video>`, `HTMLMediaElement`, track, JavaScript media API, in-core codec, or effect on apps that do not request it. Targets report `hostServices.videoFrame` through package/check diagnostics. |
| Canvas 2D | Optional V0.4 capability | `graphics.canvas2d` declares bounded `<canvas>` / `CanvasRenderingContext2D` use. Render core provides `Canvas2DRegistry` with lazy RGBA backing stores, per-surface/total-pixel budgets, path-point/state-stack budgets, gradient/stop budgets and image-display-list integration. JerryScript builds expose `canvas.getContext("2d")`, `fillStyle`, `strokeStyle`, `lineWidth`, `globalAlpha`, `font`, `save`, `restore`, `clearRect`, `fillRect`, `strokeRect`, `beginPath`, `moveTo`, `lineTo`, `arc`, `closePath`, `fill`, antialiased `stroke`, `measureText`, `fillText`, `createLinearGradient`, `createRadialGradient`, `CanvasGradient.addColorStop`, `translate`, `resetTransform`, `quadraticCurveTo` and `bezierCurveTo` when the host binds a registry. Linear gradients are sampled for rectangles, paths and strokes; Canvas text uses one sampled color because it shares the host text-command backend with DOM text. Canvas text uses the same host text backend as DOM text when provided and falls back to the tiny built-in bitmap path otherwise. Backing pixels are allocated only after `getContext("2d")`; apps without Canvas pay no drawing cost. Target presets still need to opt in with `hostServices.canvas2d`; unsupported targets produce package/check warnings. Canvas-to-canvas `drawImage()` supports the standard 3/5/9-argument forms with clipped nearest-neighbor scaling and existing `globalAlpha`; it requires an already allocated source canvas surface and creates no new surface. `createRadialGradient()` accepts only two stops on concentric circles. `translate(x, y)` rounds finite offsets to pixels, applies to drawing/path/text/image destination coordinates, captures the current translation for newly created gradients and is preserved by `save()`/`restore()`; `resetTransform()` clears that bounded translation only. `quadraticCurveTo()` tessellates one quadratic segment into at most 24 retained path points, and `bezierCurveTo()` tessellates one cubic segment into at most 32; both reject paths that exceed the existing point budget. `<img>`, `ImageBitmap` and video sources, self-copy, imageData, focal/off-center or multi-stop radial gradients, scale, rotate, generic matrices, pattern fills and compositing modes beyond source-over are deferred. |
| System status events | Host-optional V0 queue | `AppSystemEventQueue` lets a host producer inject bounded time/timezone/network/battery/screen/low-power snapshots for the current app instance. Producer push, UI-frame pump, discard and inspection are synchronized; the producer reads only the host's atomic instance-id snapshot and never borrows lifecycle or DOM state. Stale-instance events are dropped at frame boundaries; `try_push_current(...)` can diagnose `empty-instance` / `queue-full`. JerryScript V0 maps host time to standard `Date.now()`, plus `navigator.onLine`, the `window` `online`/`offline` event subset, `document.hidden`, `document.visibilityState` and `visibilitychange`. `TimeChanged` refreshes the host clock without dispatching a web event. |
| Host data snapshots | Host-optional V0 | `AppHostDataSnapshot` defines a fixed-size, allocation-free host summary shape for battery, weather, activity, location and sensor summaries. `AppHostDataAccessPolicy` strips everything by default. With an explicitly bound snapshot and `system.battery`, `system.weather` or `system.activity` declared in the manifest, `navigator.jellyframe.getSnapshot()` synchronously returns only approved battery/weather/activity values; unavailable fields are `null`. The call creates no service request, subscription, polling loop, callback queue or native handle. This proprietary namespace deliberately does not emulate Battery Status, Geolocation or sensor browser APIs. |
| Host compute jobs | Host-optional contract V0 | `compute.jobs` can declare bounded named host work. `AppComputeJobRequest` carries only an operation name, bounded byte input and timeout; the host runs it outside the UI owner and returns a result handle through the existing request/completion/handle boundary. `AppComputeJobPolicy` limits input bytes, result bytes and jobs per app; the result is cleaned on app teardown. This is deliberately not `Worker`, `MessagePort`, arbitrary callback code or a JavaScript API yet. |
| Sensors/location | Host-optional semantic service V0 | Apps may declare `sensor.accelerometer`, `sensor.gyroscope`, `sensor.heart-rate`, `sensor.ambient-light` or `location.position` in the manifest. Platform-neutral `AppSensorSampleMock` / `AppLocationSnapshotMock` policies are enabled only when the host/profile allows the same capability. Results return to the UI task through bounded request/completion queues and host handles; apps never receive raw GPIO/I2C/SPI/BLE/GPS handles. `JerryScriptRuntime` exposes the `navigator.geolocation.getCurrentPosition(success, error)` subset when a location service is bound; sensor JS APIs are still deferred. Failures classify into diagnostics such as `capability-denied`, `sample-unavailable`, `record-budget-exceeded`, `handle-budget-exceeded` and `request-timeout`. Real sample cadence, background policy and permission prompts belong to the host. |
| App frame policy | Works V0 | `AppFramePolicy` maps foreground/suspended, screen-on and low-power state into input/timer/rAF/present budget policy. Low-power can keep input and timers while stopping animation; screen-off or suspended pauses foreground input, timers, rAF and presentation, and recommends a first repaint after resume. |
| App teardown/recovery | Works V0 | `AppRuntimeHost::terminate_current(reason)` cancels current-app requests, discards completions, releases host handles and clears app font resources with stable reason names such as `user-kill`, `script-watchdog`, `budget-exceeded`, `load-failure` and `system-policy`. Script-service requests carry an internal runtime client token through request, completion and returned handle; `clear_app_services()` releases only that runtime's resources and consumes its late completions without touching another consumer in the same app instance. The Win32 system shell records installed apps terminated for watchdog or budget exhaustion as failed before returning to the launcher; users can re-enable, roll back or remove them. `--system-survival-smoke N` validates repeated bad-app budget recovery, stale completion filtering and launcher event delivery. |

## HTML Parsing

| Feature | Status | Behavior |
| --- | --- | --- |
| UTF-8 input bytes/string | Works | Input is treated as byte/string data. Text rendering quality depends on the renderer backend. |
| Start/end tags | Works | Common tags become DOM elements. |
| Attributes | Works | Quoted and common unquoted forms are parsed. Attribute names are normalized by parser paths that lowercase HTML names. |
| Text nodes | Works | DOM text preserves author whitespace. The render tree skips pure formatting whitespace outside preserving contexts so indentation does not pollute block/grid/flex layout. Layout/rendering collapses ordinary display text while preserving `pre`, `script`, `style`, `textarea` and `title` text. |
| Comments | Works | Tokenized and ignored by visual tree construction. |
| Doctype | Works/Lazy | Accepted; no quirks mode is entered. |
| Character references | Subset | Common named references and decimal/hex numeric references are decoded, including common Windows-1252 legacy numeric remaps. Unknown or unsupported cases degrade to literal/fallback behavior. |
| Raw text for `script`/`style` | Subset | Content is preserved enough for style/script collection. |
| RCDATA-like `textarea`/`title` | Subset | Character references are decoded in bounded simplified content scanning. Full browser RCDATA state compatibility is absent. |
| Synthesized `html`/`body` | Works | Missing wrapper structure is repaired. |
| Void elements | Works | Common void tags do not require closing tags. |
| Implied end tags | Subset | Common paragraph/list/table-ish cases are tolerated; full HTML tree-builder compatibility is not a goal. |
| Malformed markup | Subset | Parser recovery is bounded. Before DOM construction, the tokenizer limits attributes per tag plus tag-name, attribute-name and attribute-value bytes; excess input is still consumed but does not keep growing a token string and emits a stable diagnostic. The tree builder continues to report node/depth/attribute pressure. Malformed input must not loop forever or crash. |
| Quirks mode | Deferred | Always ignored. JellyFrame targets modern authored pages. |
| Template contents | Lazy | `template` is hidden by default style; template DOM semantics are not implemented. |
| Custom elements | Subset | Unknown tags create elements and can be styled as ordinary boxes; lifecycle callbacks are absent. |
| `details` / `summary` | Subset | Closed `details` renders only the first `summary`; open `details` renders summary and content. Pointer or focused activation on that summary toggles the `open` attribute and dispatches a `toggle` event; `click.preventDefault()` blocks the default toggle. The `name` grouping behavior, `ToggleEvent` class and browser disclosure marker styling are not implemented. |

## DOM Model

| Feature/API | Status | Behavior |
| --- | --- | --- |
| `Node` tree | Works | Element and text nodes with parent/children ownership. |
| `tag_name`, `text`, `attributes` | Works | Internal C++ model exposes these fields. |
| `append_child` | Works | Moves/attaches a child and marks tree/layout dirty. |
| `detach_child` / `remove_child` | Works | Removes child ownership and marks tree/layout dirty. |
| `set_attribute` / `remove_attribute` | Works | Updates attributes, resets relevant form state and marks attributes/style/layout dirty. |
| `set_text` / `set_text_content` | Works | Updates text and marks text/layout dirty only when content changes. |
| `text_content()` | Works | Concatenates descendant text. |
| `attribute()` | Works | Returns empty string for missing attributes. |
| `has_class()` | Works | Whitespace-separated class matching. |
| Dirty flags | Works | Dirty bits propagate to ancestors, so root dirty checks are O(1). Clean subtree clearing skips clean branches. |
| DOM ranges | Deferred | No Range/Selection model. |
| MutationObserver | Deferred | Use host dirty flags instead. |
| Shadow DOM | Deferred | No shadow root, slots, parts or scoped tree. |
| Full browser `document` | Deferred | Only the bound scripting subset exists. |

## CSS Syntax And CSSOM

| Feature | Status | Behavior |
| --- | --- | --- |
| Comments | Works | Removed during parsing. |
| Qualified rules | Works | `selector { declarations }`. |
| Selector lists | Works | Split on top-level commas. |
| Declaration order | Works | Duplicate declarations are preserved for fallback. |
| `!important` | Works | Participates in cascade. |
| Balanced functions/strings | Works | Parser skips over nested component values safely. |
| Bad declarations/rules | Works | Recover at declaration/rule boundaries. |
| `@layer` | Lazy | Block is flattened; layer ordering is not modeled. |
| `@media` | Subset | Empty/all/screen blocks are parsed. `screen`/`all` queries with `min-width`, `max-width`, `min-height` and `max-height` in `px` or unitless px-like values are evaluated against the configured parser viewport. Unsupported or complex media queries are skipped as full blocks. |
| `@supports` | Subset | Declaration feature queries are evaluated conservatively. `(property: value)`, `not`, homogeneous `and`/`or` chains and parentheses are supported. `selector()` and unknown/unsafe features evaluate false and skip the block. |
| `@container` | Deferred/Lazy | Whole block skipped. Avoid it for required UI. |
| `@font-face` | Lazy | Balanced block skipped; CSS font loading rules are not implemented. App fonts must be declared in manifest `fonts[]`; `.jffont` entries can then participate in runtime text selection through the documented `font-family` subset when the host uses the app-font text backend. |
| Font coverage check | Default tooling preflight | `package`, `check`, `preview` and source-package `install` run `jellyframe_font_resource_check` by default and use `--font-subset auto` to write `*.used_chars.txt` beside the report, plus a `fontSubset` plan in JSON. `--font-coverage` reports missing codepoints before embedding, and `--font-source-bdf` + `--font-output` can invoke the existing BDF-to-`.jffont` generator during preflight; the generated file still must be declared explicitly in manifest `fonts[]` by the app author. `--no-font-check` or `--font-subset off` skips the path explicitly. Package reports also include `fontDiagnostics`, which merges source codepoints, target font-profile estimates and manifest `.jffont` glyph tables so missing app-visible glyphs are reported before install. |
| Font profile and budget estimate | Default tooling preflight | Defaults to a `16x16` bitmap-font byte estimate; `--font-budget WxH` overrides it and recommends `tiny`, `tiny-plus-symbols`, `app-subset-cn`, `cn-standard` or `global-product` from scanned codepoints. Missing manifest `fonts[].license`, `sizes` or `weights` metadata is reported before release; `budgets.maxAppFonts`, `maxAppFontBytes` and `maxAppFontGlyphs` cap installable `.jffont` count, bytes and glyphs. |
| Bitmap font pack generation | Tool/runtime/fallback chain works | `jellyframe_font_pack_gen` subsets BDF bitmap fonts into C++ `BitmapFont` headers for embedded builds and can also emit `.jffont` V0/V1 binary supplements. V0 is compact 1bpp. V1 stores opt-in 2bpp/4bpp glyph coverage for font-level antialiasing through `--coverage-bits 2|4`; 1bpp fonts keep the compact path and pay no coverage cost. `BitmapFontResource` can parse `.jffont` bytes, and `AppFontSet` exposes a bitmap fallback chain. Generic/no-family text tries the system font profile first, then app supplements for missing glyphs. A CSS `font-family` whose first family matches a manifest `.jffont` family hash tries that app font first, then falls back to the system/default chain. Win32 `--use-app-fonts` validates this layout/paint path. Stable `.jfapp`/flash font payloads can be attached through the zero-copy view path. Package diagnostics match explicit CSS `font-family` declarations against manifest metadata and validate `sizes`/`weights` arrays. App-font rendering supports low-cost integer scaling from CSS `font-size` and synthetic bold for `font-weight >= 600`; full browser font matching, stretch/style/features and vector shaping remain future work. |
| `@keyframes` | Subset | Parses named `@keyframes` blocks and stores `from`/`to` or `0%`/`100%` declarations. Intermediate percentages are diagnosed and ignored. Execution is limited to the animation property subset below. |
| Unknown at-rules | Lazy | Statement or balanced block skipped. |
| CSS custom properties | Subset | Direct `var(--token)` and `var(--token, fallback)` are resolved from inherited `:root`/ancestor/current/inline custom property declarations. Unresolved `var()` values do not override earlier supported fallbacks. Full dependency graph, case-sensitive custom names and complete invalid-at-computed-value-time semantics are absent. |
| CSS nesting | Explicit single-level subset | A qualified rule may contain one level of nested qualified rules only when each nested selector includes `&`, for example `.card { &:hover { ... } & .label { ... } }`. Parent declarations and nested rules retain source order; comma expansion is capped at 16 selectors. Implicit nesting, nesting deeper than one level, nested at-rules and selector forms without `&` are skipped with `css-nesting-skipped`. |
| Cascade origins | Subset | Author + inline + small built-in defaults. No user/animation origin. |
| Rule indexing | Works | Rules are bucketed by rightmost id/class/tag/universal selector. |

## Selectors

| Selector | Status | Behavior |
| --- | --- | --- |
| Type selector | Works | `button`, `section`. |
| Class selector | Works | `.card`. |
| ID selector | Works | `#search`. |
| Simple compound | Works | `button.primary.large`. |
| Descendant combinator | Works | `.panel button`. |
| Child combinator | Works | `main > section`. |
| Simple attribute selector | Subset | Existence and simple equality-style matching are supported. |
| `:root` | Works | Supported. |
| Dynamic pseudo-classes | Subset | `:hover`, `:active`, `:focus`, `:focus-within`, `:checked` and `:disabled` participate in selector matching. Input state changes mark style/layout dirty; checked/disabled use form-control and attribute state. |
| `:is()` / `:where()` | Subset | `:is()` matches selector-list arguments and contributes the maximum argument specificity. `:where()` matches the same subset with zero specificity. |
| `:has()` | Deferred/Lazy | Rules using `:has()` are skipped; relational selector matching is intentionally deferred. |
| Pseudo-elements | Subset | `::before` supports a tiny generated-content path for text/counter list markers and short generated text. `::after` supports the same text/counter paint subset for badges, units and status markers. Full generated-content layout, marker styling and selection styling are deferred. |
| Sibling combinators | Subset | Adjacent `+` and general `~` sibling selectors match previous element siblings. Text nodes between elements do not block adjacent matching. |
| Shadow selectors | Deferred | `::part`, `::slotted` skipped. |

## CSS Properties

Only supported values should be used for required UI. Unsupported values do not
clear older supported fallback declarations.

| Property | Status | Supported values / degradation |
| --- | --- | --- |
| `display` | Subset | `block`, `inline`, `inline-block`, `flex`, `inline-flex`, `grid`, `inline-grid`, `none`. Inline flex/grid map to the same simplified layout modes. |
| `color` | Subset | Named basics, hex, `rgb()`, `rgba()`, `hsl()` / `hsla()` with numeric degrees, percentage saturation/lightness and optional alpha, plus two-color `color-mix(in srgb, <color> [<percentage>], <color> [<percentage>])`. This is useful for translucent borders and tinted depth without extra DOM. `oklch()` and other color spaces do not override fallbacks. |
| `background-color` | Subset | Same color parser as `color`; gradients are intentionally not accepted here because CSS treats them as background images. |
| `background` | Subset | Solid colors; two-color `linear-gradient()` with `to top/bottom/left/right`, diagonal keywords, or common design-tool angles `0deg` through `315deg` in 45-degree steps; a two-segment progress `conic-gradient(<color> 0% N%, <color> N% 100%)`; and two-color circular `radial-gradient([circle] [at center\|<x%> <y%>,] <color> [0%], <color> [100%])`. Up to two layers are supported and painted in standard bottom-to-top order, enabling a base gradient plus a translucent radial highlight. The optional top layer is stored as RGBA4444 to keep per-node memory bounded. Conic gradients are for rings; positioned radial gradients are for hydrogel highlights and inset depth. Other angles, complex stops, focal points, ellipses, repeating and more than two backgrounds are diagnosed and ignored without clearing earlier fallbacks. Out-of-subset conic/radial syntax emits `style-conic-gradient-unsupported` / `style-radial-gradient-unsupported`, and oversized paint areas emit `layer-conic-gradient-area-budget` / `layer-radial-gradient-area-budget`. |
| `background-image` | Subset | Accepts the same one/two gradient-layer subset as `background`, but not solid colors. One package-absolute `url("/assets/image.bmp")` is also supported: it reuses the host-owned image surface cache, with `background-color` retained as a fallback. One package image may use `background-size: cover`, `contain` or `100% 100%`, the simple `background-position` subset, and `background-repeat: no-repeat`; the chosen fit, position and sampling mode reuse the existing image display command, adding no fields, decoder, cache or paint work to pages without a background image. Remote/data URLs, relative paths, query/fragment/traversal, multiple URL layers, tiling and arbitrary size expressions remain unsupported. Package reports emit `backgroundImageDiagnostics` for invalid or missing local resources. |
| `margin` | Works | 1-4 length values plus horizontal `auto`. |
| `margin-top/right/bottom/left` | Works | Physical longhands. `margin-left/right:auto` works for horizontal centering paths. |
| `padding` | Works | 1-4 length values. |
| `padding-top/right/bottom/left` | Works | Physical longhands. |
| Logical box edges | LTR subset | In horizontal LTR writing mode, `margin-inline` / `margin-block`, `padding-inline` / `padding-block`, `border-inline-width` / `border-block-width` and their `-start` / `-end` longhands expand into the same physical cascade slots. Logical border color/style shorthands, writing modes and RTL mapping are not implemented. |
| `border` | Subset | Parses `none`, width and color from simple shorthand. Style keyword is tolerated only as ignored text. |
| `border-top/right/bottom/left` | Subset | Supports the width/color subset of single-side shorthands, for example `border-right: 1px solid #ddd`. The current style model has one `border_color`, so a single-side shorthand color becomes the global border color; the single-side width applies only to that edge. |
| `border-width` | Works | 1-4 length values. |
| `border-top/right/bottom/left-width` | Works | Physical width longhands. |
| `border-color` | Subset | Single color for all borders. |
| `border-radius` | Subset | One to four non-negative length values in standard physical order, plus a single percentage such as `50%`. Rounded fills and borders use local coverage antialiasing. A single radius keeps the compact fast path; distinct corners use the bounded per-corner raster path. Elliptical slash syntax and percentage-per-corner values are deferred. |
| `outline` / `outline-width` / `outline-color` / `outline-offset` | Subset | Painted as a non-layout outer stroke. Simple width/color shorthand and a supported length offset are available; positive offsets create a visible focus gap, while negative offsets overlap the border box. Outline style keywords, auto/invert colors and browser focus-ring policy are deferred. |
| `width` / `height` | Works | Length values and percentage values. Percentages resolve against the containing content box; root/full-screen app wrappers can use the actual viewport width/height. |
| `min-width` / `min-height` | Works | Length values and percentage values. |
| `max-width` / `max-height` | Works | Length or percentage value; used by block layout. |
| `aspect-ratio` | Works | Positive number or `w / h`, including `auto w / h`. Used for intrinsic box height. |
| `font-size` | Works | Length values. |
| `font-weight` | Subset | `normal`, `bold`, `bolder`, `lighter` and numeric weights. Software fallback approximates bold; platform text painters decide final glyph weight. |
| `line-height` | Works | Unitless multiplier or length. |
| `text-align` | Works | `left`, `right`, `start`, `end`, `center`. |
| `text-indent` | Works | Length value. |
| `letter-spacing` | Bounded subset | `normal` or a supported length from `-0.5em` through `2em` after local font-size resolution. Declared spacing measures and paints the same UTF-8 scalar advances; it is inherited. The declared path emits one text command per scalar, so use it for short labels and numeric displays rather than long paragraphs. Complex-script shaping, kerning preservation and browser letter-spacing edge cases remain with the platform text backend. |
| `text-transform` | ASCII subset | `none`, `uppercase`, `lowercase` and `capitalize`. Transformation is applied consistently before text measurement and paint. V0 intentionally transforms ASCII letters only; locale-sensitive Unicode case mapping is deferred. |
| `text-decoration` / `text-decoration-line` | Subset | `none`, `underline` and `line-through` paint cheap solid decoration lines. Color/thickness/style variants and wavy/double lines are deferred. |
| `text-shadow` | Subset | One `<offset-x> <offset-y> [blur-radius] [color]` shadow is painted as offset text. Authored color is retained and omitted color uses `currentColor`; blur is parsed for compatibility but not rasterized, and multiple shadows are not painted yet. |
| `box-sizing` | Works | `content-box`, `border-box`. |
| `visibility` | Bounded subset | Inherited `visible` and `hidden` preserve normal layout flow. `hidden` suppresses the element's own paint and hit target; a descendant explicitly declaring `visible` can paint and receive input. There is no hidden-subtree allocation, cache or idle-frame work. `collapse`, accessibility-tree behavior and browser focus-navigation semantics are deferred. |
| `overflow` / `overflow-y` | Subset | `overflow` accepts `visible`, `hidden`, `clip`, `auto`, `scroll`. Standard `overflow-y` accepts only `auto` and `scroll`, and is the preferred explicit vertical-scroll spelling; other axis-specific values are rejected rather than silently clipping both axes. V0 native scroll containers support fixed-size vertical `auto`/`scroll` regions with host-provided scroll offsets, clipped paint and hit testing. `VerticalScrollGesture` is an allocation-free host helper for tap-versus-drag thresholding, control cancellation after a drag begins and bounded inertia; it consumes no work unless a host drives it. The Win32 shell maps wheel/arrow/drag default actions to the nearest scrollable container before falling back to page scroll, and the ESP32-S3 retained-scroll demo uses the same gesture rules for queued touch input. Hosts repaint only the container viewport when the dirty area stays within budget. A conservative strip-blit fast path is used only for safe rectangular, opaque, non-overlapped containers; rounded/translucent/overlapped containers fall back to dirty repaint. `overflow-x` and horizontal scroll are deferred. |
| `white-space` / `text-wrap` | Bounded subset | `white-space: normal` / `nowrap` and the equivalent modern aliases `text-wrap: wrap` / `nowrap` share one cascade slot and the same layout path. `nowrap` is inherited into text layout and prevents cheap wrapping. `balance`, `pretty`, `text-wrap-mode` and `text-wrap-style` remain unsupported. |
| `overflow-wrap` | Bounded subset | `normal` and inherited `anywhere`. `anywhere` only breaks at valid UTF-8 scalar boundaries and uses the same scalar measurement path as paint. It does not implement hyphenation, grapheme/word segmentation dictionaries, `break-word`, balance/pretty wrapping, or complex-script line breaking. The extra scalar work and display commands are paid only when declared. |
| `text-overflow` | Bounded visual subset | `clip` and `ellipsis` are accepted. For `ellipsis` with inherited `white-space: nowrap`, an over-wide text run is UTF-8-safely shortened during layer-tree construction and painted with an ASCII `...` marker that all small bitmap fonts can render. The path uses the active host/app font measurement and runs only for declared overflowing text. Multi-line ellipsis, locale-sensitive U+2026 selection behavior and browser line-clamp semantics are deferred. |
| `opacity` | Subset | 0..1; creates composited layer in software compositor. |
| `position` | Subset | `relative` applies visual offsets without changing normal-flow space. `absolute`/`fixed` boxes are taken out of flow and positioned by simple insets. `sticky` is only stored as a layer hint. |
| `top` / `right` / `bottom` / `left` | Subset | Length and `auto` values. Absolute/fixed boxes use parent content box or viewport-like origin. Percentages, shrink-to-fit, full containing-block rules and sticky scrolling are absent. |
| Logical sizing/inset | LTR subset | `inline-size` / `block-size`, their `min-` / `max-` forms, `inset`, `inset-inline` / `inset-block` and their start/end longhands map to the existing LTR physical width/height/inset subset. Writing modes and RTL mapping are deferred. |
| `z-index` | Subset | Integer or `auto`; layer-local ordering only. |
| `transform` | Subset | `translate()`/`translateX()`/`translateY()`, `scale()`/`scaleX()`/`scaleY()` and `rotate()`/`rotateZ()` are parsed into a composited layer and painted by the software compositor. Angles support `deg`, `turn`, `rad` and `grad`. `transform-origin` supports common keywords and percentages. `skew()`, `matrix()`, perspective and 3D transforms are unsupported and diagnosed/ignored. |
| `justify-content` | Subset | `start`, `flex-start`, `normal`, `end`, `flex-end`, `center`, `space-around`, `space-between`, `space-evenly` on the main axis of simplified row and column flex. |
| `align-items` | Subset | `stretch`, `normal`, `start`, `flex-start`, `center`, `end`, `flex-end` on the cross axis of simplified row and column flex. |
| `align-self` | Subset | `auto`, `stretch`, `normal`, `start`, `flex-start`, `center`, `end`, `flex-end` override the parent cross-axis alignment for one simplified row or column flex item. |
| `align-content` | Subset | `start`, `flex-start`, `normal`, `end`, `flex-end`, `center`, `space-around`, `space-between`, `space-evenly` distribute wrapped row lines only when the flex container has extra fixed or minimum height. `stretch` and column wrapping are unsupported. |
| `place-content` | Subset | One/two-value shorthand that expands to the documented `align-content` / `justify-content` subsets. `place-items` and `place-self` remain deferred because their missing grid alignment semantics must not be silently discarded. |
| `flex-direction` | Subset | `row` and `column`. `row-reverse` and `column-reverse` are rejected rather than approximated. |
| `order` | Subset | Signed integer order for direct flex children. A nonzero order creates one stable temporary ordering view for layout and same-stack paint order; default-zero flex containers retain the allocation-free source-order path. This is not a full order-modified document model for browser APIs. |
| `flex` | Subset | Shorthand supports common `none`, `auto`, `<grow>`, `<grow> <basis>` and `<grow> <shrink> <basis>` forms for simplified row and non-wrapping column flex layouts. Full Flexbox grammar is absent. |
| `flex-grow` / `flex-shrink` / `flex-basis` | Subset | Non-negative numeric grow/shrink factors and supported length/`auto` basis values participate in simplified row and non-wrapping column sizing passes. In a column, basis is the height basis. |
| `flex-wrap` | Subset | `wrap`/`wrap-reverse` enable simple row wrapping. Column flex is intentionally non-wrapping. Wrapped lines use fixed/basis probing and do not run the full per-line Flexbox algorithm. |
| `gap` | Works | 1-2 length values for grid and simplified flex support. |
| `row-gap` / `column-gap` | Works | Length values. |
| `grid-template-columns` | Subset | Extracts minimum track from `repeat(auto-fit, minmax(<length>, 1fr))`, `minmax(<length>, 1fr)`, a length, or `1fr`. |
| `grid-template-rows` | Bounded subset | Two to four fixed-length or `1fr` tracks. A definite grid height distributes remaining space evenly across `1fr` rows; without one, content supplies their minimum. Named lines/areas, weighted `fr`, `repeat()`, content-sized tracks, subgrid and masonry are deferred. |
| simple fixed grid columns | Subset | `grid-template-columns: <length> 1fr`, `repeat(N, 1fr)`, `repeat(N, minmax(0, 1fr))` and similar 2-4 column length/`fr` templates are supported for definition lists, settings forms and compact keypads. |
| `grid-auto-rows` | Subset | Length or `minmax(<length>, auto)` minimum row height. |
| `grid-column` / `grid-row` | Bounded subset | Positive integer start, `span N`, `start / end`, or `start / span N`; spans are bounded. Explicit placement covers the documented template tracks and at most 128 tracked implicit rows. A row/span beyond that bound is laid out after the grid in non-overlapping block flow with `grid-placement-budget`, rather than clamped onto the final row. No negative lines, named lines, `auto` grammar, `grid-*-start/end` longhands, dense packing or browser overlap resolution. |
| `list-style` / `list-style-type` | Subset | `none`, disc-like values and decimal-like values. Native-lite list markers are painted for `li`. |
| `content` on `::before` / `::after` | Subset | Plain text and `counter(name) "suffix"` for lightweight list counters, units and badges. Generated content is painted inside the element box and is budgeted as display commands, not real DOM nodes. Full generated-content layout is deferred. |
| `box-shadow` | Subset | `none` clears the shadow. One outer `<offset-x> <offset-y> [blur-radius] [spread-radius] [color]` shadow emits a bounded, rounded soft-shadow command with quadratic falloff. Authored `hex`/`rgb`/`rgba`/`color-mix()` RGB and alpha are retained; omitted color uses `currentColor`. Blur is capped at 24 px and larger values emit `layer-box-shadow-blur-clamped`. Inset, negative spread, multiple shadows and blend modes remain deferred. Shadow work is emitted only when declared; oversized areas emit `layer-box-shadow-area-budget`. |
| `object-fit` / `object-position` | Subset | `object-fit` supports `fill`, `contain`, `cover`, `none` and `scale-down`. `object-position` supports the one/two-value keyword and percentage subset, such as `center`, `right top` and `25% 80%`; complex four-value and length-offset syntax is deferred. |
| `image-rendering` | Subset | Supports the standard keywords `auto`, `pixelated` and `crisp-edges`. `auto` allows host image painters to use bilinear/smooth sampling; `pixelated` and `crisp-edges` keep nearest-neighbor sampling for pixel art. |
| `font-family` | Runtime subset | Parses a comma-separated family list. The first listed custom family is normalized to a small runtime hash; generic families such as `system-ui`/`sans-serif` map to the host/system fallback. With an app-font backend, a manifest `.jffont` whose `family` matches that hash is tried before the normal system-first fallback chain, and layout measurement and paint use the same selection. Full browser cascade, `@font-face`, style/stretch/features and multi-size matching are not implemented. Package diagnostics still report generic families, manifest matches and unmatched primary families. Win32 uses GDI by default; pass `--use-app-fonts` to validate package `.jffont` selection. |
| `requestAnimationFrame` | Scripting subset | Available in JerryScript builds. The host pumps callbacks with a per-frame budget and timestamp. Background/low-power profiles may set the animation callback/FPS budget to zero. The Win32 validation shell exposes these budgets through `--animation-fps`, `--animation-callbacks` and frame-script commands for deterministic low-power checks. |
| CSS `transition` | Subset | Supports bounded `transition` and `transition-*` lists. Current animatable properties are `opacity`, `transform: translate()/scale()/rotate()`, `background-color` and `color`. Timing accepts named `linear`/`ease*` forms and bounded `cubic-bezier(x1, y1, x2, y2)` where `x1/x2` are 0..1 and `y1/y2` are -2..2. The Win32 debug shell advances the timeline on interaction state changes and uses animation dirty-region helpers to repaint only previous/current motion or paint bounds; layout-property animation does not reflow every frame. |
| `@keyframes` / `animation-*` | Subset | Supports bounded `animation`, `animation-name`, `animation-duration`, `animation-delay`, `animation-timing-function`, `animation-iteration-count`, `animation-direction` and `animation-fill-mode`. Timing accepts named forms and the same bounded `cubic-bezier()` subset. Executed keyframes are limited to `from`/`to` over `opacity`, `transform: translate()/scale()/rotate()`, `background-color` and `color`; layout properties such as width/margin/grid/flex are diagnosed and ignored rather than reflowed every frame. `normal`/`alternate`, positive-integer/`infinite` iterations, and `none`/`forwards`/`backwards`/`both` fill modes are supported. Fill modes retain overrides only when explicitly declared. No play-state or multiple percentage interpolation. |
| Filters/backdrop filters | Deferred | Not painted. |

Supported length units are currently `px`, unitless px-like numbers, `rem`,
`em` and simple `vh`/`vw` approximations. `width`, `height`, `min-width`,
`min-height`, `max-width` and `max-height` additionally preserve percentage values and
resolve them during layout against the containing box or root viewport. Other
percentage lengths still use conservative parser fallbacks. `min()`, `max()`, `clamp()` and simple
`calc(A +/- B)` are parsed when their arguments reduce to supported lengths.
These functions are conservative fallbacks, not a full CSS value algebra.

## Layout

| Feature | Status | Behavior |
| --- | --- | --- |
| Block layout | Works | Vertical box layout with margin, padding, border, max-width and horizontal auto margins. |
| Inline text flow | Subset | Text and inline controls flow horizontally and wrap by available width. |
| Inline background/border | Subset | Shrunk to text/content bounds where possible. |
| `inline-block` | Subset | Represented as inline-like render object with usable box behavior. |
| Flex | Subset | Simplified row layout with basic grow/shrink/basis sizing, justification, alignment, gaps and optional wrapping; non-wrapping column layout with the same main/cross-axis sizing controls. Signed integer `order` is supported for direct flex children. No full Flexbox algorithm, reverse directions, baseline alignment or min-content sizing. |
| Grid cards/forms | Subset | Responsive auto-fit/minmax card grid, gaps, minimum auto rows, spans, `repeat(N, 1fr)`, `repeat(N, minmax(0, 1fr))`, fixed/`fr` `grid-template-rows` and positive numeric `grid-column`/`grid-row` start/end/span placement. No named lines/areas, subgrid or dense packing. |
| Aspect ratio | Works | Provides intrinsic height when explicit height/content height is absent. |
| Positioned boxes | Subset | Bounded `relative`, `absolute` and `fixed` positioning for app overlays, badges and pinned panels. Out-of-flow boxes do not consume block/flex/grid/inline placement space. |
| Replaced elements | Subset | Common controls/media are leaf boxes with fallback sizing; real image/video layout is deferred. |
| Text measurement | Subset | Core exposes `TextMeasureProvider`; fallback is tiny but UTF-8-aware. Win32 shell uses GDI measurement. `HostTextAdapter` can wrap LVGL/vendor measurement callbacks. |
| Bidi/text shaping | Deferred | Production non-Latin text needs a platform text backend or future shaping strategy. |
| Fragmentation/multicolumn | Deferred | Not implemented. |

## Form Controls

| Element / feature | Status | Behavior |
| --- | --- | --- |
| `button` | Works / submit subset | Native-lite painted box, shrink-wrap-ish default and click events. A default or `type=submit` button inside a form runs bounded validation then dispatches a cancellable `submit` event unless its `click` default is prevented. |
| `input type=text` and default input | Works | Value state, UTF-8 text input from host, Backspace. |
| `input type=search/tel/url/email/number` | Subset | These ASCII-case-insensitive types preserve their canonical IDL `type` token and use the bounded text-entry path. There is no browser keyboard hint, URL/email/number parsing or corresponding constraint validation. |
| `readonly` / `maxlength` | Subset | Text-entry controls honor `readonly` and `maxlength` for user input. Script `value` writes remain programmatic state changes; V0 validation can report programmatic values that exceed `maxlength`. |
| `input list` / `datalist` | Subset | Options are not shown as a popup. Focused text inputs can accept the first matching datalist option with Tab/Enter. |
| `textarea` | Subset | Value-like state and basic painting; full multiline editing is limited. |
| `input type=checkbox` | Works | Checked state, click activation, input/change events. |
| `input type=radio` | Subset | Checked state and painting; full same-name group exclusivity is limited. |
| `input type=range` | Works | Track/thumb painting; pointer drag updates value and uses `min`, `max` and `step`. |
| `select` / `option` / `optgroup` | `forms.advanced` popup subset | The basic profile paints the selected option and cycles it on activation. With `forms.advanced`, a single-select click or focused activation opens a core-rendered option overlay; pointer selection commits the value and dispatches `input`/`change`, and Up/Down still moves across options including `optgroup`. The overlay is viewport-bounded and warns when it cannot show every option. Popup scrolling, `multiple`, native pickers and browser top-layer/grouped-menu behavior remain absent. |
| `progress` / `meter` | Works | Value bar painting from attributes. |
| Date/color/file controls | Deferred | Use text/select/range fallbacks for now. |
| Validation / form submission | Subset | `required`, text/textarea `minlength`/`maxlength`, checked required checkbox/radio groups and required select values are checked on form and control `checkValidity()` / `reportValidity()` and submit activation. Validating controls expose a fresh `validity` snapshot with `valueMissing`, `tooShort`, `tooLong`, `customError` and `valid`, plus `willValidate`, `validationMessage` and `setCustomValidity(message)`. Invalid controls receive non-bubbling `invalid`; no browser popup is painted. `form.requestSubmit([submitter])` dispatches cancellable `SubmitEvent`-shaped `submit` with `submitter` after valid data collection. `form.reset()` and uncancelled `button/input type=reset` activation dispatch a cancellable bubbling `reset` and, unless cancelled, restore lazy control state from authored attributes/text without retaining a snapshot. Browser navigation, action/method POST, multipart/file upload, pattern/type/date/range/step validation, `form.submit()` and the remaining `ValidityState` flags remain absent. |
| `dialog` | Bounded modal subset | Scripting builds expose `open`, `returnValue`, `showModal()` and `close([returnValue])`. At most one `showModal()` dialog is active per document. A host may call `request_modal_cancel()` for Escape/back, which dispatches cancellable `cancel` and then `close`. Win32 reapplies the modal input gate after a rebuild, constraining focus/hit testing to the dialog and restoring prior focus after close. No `show()`, `requestClose()`, nested modal, light dismiss, browser top layer, backdrop or complete inert algorithm. |
| Native IME | Shell-dependent | Core accepts UTF-8 text; platform shell must provide text input/IME integration. |

## Events And Input

| Feature | Status | Behavior |
| --- | --- | --- |
| `EventTarget` | Works | Compact listener groups by type. |
| Capture/target/bubble phases | Works | DOM-like dispatch implemented. |
| `preventDefault` | Works | Event object records cancellation. |
| `stopPropagation` / `stopImmediatePropagation` | Works | Implemented. |
| `MouseEvent` | Works | `clientX`, `clientY`, `button`, `buttons`, modifier fields. |
| `WheelEvent` | Works | `deltaX`, `deltaY`, `deltaMode`, modifiers. |
| Hit testing | Works | Layer/layout based, with clipping and z-order hints. |
| Pointer move/down/up | Subset | Platform-neutral input dispatches mouse-like events plus `pointerdown`/`pointerup` aliases. Scroll drag is host default behavior, not HTML Drag and Drop. `setPointerCapture()`, `pointercancel`, `touch-action`, multitouch and full Pointer Events fields are deferred. |
| Click synthesis | Works | Same target down/up creates click. |
| Hash anchor click | Shell-only | Win32 shell handles `<a href="#id">` as viewport scroll. Core only dispatches the click event. |
| Focus tracking / `tabindex` | Subset | `InputController` stores the focused node, drives `:focus` / `:focus-within`, dispatches non-bubbling `focus`/`blur` on real focus changes, and selects the first focusable `[autofocus]` node when constructed. Native controls/links plus generic elements with non-negative `tabindex` participate in tree-order focus navigation; `tabindex=-1` is skipped. Positive tabindex ordering, script `focus()`/`blur()`, focus-visible heuristics and browser focus scopes are deferred. |
| Modal input gate | Host-installed subset | `InputController::set_modal_root()` constrains hit testing, focus traversal and activation to one visible dialog subtree. It retains no global inert state and does no work unless a host installs a root. |
| Touch events | Subset | `touchstart`/`touchend` are exposed as mouse-like events for press feedback. Full multi-touch objects are deferred. |
| Keyboard events | Deferred | Core handles simple key actions for controls; DOM keyboard event objects are not complete. |

## JavaScript / JerryScript Binding

JavaScript support exists only when built with `JELLYFRAME_BUILD_SCRIPTING=ON` and a
local JerryScript tree configured through `JERRYSCRIPT_ROOT`.

| API | Status | Behavior |
| --- | --- | --- |
| Classic document scripts | Subset | In scripting builds, pseudo/Win32 shells execute inline classic `<script>` and local external `<script src>` through host callbacks. |
| `window` / `document` | Subset | Bound objects expose the methods below. `window`, `window.window`, `self` and `document.defaultView` point to the same JellyFrame window object; `window.document` points to the bound package document and `window.navigator` points to the embedded Navigator subset. Package documents expose `origin === "null"`, `isSecureContext === false` and `crossOriginIsolated === false`; browser URL/origin/security-policy machinery is not modeled. |
| `document.head` / `document.body` | Works | Returns the first `head` / `body` element wrapper or `null`. Both are read-only in V0 and do not imply resource loading or live document collections. |
| `document.title` / `document.dir` / `document.readyState` / `document.defaultView` / `document.hasFocus()` | Works | `document.title` reads/writes the first `title` element text. `document.dir` reflects the document direction attribute on the `html` element, with body/document fallback for simplified documents. `document.readyState` is always `complete` after JellyFrame binds the package document; browser loading/intermediate states are not modeled. `document.defaultView` returns the bound JellyFrame `window`. `document.hasFocus()` follows the embedded lifecycle and returns false when host state marks the document hidden. |
| Document collections | Subset | `document.images`, `embeds`, `plugins`, `links`, `forms`, `scripts` and `getElementsByName()` return static array snapshots. They are not live HTMLCollections and do not expose named lookup or browser collection methods. `links` includes `a`/`area` elements with `href`; `plugins` follows the same embed snapshot as `embeds`. |
| `document.getElementById` | Works | Returns wrapper or `null`. |
| `document.createElement` | Works | Creates a detached element owned by the runtime until it is attached. Creation is bounded both by `HostBudgets::max_detached_dom_nodes` and the aggregate DOM node/depth/attribute/string ledger. |
| `document.createTextNode` | Works | Creates a detached text node under the same detached-root and aggregate DOM budget. |
| `appendChild` / `append` / `prepend` / `removeChild` | Subset | `appendChild` moves one node and returns it. `append(...items)` / `prepend(...items)` accept existing runtime-owned nodes and scalar values converted to text nodes, preserve argument order, prevent cycles and mark dirty. Their text additions and depth changes are preflighted before mutation, so an over-budget multi-value call leaves the tree unchanged. They do not accept `DocumentFragment`, and `replaceChildren()` remains deferred because its multi-wrapper lifetime and detached-budget semantics require a separate transaction design. `removeChild` keeps the returned node runtime-owned and reusable while detached. |
| `setAttribute` / `getAttribute` / `removeAttribute` / `hasAttribute` / `toggleAttribute` | Works / subset | Attribute names are lowercased by binding. New/replaced values are checked against the aggregate retained DOM-string budget and per-element attribute cap before mutation. `toggleAttribute(name[, force])` uses normal boolean-attribute-style add/remove behavior and returns presence; empty names raise `TypeError` rather than browser `InvalidCharacterError`. |
| `Node.remove()` | Works | Removes an attached node through the same runtime-owned detached-node budget as `removeChild`; a detached/root node is a no-op. |
| `textContent` / `innerText` | Works / Subset | `textContent` getter/setter; unchanged text avoids dirty work. Replacement preflights the node and retained-string budgets, so rejection preserves the old subtree. A sole existing text child is updated in place; replacing mixed children remains structural. `innerText` is exposed as a lightweight `textContent` alias on element wrappers; it does not run the browser layout-aware rendered-text algorithm. |
| `id` | Works | Reflected to the `id` attribute and uses the normal style/layout dirty path. |
| `className` | Works | Reflected to the `class` attribute and uses the normal style/layout dirty path. |
| `title` / `lang` / `dir` | Works | Reflected string attributes on element wrappers. `lang` and `dir` reflection does not imply full language-specific shaping or bidirectional layout. |
| Element-specific reflected attributes | Subset | Common low-cost per-element IDL reflections are available where they are plain content attributes: `meta.name/content/httpEquiv/media`, `data.value`, `time.dateTime`, `img.alt`, `label.htmlFor`, anchor `text/download/ping/rel`, plus anchor `referrerPolicy` as string reflection without browser policy normalization. `label.control` resolves `for=id` or the first labelable descendant on demand. Navigation, resource loading, image decoding, ping delivery, Shadow DOM/custom-element label association and full URL utilities remain absent. |
| `classList` | Subset | Minimal DOMTokenList-like helper for `contains(token)`, `add(...tokens)`, `remove(...tokens)`, `toggle(token[, force])` and `replace(oldToken, newToken)`. Tokens with whitespace are ignored instead of throwing; invalid `replace()` returns `false`. The helper reflects to `class` and uses the normal dirty path; iteration and full DOMTokenList exception semantics are deferred. |
| `children` / `parentElement` | Subset | Snapshot element-child array and parent wrapper/null. |
| `matches` / `closest` | Subset | Simple tag, `.class`, `#id`, `[attr]` and `[attr=value]` selectors. No combinators. |
| `dataset` | Bounded subset | Existing `data-*` attributes read as camelCase properties. The writable `DOMStringMap` subset accepts ASCII identifier-like camelCase keys through `dataset[key] = value` and `delete dataset[key]`, reflects to the normal attribute dirty path, limits keys to 48 bytes, values to 256 bytes and `data-*` attributes to 64 per element. It does not provide iteration, arbitrary keys or full DOMStringMap/prototype semantics. |
| `getBoundingClientRect()` | Frame-snapshot subset | Returns a new numeric `{x,y,width,height,top,right,bottom,left}` object from the last completed host layout frame after the element requested a measurement. The snapshot is client-relative; the Win32 shell applies root page scroll, while nested scroll and transform geometry remain deferred. It never retains a `LayoutBox*`, does not force synchronous layout and is not a live DOMRect. The runtime keeps at most 32 requested element snapshots; an unavailable/first-frame snapshot is a zero rectangle. |
| `element.style` | Subset | Mutable inline style object for common safe CSS properties: `display`, `color`, `background*`, `textAlign`, `textTransform`, `fontSize`, `fontWeight`, `lineHeight`, size/min/max size, `boxSizing`, margin/padding shorthands and sides, `opacity`, `transform`, `borderRadius`, inset/position, `visibility`, `whiteSpace`, `textOverflow`, `overflow`, `overflowY` and `zIndex`. `style.getPropertyValue(name)`, `style.setProperty(name, value)` and `style.removeProperty(name)` accept the same safe CSS property subset plus CSS custom properties such as `--progress`. |
| `hidden` / `disabled` / `open` / `autofocus` / `tabIndex` properties | Subset | Boolean reflection for `hidden`, `disabled`, `open` and `autofocus`, plus integer `tabIndex` reflection. `hidden` supplies the default `display:none` behavior, which normal authored CSS may override; when it removes a node from the rebuilt layer tree, hit testing and restored hover/active/focus state are cleared with it. CSS `visibility:hidden` gets the same restored-interaction cleanup while preserving layout. Disabled form controls do not activate or accept text input; `open` reflects details disclosure and non-modal dialog visibility. `autofocus` is consumed when a host creates an `InputController`, and `tabIndex` affects only the bounded hardware focus order. `HTMLElement.focus()` / `blur()` remain deferred: focus ownership stays host-owned until a port supplies a lifetime-safe adapter across layer-tree rebuilds. |
| `HTMLElement.click()` | Subset | Dispatches a synthetic mouse-like `click` with zero coordinates. For JellyFrame controls it also runs the existing bounded activation path: checkbox/radio state changes emit `input`/`change`; in `forms.advanced`, a single-select click opens/closes the option overlay without committing a value; uncanceled `summary.click()` toggles parent `details`, and an uncanceled form submit button invokes the bounded form submit path. It does not implement browser navigation. |
| `addEventListener` / `removeEventListener` | Works | JS callbacks are bridged to core event dispatch. |
| `on*` event handler properties | Subset | Function-valued handler properties are supported only for events JellyFrame actually dispatches: `onclick`, `oninput`, `onchange`, `ontoggle`, `oncancel`, `onclose`, mouse/wheel handlers, `onfocus`/`onblur`, `document.onvisibilitychange`, `window.ononline`/`window.onoffline`/`window.onhashchange`/`window.onpopstate`, and wearable aliases `onpointerdown`/`onpointerup`/`ontouchstart`/`ontouchend`. Setting `null` or a non-function clears the handler. These handlers share the normal listener budget and cleanup path. Full `GlobalEventHandlers`, inline HTML event handler attributes and browser event-handler compilation semantics are not implemented. |
| Event object | Subset | `type`, `target`, `currentTarget`, phase, cancel/propagation APIs and mouse/wheel fields. Form `submit` events additionally expose a `submitter` wrapper. |
| Form properties / `FormData` | Subset | Common IDL properties on relevant controls: `value`, `defaultValue`, input `defaultChecked`, `type`, `name`, `placeholder`, `required`, `checked`, `selectedIndex`, `readOnly`, `maxLength`, `minLength`, `min`, `max`, `step`, `willValidate`, `validationMessage`, `validity`, textarea `rows`/`cols`/`wrap`/`textLength`, select `size`, option `label`/`defaultSelected`/`value`/`text`/`index`, optgroup `label`, and progress/meter numeric `value`/`min`/`max` plus meter `low`/`high`/`optimum` and progress `position`. Controls provide `checkValidity()`, `reportValidity()` and `setCustomValidity(message)`; form wrappers additionally provide `requestSubmit()` and cancellable `reset()`. `new FormData(form)` and `append`/`set`/`delete`/`get`/`getAll`/`has`/`forEach` cover string form entries. Every `FormData` is bounded during both core collection and JS mutation by runtime entry and total name+value-byte budgets: 32 entries / 4096 bytes by default. Exceeding either throws `RangeError` and leaves no partial collection result. `forEach` passes `(value, name, formData)` in entry order and uses a bounded entry snapshot, so entries added by its callback wait for the next call. The `ValidityState` snapshot intentionally contains only the implemented flags; labels collections, selection APIs, picker UI, file entries, iterator methods and browser form navigation are deferred. |
| Timers | Works | Host-pumped `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`; callback budget controlled by host. |
| Script execution watchdog | Host/runtime optional | `JerryScriptRuntimeOptions::max_execution_check_count` and `HostBudgets::max_script_execution_checks` can interrupt runaway evals and callbacks with `script execution budget exceeded` when the linked JerryScript library was built with `JERRY_VM_HALT=ON`. If that JerryScript feature is absent, JellyFrame reports the watchdog as unsupported and does not fake preemption. The Win32 validation shell can require this path with `--require-script-watchdog` plus bounded check options for recovery smoke tests. |
| `btoa` / `atob` | Partial | Base64 helpers are exposed on `window` and the global object in bound document runtimes. `btoa` accepts HTML binary strings and rejects code points above 255; `atob` ignores ASCII whitespace, tolerates missing padding and rejects malformed input. Errors currently use JellyFrame `TypeError`, not DOMException `InvalidCharacterError`. |
| Promise/microtask queue | Deferred | Do not rely on browser task semantics. |
| App-local route fragment / history | Subset | `window.location.hash` / global `location.hash` store one route fragment for the current running app. Changed fragments dispatch non-cancelable `hashchange` on `window`; `history.length`, `back()`, `forward()`, `go(delta)`, `pushState(state, title, url)` and `replaceState(state, title, url)` retain at most `max_route_history_entries` fragment entries. History URL arguments must be empty or `#fragment`; `state`/`title` are accepted but not retained, and traversal dispatches `popstate` then `hashchange` when the fragment changes. `window.onhashchange` / `window.onpopstate` share the normal listener budget. The fragment and history reset when a new document binds. URL loading, `Location.assign/replace/reload`, `history.state`, reload-on-`go(0)`, browser navigation, browsing contexts and cross-app routes are absent. |
| Modules/import | Package-time subset | One external `type="module"` entry can import a bounded acyclic graph of package-local `.js` files. The packager rewrites the entry HTML and emits one classic bundle; source modules are not retained in the final resources. Named/default/namespace imports, named/default exports and side-effect imports are supported. Inline module scripts, multiple module entries, cycles, re-exports, `export *`, remote/non-JS paths, `modulepreload` and dynamic `import()` remain deferred. |
| `querySelector` / `querySelectorAll` | Subset | Supports simple selectors on document/element: tag, `.class`, `#id`, `[attr]`, `[attr=value]` and same-compound combinations such as `button.primary`. Results are static wrapper/array snapshots, not live NodeLists. Descendant/child/sibling combinators, commas, pseudo-classes, `:has()` and the full CSS selector API are unsupported; complex string literals are reported as `script-api-subset` in package reports. |
| `innerHTML` | Deferred | Use DOM creation APIs. |
| XHR/fetch/storage | Partial | Scripting builds support async `XMLHttpRequest` GET V0 and a tiny `localStorage` subset when a non-blocking `AppLocalStorageShadow` is bound. `fetch()` waits for bounded Promise/microtask support. |
| Pipeline diagnostics | Started | HTML tokenizer/parser, CSS parser, style resolver, render tree, layout, layer tree, script collection, package/resource loading and software renderer report caps, skipped input, ignored declarations, load failures and degradation through an optional sink used by desktop tools. `jellyframe_pseudo_browser --diagnostics-json` emits the structured report, and `jellyframe_cli.py check`/`preview`/`package` merge it into `pipelineDiagnostics`. Development-time visual diagnostics also report horizontal overflow (`visual-horizontal-overflow`), vertical paint overflow (`visual-vertical-paint-overflow`), page scroll-needed content (`visual-scroll-needed`), internal scroll containers with clipped content (`visual-scroll-container`) and high display-command density (`visual-display-command-density`). Common layout diagnostics expose parsed app-author fields when available, including text snippets, compact node labels, selector-like DOM paths, measured/available text widths, scroll container heights/overflow, paint bounds, viewport and overflow pixels. Horizontal and vertical overflow diagnostics include likely layout-box node/path and box overflow metrics when attribution is safe. Package reports also include `htmlApiDiagnostics`, a static HTML scan for browser-only tags, form submission and high-risk partial markup including responsive image selection, tables, ruby, templates, media and rich-text editing. `scriptApiDiagnostics` scans classic/inline scripts for manifest mismatches plus deferred/subset APIs such as `fetch()`, `Promise`, complex `querySelector`, `innerHTML`, `getBoundingClientRect()`, pointer capture, dynamic `import()`, `WebSocket`, `EventSource`, browser/message channels, Workers, session storage/cookies, browser navigation and Selection/Range. CLI-authored reports derive `developerAdvice[]`, an app-author explanation/action layer over warnings, pipeline diagnostics, responsive profiles and font diagnostics; known crosswork diagnostics name a concrete package-local, Canvas, app-route, host-service or system-component replacement. Structured text/scroll/paint overflow diagnostics are converted into advice that names the likely element/text and pixel overflow. Target-specific advice carries `targetViewport` and structured `targetGate` decision/reasons; font advice carries missing-codepoint samples, target profile and unmatched CSS family context when available. Unknown or unclassified recovery keeps the diagnostic code, stage/source and triggering detail so the app author can locate the field or snippet. |
| Performance preflight report | Tool-only | CLI-authored reports derive `performanceSummary` and optional `performanceAdvice[]` from package resources, responsive profiles and pseudo-browser pipeline statistics. The summary reports an `ok`/`watch`/`high-risk` rating, object counts, layer/display-command counts, framebuffer bytes, estimated pipeline heap, resource budget ratio, full-frame present scale, desktop tool-side stage timings when available, and a short `bottlenecks[]` list for the most likely causes of slow UI. This is preflight attribution, not device FPS; `--runtime-log` can merge Win32 frame-script/capture counters, and `--port-telemetry` can merge real port frame ms, DMA wait, flush-done time and internal-RAM peaks into `runtimeMetrics` / `portTelemetry` plus measured performance-summary fields. |
| Responsive profile report | Tool-only | `jellyframe_cli.py check`/`preview`/`package`/`install` can explicitly pass `--targets a,b` or `--all-targets` to run the render-core pseudo browser once per target preset and write `responsiveProfiles[]` into the report. Each profile records viewport, shape, content height, horizontal overflow, scroll need and diagnostic counts. Manifest `targets[id].gate` can declare release-time accept/warn/reject gates such as minimum viewport, whether scroll is allowed, whether horizontal overflow is allowed and warning/error caps; `reject` makes the CLI fail. Normal single-target commands do not emit this field or render extra viewports. This is pre-release adaptation validation, not a full browser-grade responsive/layout engine. |
| Font resource checker | Tool-only | `jellyframe_font_resource_check` is currently retained for deterministic font work: emit non-ASCII used characters, estimate bitmap font budgets and verify embedded font coverage. |

## Rendering And Pixels

| Feature | Status | Behavior |
| --- | --- | --- |
| Display list | Works | Rectangles, borders, gradients, text and image-surface-handle commands, including approximate text weight. Canvas output is integrated through the same bounded image command path. |
| CPU framebuffer | Works | Software rasterizer/compositor can produce BMP/PPM. Budgeted compositor renders reject oversized primary framebuffers before allocation. |
| Embedded framebuffer adapter | Works | `embedded_framebuffer` converts `HostFrameBufferView` into caller-owned RGBA8888/BGRA8888, RGB565/BGR565, RGB332, Gray8 or 1-bit monochrome buffers and flushes dirty rects through a callback. `EmbeddedPackedRgb565Sink` additionally converts each dirty rect directly into a compact native RGB565/BGR565 word buffer for synchronous panel paths; it is an opt-in presentation contract, not a performance guarantee, and each port must A/B test it against the linear framebuffer path. RGB565/BGR565 targets may enable 4x4 ordered dithering, and optional `EmbeddedFrameBufferPresentStats` reports converted pixels, packed bytes, clipped/empty rects and flush count for board bring-up. |
| Source-over alpha | Works | Straight-alpha composition. |
| Opacity layers | Subset | Offscreen compositing for opacity/composited layers. Embedded hosts can cap offscreen pixels; oversized layers degrade to direct per-command opacity instead of allocating a large temporary buffer. The same limit also covers clipped text/image temporary surfaces: rejected text is skipped and image paint uses a clipped placeholder with `paint-transient-surface-budget`. |
| Rounded fills | Subset | Rounded rectangle fill clipping for backgrounds/shadows. Rounded fill/stroke/gradient edges use local coverage antialiasing, while ordinary opaque square rectangles keep the fast fill path. |
| Border painting | Works | Borders emitted as fill rectangles. |
| Render Core flex-grid profile | Build-time optional family | `css.flex-grid` gates the simplified flex/grid parser, computed-style fields, layout passes and flex order paint sorting as one vertical family. When disabled, flex/grid declarations and matching `@supports` conditions are rejected, layout falls back to block/inline, and no flex/grid-specific test or sample is required. The generated profile names every Canvas/modern-paint/flex-grid/advanced-forms combination deterministically; port link, flash/RAM and startup measurements remain required before enabling it in a constrained firmware. |
| Render Core modern-paint profile | Build-time optional family | `css.modern-paint` gates the bounded gradient and shadow raster paths as one vertical family. The generated profile and desktop link map must agree. When disabled, no `modern_paint.cpp` object is linked; gradients use the first-color/solid fallback and shadows emit no dedicated command. Apps cannot load native feature modules. |
| Render Core advanced-forms profile | Build-time optional family | `forms.advanced` gates local constraint validation, custom validity, bounded `FormData`, `SubmitEvent`, `requestSubmit()` and cancellable form reset/default actions. When disabled, base controls still accept input, but form activation does not submit/reset and scripting omits `FormData`, `validity`, `checkValidity()`, `requestSubmit()` and `reset()`. The public C++ API remains safely linkable through no-op stubs; Apps cannot enable the family at runtime. |
| Linear gradient | Subset / profile-gated | With `css.modern-paint`, two-color horizontal, vertical and bounded diagonal commands are supported. The opaque square-corner wearable path has a dedicated fast path; p50/p95 raster timing is reported by `jellyframe_render_core_microbench`, not presented as device FPS. |
| Conic gradient | Subset / profile-gated | With `css.modern-paint`, two-segment clockwise progress commands start at 12 o'clock and are rasterized only when used; ordinary rectangles and linear gradients keep their existing fast paths. Stops must cover `0%..100%` contiguously; out-of-range values are diagnosed instead of silently clamped. |
| Radial gradient | Subset / profile-gated | With `css.modern-paint`, two-color center-circle radial gradients are intended for small highlights, gel cards and watch-face glows; focal points, ellipses, multiple stops, repeating gradients and multiple backgrounds are deferred. Large paint areas emit `layer-radial-gradient-area-budget`. |
| Text | Subset | Core fallback is tiny ASCII bitmap painting with UTF-8 placeholder glyphs. Win32 shell injects GDI for UTF-8/Chinese validation. |
| Chinese text | Shell-dependent | Use Win32 shell or future platform text backend. Pseudo-browser fallback will show placeholder glyphs. |
| Images | Host-optional/package BMP V0 plus codec adapter shape | Platform-neutral `ImageDecodeMock`, `AppImageSurfaceCache`, `Surface` handle lifetime and width/height/decoded-byte/pending budgets now exist. Render core supports `ImageHandleResolver`, image display commands and `ImagePainter`; pages should use package-local standard paths such as `<img src="/assets/icon.bmp">` or relative URLs. The Win32 shell can load uncompressed 24/32-bit BMP resources from `.jfapp`/source packages as the in-bundle image V0 path; its private debug fixtures are shell validation internals, not app syntax. `AppImageCodecAdapter` now defines the production decoder boundary for PNG/JPEG/WebP/vendor codecs, and `app_image_codec_result_within_policy(...)` validates decoded surfaces before they become handles. `AppImageSurfaceCache` can evict LRU ready surfaces by surface-count and decoded-byte budgets while protecting current display-list references; stale completions are rejected and stale ready entries can be dropped/reported during eviction. Image commands carry the `object-fit`, simple `object-position` and `image-rendering` subsets. The `auto` path uses bilinear scaling, while `pixelated`/`crisp-edges` keep hard-edge sampling. Package reports include `imageDiagnostics`, which classifies package image codecs, reads lightweight BMP/PNG metadata and compares resources with target `hostServices.imageDecode` / `imageCodecs` declarations. Image request rejections and completion failures are classified into stable reasons such as `capability-denied`, `resource-not-found`, `decode-budget-exceeded` and `surface-budget-exceeded`; `diagnostic_detail_for_url(...)` exposes stable `src`/`state`/`reason`/`submit` plus optional host/job/handle/byte fields for desktop and port logs. Real PNG/JPEG/WebP decoders and complex position syntax remain port/future work. |
| Static SVG icon build | Package-time subset | `package_app.py --rasterize-svg` compiles statically referenced icon SVG sources into generated 32-bit BMP paths and rewrites HTML/CSS references before packaging. The source SVG is excluded from the package and no runtime SVG parser is linked. Accepted input is `svg`/`g`, path commands except arcs, basic shapes, flat fill/stroke colors, unitless/px sizing and `viewBox`; scripts, external resources, transforms, text, filters, gradients and dynamic JS references fail with source-specific advice. `staticSvgRasterization` records source/output paths, dimensions and shape count in the package report. |
| Audio playback | Host-optional/runtime mock | Core does not own PCM/I2S/codecs. `app_runtime` includes `AudioCommandMock` for open/play/pause/stop/close/setVolume, `AudioStream` handle lifetime, ended/error completions and stream budget checks. Audio request rejections and completion failures classify into stable reasons such as `capability-denied`, `invalid-source`, `source-not-found`, `invalid-handle`, `stream-budget-exceeded`, `command-timeout` and `command-cancelled`; `app_audio_failure_detail(...)` exposes stable `source`/`reason`/`submit`/`host`/`error` fields. JerryScript builds expose a tiny host-optional standards-shaped `Audio` V0: `new Audio(src)`, `src`, `volume`, `play()`, no-op `pause()`, `onended`/`onerror` and `addEventListener`/`removeEventListener` for `ended` and `error`. The Win32 shell binds this to package/local audio resources, dispatches `ended` after its debug playback estimate and still provides `--audio-smoke` for local files or package paths such as `/audio/tone.wav`. `media.audio.playback` can be declared in manifests, but real MCU MP3/I2S playback remains host/port work. |
| Background services | Manifest intent/runtime policy | `backgroundServices` in `jellyframe.app.json` declares whether network, audio, sensors or location want to continue while suspended, while the screen is off or, for sensors/location, in low-power mode. This does not grant permission by itself. `AppBackgroundServicePolicy` plus host profile/system state produce `AppServiceActivityPolicy`, so foreground apps run normally, non-approved background work pauses, audio can be paused, sensors can be throttled and location snapshots can be delayed without touching render core. |
| Target service support | Tooling only | Target presets may describe optional services with `hostServices.networkFetch`, `storageKv`, `audioPlayback`, `imageDecode`, `imageCodecs`, semantic sensors and location fields. `serviceIntent.targetSupport` reports requested service support as `supported`/`unsupported`/`unknown`, while `imageDiagnostics` reports image codec support for package-local image resources. These are developer compatibility signals, not permission grants. |
| Host service workers | Platform-neutral pump | `pump_app_host_service_worker(...)` provides a tiny worker boundary for real host services. It processes one `HostServiceJobKind`, preserves request identity on completions and refuses to pop when the UI completion queue is full. If the queue fills after a request is worker-owned, the bounded in-flight slot retains its completion until a UI frame can deliver it, so the result is never silently lost. `HostHandleTable` operations are synchronized and expose copy-out lookup only; workers must not retain table pointers. DOM/JS/layout/framebuffer ownership remains on the UI task. The helper does not create threads or perform I/O. |
| Lightweight video/MJPEG/H.264 | Experimental/host-optional contract | `media.video.frame` provides a bounded latest-frame path for MJPEG and explicitly enabled H.264 baseline. It keeps one request per source, preserves the prior frame when a replacement cannot allocate, and drops stale frames after replacement. `<video>` remains unsupported; the ESP32-S3 H.264 retest remains experimental and is not a default profile capability. |
| SVG | Package-time icon subset; runtime deferred | There is no SVG renderer or SVG DOM. `package --rasterize-svg` can turn one static package-local icon source into an ordinary generated BMP when it meets the documented restricted input vocabulary; invalid or dynamic SVG fails with source-specific repair advice. Use DOM/CSS boxes, images or optional Canvas 2D V0.4 for custom charts. |
| Real shadow blur | Deferred | `box-shadow` blur is approximated. |
| Filters/blend modes | Deferred | Only normal source-over. |
| GPU compositing | Deferred | Current renderer is CPU-only; layer model leaves room for hardware backends. |

## Dirty Work And Rerendering

| Mechanism | Status | Behavior |
| --- | --- | --- |
| Dirty propagation | Works | Mutations OR dirty bits onto the changed node and ancestors. |
| Dirty check | Works | Root check is O(1) because ancestor propagation keeps aggregate bits. |
| Dirty clear | Works | Skips clean branches. |
| Host coalescing | Subset | Win32 shell rerenders only after dirty input/script callbacks. Viewport scroll in the Win32 shell reuses the existing full-content framebuffer and updates the visible blit buffer by moving rows plus copying only newly exposed rows when possible. Ports may opt into `coalesce_dirty_rects_into(...)` to trade a bounded amount of extra painted area for fewer generic present calls; the port supplies the cost policy and unused apps do not pay for it. |
| Incremental style/layout | Subset | Paint-only form-control state changes can reuse render/layout in the Win32 validation shell and rebuild only layer/display commands. A guarded same-box single-line text path can also reuse render/layout when the updated text measures to the existing layout box. A guarded style/class path reuses layout when the render tree shape and all layout-affecting style fields stay unchanged; paint/compositor changes such as color, background, opacity and transform can use this path. Transform changes reuse the animation invalidation helper so old and new bounds are repainted. Wrapping text, layout-affecting style, unknown structural changes and tree changes still rebuild render/layout. |
| Dirty rectangle repaint | Subset | `dirty_region` computes bounded repaint rects for direct text/attribute/form-control paint changes by comparing old and new layout boxes, or by reusing the same layout for paint-only changes. A transient core-rendered overlay (for example the `forms.advanced` select popup) is the explicit exception to the tree-mutation fallback: hosts pass both the previous and current `LayerNode` trees, and the core unions the old and new `LayerReasonTransientOverlay` bounds with the control bounds. This covers both opening and closing without requiring a full-frame repaint. Actual tree mutations still conservatively repaint the viewport. Hosts may also choose full-frame repaint when estimated dirty area is too large for partial flush to pay off. The software compositor drops duplicate or fully contained dirty rectangles before replaying clips, avoiding repeated clear/repaint work for the same area. |
| Animation invalidation | Subset | `animation_invalidation` uses previous/current animation style overrides and the current layout tree to produce local dirty rectangles for opacity/color paint-only animation and translate/scale/rotate transform before/after bounds. Opacity-only frames whose target already owns a layer may reuse that layer's display list; all other animation updates retain the conservative layer rebuild path. |
| Display invalidation diagnostics | Works | `analyze_display_invalidation(...)` reports dirty-rectangle coverage over layers and display commands. Frame planning can reuse existing frame/layout state for paint-only and stable-layout changes; full retained display-list diffing is still deferred. |
| Frame dirty diagnostics | Works | Win32 scripted capture reports the per-run dirty flag distribution (`tree`, `attributes`, `text`, `style`, `layout`, `paint`, `overlay`, `render_or_layout`), frame-update reasons such as `text_stable`, `style_stable` and `overlay_dirty`, and final `layer_tree layers=N display_commands=N` counts. Use this to find whether an app is spending frames on layout-producing DOM changes, cheap paint-only updates, transient overlays or display-command-heavy pages. |
| Per-app budget snapshot | Works | `AppBudgetSnapshot` reports the active app instance, role/state, host service queues, host handles/bytes, app fonts, system events, localStorage-shadow counters, frame-loop callback caps, active animations and script timer/listener/detached-node counts when the host supplies them. Win32 frame capture prints this summary; MCU ports can sample the same counter-only structure for serial diagnostics or recovery decisions. |
| Budget recovery | Works V0 | `app_budget_recovery_for_snapshot(...)` classifies exhausted runtime budgets into `none`, `warn` or `terminate-app`. Queue/handle/font/system-event/script-object exhaustion is treated as `budget-exceeded` app recovery; frame callback and active-animation caps are warnings because frame policy can throttle them. Win32 system-shell mode reports `budget_recovery_teardown reason=budget-exceeded` and returns to the launcher for the validation fixture. |
| Host frame sink | Subset | `present_frame` exposes `FrameBuffer` through `HostFrameSink` with optional dirty rects. A successful `present` is the frame-buffer reuse boundary: asynchronous panel/DMA hosts must wait, copy into driver-owned memory or gate the next render until flush completion. `embedded_framebuffer` supplies portable pixel conversion; real display I/O remains host-owned. |
| Host device capabilities | Draft | `HostDeviceCapabilities` records display, input, memory, budget and service flags for board ports. Current core code treats it as a contract/documented policy input; deeper automatic adaptation is deferred. |
| Device Runtime / JFDP | Reference implementation | `DeviceInstallTransaction`, `DeviceCapabilitySnapshot` and `JFDP/1` framing provide a bounded, platform-neutral install transaction, capability-handshake data, stable result codes and message integrity checks. CLI `device --transport reference` operates only on the desktop registry reference endpoint and does not claim that a physical board is connected. Discovery, flash/storage, display/touch, logs and telemetry remain official-port responsibilities. |
| Host budgets | Subset | `HostBudgets` feeds HTML/CSS parsing, render/layout/layer tree caps, display-list caps, dirty-rect count, frame-loop input/timer/animation caps, and JerryScript timer/rAF/listener limits. Script DOM mutation shares `max_dom_nodes`, `max_dom_depth`, `max_attributes_per_element` and `max_resource_bytes` as its attached-plus-detached retained-string cap, in addition to `max_detached_dom_nodes`; apps that do not enable scripting pay no runtime cost. Render, layout and layer trees now have arena-backed build paths; full mutable DOM arenas remain future work. |
| Frame scratch | Works | `FrameScratch` reuses dirty-region bounds, dirty rectangles and animation overrides; `AppFrameScratch` reuses host-completion batch/accepted lists. Regular frames clear and reuse storage, while screen-off, app switches and memory pressure can call `release()`. Real DMA/panel buffers remain port-owned. |

Practical implication: repeated script mutations should be batched in one event
or timer callback. The host will see one dirty document and rerender once.
Paint-only form-control changes can avoid render/layout rebuilds in the Win32
validation shell; other changes still run a simplified pipeline pass, while the
framebuffer stage can repaint bounded dirty rectangles for non-structural
changes.

## Recommended Authoring Rules

- Prefer stable IDs for interactive nodes.
- Use simple selectors and class-based styling.
- Use the supported grid-card subset for dashboards.
- Use `button`, `input`, `select`, `textarea`, `progress`, `meter` instead of
  custom canvas widgets.
- Provide classic CSS fallbacks before unsupported modern values:
  `color: #334155; color: oklch(...);`.
- Avoid required UI inside `@container`, unsupported/complex `@supports`,
  unsupported/complex media queries or unsupported selector functions such as
  `:has()`.
- Keep scripts synchronous and small. Use host-provided data.
- Treat Win32 shell rendering as the desktop validation path for Chinese text,
  because it injects both measurement and painting through the text backend APIs.
- Keep pages small and bounded; parser limits exist by design.

## Current Hard Limits

- CSS parser: `max_rules` 4096, `max_declarations_per_rule` 256,
  `max_nesting_depth` 8, default media viewport 360x240.
- Default host budgets cap DOM nodes, render objects, layout boxes, layers,
  display commands, dirty rects, timers, listeners and framebuffer pixels.
- Diagnostic/example input files are capped by the tool, usually 512 KiB or
  1 MiB depending on the shell.
- Grid auto columns are bounded internally for embedded memory predictability.
- Script runtime currently assumes one active JerryScript runtime in this build.

## When To Add A Feature

Add a feature when it is:

- Common in embedded-app authoring.
- Cheap to compute with integer or bounded algorithms.
- Easy to degrade coherently.
- Useful without requiring network, GPU, font loading or large browser services.

Defer a feature when it:

- Creates style/layout feedback cycles (`@container`).
- Needs large external subsystems (full image decode, font shaping, unbounded canvas).
- Requires a complete browser task/loading model.
- Would make unsupported modern styling look half-correct and incoherent.
