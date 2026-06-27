# Developer Capability Matrix


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

## Best Fit

JellyFrame works best for:

- Weather, clock, timer, calculator and settings apps.
- Small dashboards with cards, text, form controls and host-provided data.
- Local embedded applications that want HTML/CSS/JS authoring instead of canvas
  drawing.
- Desktop validation through `jellyframe_pseudo_browser` or
  `jellyframe_win32_browser`.

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
| Embedded backend boundary | Port-owned subset | The platform-neutral engine exposes framebuffer conversion, dirty rects, frame sink boundaries, input-controller hooks, text callbacks and host-service pumps. Real display flush, touch/key drivers, panel DMA, sleep/wake and storage/network/audio/sensor devices remain port-owned. |
| Linked CSS loading | Shell-only | Example tools can load local `<link rel="stylesheet">`; core exposes callback-style helpers only. |
| Network loading | Host-optional XHR V0 | Core still has no HTTP, WebSocket or remote resource loading, and remote HTML/CSS/script/image resources cannot enter the page loader. `NetworkFetchMock` provides a fixture/handle/completion contract; `JerryScriptRuntime` exposes an async `XMLHttpRequest` GET subset in scripting builds. Network rejection/completion failures classify into stable diagnostics such as `capability-denied`, `invalid-url`, `resource-not-found`, `offline`, `response-budget-exceeded`, `response-handle-budget-exceeded`, `request-timeout` and `request-cancelled`. Real networking belongs to host services/workers, and JS callbacks run only after UI/main task completion pumping. |
| Storage | Host-optional localStorage V0 | No cookies, IndexedDB or filesystem API in core. `AppPrivateKvStorageMock` provides app-id-isolated async KV semantics and budget checks. `JerryScriptRuntime` exposes a tiny `localStorage` subset when the host binds a non-blocking `AppLocalStorageShadow`; it is absent when no shadow is bound. Storage failures classify into stable diagnostics such as `capability-denied`, `invalid-key`, `value-budget`, `quota-exceeded`, `not-found`, `handle-budget-exceeded`, `operation-timeout` and `operation-cancelled`. `AppStorageLifecyclePolicy` and `apply_app_storage_lifecycle(...)` define when hosts flush, drop, delete or retain pending/persistent app storage during suspend, exit, crash, uninstall, update and memory pressure, with stable system-shell diagnostics such as `storage-flush-ok`, `storage-flush-failed`, `storage-drop-pending`, `storage-delete-data` and `storage-retain-data`. |
| Authorized file access | Planned host broker | Ordinary apps have no general filesystem access and cannot affect runtime files, system components or another app's data. Future system components, file managers or user-approved apps should go through a host-owned file broker with manifest/profile capability gates, bounded async byte operations, normalized paths, stable errors and rollback/fallback behavior that does not require reflashing firmware. Apps still must not receive raw filesystem, flash partition or block-device handles. |
| System status events | Host-optional V0 queue | `AppSystemEventQueue` lets the host inject bounded time/timezone/network/battery/screen/low-power snapshots for the current app instance. Stale-instance events are dropped at frame boundaries; `try_push_current(...)` can diagnose `empty-instance` / `queue-full`. JerryScript V0 maps `navigator.onLine`, the `window` `online`/`offline` event subset, `document.hidden`, `document.visibilityState` and `visibilitychange`. |
| Sensors/location | Host-optional semantic service V0 | Apps may declare `sensor.accelerometer`, `sensor.gyroscope`, `sensor.heart-rate`, `sensor.ambient-light` or `location.position` in the manifest. Platform-neutral `AppSensorSampleMock` / `AppLocationSnapshotMock` policies are enabled only when the host/profile allows the same capability. Results return to the UI task through bounded request/completion queues and host handles; apps never receive raw GPIO/I2C/SPI/BLE/GPS handles. `JerryScriptRuntime` exposes the `navigator.geolocation.getCurrentPosition(success, error)` subset when a location service is bound; sensor JS APIs are still deferred. Failures classify into diagnostics such as `capability-denied`, `sample-unavailable`, `record-budget-exceeded`, `handle-budget-exceeded` and `request-timeout`. Real sample cadence, background policy and permission prompts belong to the host. |
| App frame policy | Works V0 | `AppFramePolicy` maps foreground/suspended, screen-on and low-power state into input/timer/rAF/present budget policy. Low-power can keep input and timers while stopping animation; screen-off or suspended pauses foreground input, timers, rAF and presentation, and recommends a first repaint after resume. |
| App teardown/recovery | Works V0 | `AppRuntimeHost::terminate_current(reason)` cancels current-app requests, discards completions, releases host handles and clears app font resources with stable reason names such as `user-kill`, `script-watchdog`, `budget-exceeded`, `load-failure` and `system-policy`. |

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
| Malformed markup | Subset | Parser recovers with limits and can report node/depth/attribute budget diagnostics; it should not loop forever or crash. |
| Quirks mode | Deferred | Always ignored. JellyFrame targets modern authored pages. |
| Template contents | Lazy | `template` is hidden by default style; template DOM semantics are not implemented. |
| Custom elements | Subset | Unknown tags create elements and can be styled as ordinary boxes; lifecycle callbacks are absent. |

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
| Font coverage check | Default tooling preflight | `package`, `check`, `preview` and source-package `install` run `jellyframe_font_resource_check` by default; `--font-coverage` reports missing codepoints before embedding, and `--no-font-check` skips it explicitly. Package reports also include `fontDiagnostics`, which merges source codepoints, target font-profile estimates and manifest `.jffont` glyph tables so missing app-visible glyphs are reported before install. |
| Font profile and budget estimate | Default tooling preflight | Defaults to a `16x16` bitmap-font byte estimate; `--font-budget WxH` overrides it and recommends `tiny`, `tiny-plus-symbols`, `app-subset-cn`, `cn-standard` or `global-product` from scanned codepoints. Missing manifest `fonts[].license`, `sizes` or `weights` metadata is reported before release; `budgets.maxAppFonts`, `maxAppFontBytes` and `maxAppFontGlyphs` cap installable `.jffont` count, bytes and glyphs. |
| Bitmap font pack generation | Tool/runtime/fallback chain works | `jellyframe_font_pack_gen` subsets BDF bitmap fonts into C++ `BitmapFont` headers for embedded builds and can also emit `.jffont` V0/V1 binary supplements. V0 is compact 1bpp. V1 stores opt-in 2bpp/4bpp glyph coverage for font-level antialiasing through `--coverage-bits 2|4`; 1bpp fonts keep the compact path and pay no coverage cost. `BitmapFontResource` can parse `.jffont` bytes, and `AppFontSet` exposes a bitmap fallback chain. Generic/no-family text tries the system font profile first, then app supplements for missing glyphs. A CSS `font-family` whose first family matches a manifest `.jffont` family hash tries that app font first, then falls back to the system/default chain. Win32 `--use-app-fonts` validates this layout/paint path. Stable `.jfapp`/flash font payloads can be attached through the zero-copy view path. Package diagnostics match explicit CSS `font-family` declarations against manifest metadata and validate `sizes`/`weights` arrays; multi-size packs and full browser font matching remain future work. |
| `@keyframes` | Subset | Parses named `@keyframes` blocks and stores `from`/`to` or `0%`/`100%` declarations. Intermediate percentages are diagnosed and ignored. Execution is limited to the animation property subset below. |
| Unknown at-rules | Lazy | Statement or balanced block skipped. |
| CSS custom properties | Subset | Direct `var(--token)` and `var(--token, fallback)` are resolved from inherited `:root`/ancestor/current/inline custom property declarations. Unresolved `var()` values do not override earlier supported fallbacks. Full dependency graph, case-sensitive custom names and complete invalid-at-computed-value-time semantics are absent. |
| CSS nesting | Deferred | Do not rely on nested selectors. |
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
| `color` | Subset | Named basics, hex, `rgb()`, `rgba()`. Unsupported color functions such as `oklch()` do not override fallbacks. |
| `background-color` | Subset | Same color parser as `color`; gradients are intentionally not accepted here because CSS treats them as background images. |
| `background` | Subset | Solid colors, `linear-gradient(<color>, <color>)`, `linear-gradient(to bottom/top/right/left, ...)`, and a two-segment progress `conic-gradient(<color> 0% N%, <color> N% 100%)`. Conic gradients are intended for watch rings, battery rings and activity arcs; complex stops/angles/repeating/multiple backgrounds are diagnosed and ignored without clearing earlier fallbacks. Out-of-subset syntax emits `style-conic-gradient-unsupported`, and oversized paint areas emit `layer-conic-gradient-area-budget`. |
| `margin` | Works | 1-4 length values plus horizontal `auto`. |
| `margin-top/right/bottom/left` | Works | Physical longhands. `margin-left/right:auto` works for horizontal centering paths. |
| `padding` | Works | 1-4 length values. |
| `padding-top/right/bottom/left` | Works | Physical longhands. |
| `border` | Subset | Parses `none`, width and color from simple shorthand. Style keyword is tolerated only as ignored text. |
| `border-width` | Works | 1-4 length values. |
| `border-top/right/bottom/left-width` | Works | Physical width longhands. |
| `border-color` | Subset | Single color for all borders. |
| `border-radius` | Subset | Single length radius or single percentage such as `50%`. Rounded fills and borders are supported; the software renderer applies local coverage antialiasing on rounded edges. Complex per-corner radii are not supported. |
| `outline` / `outline-width` / `outline-color` | Subset | Painted as a non-layout outer stroke. Simple width/color shorthand is supported; `outline-offset` and complex style semantics are deferred. |
| `width` / `height` | Works | Length values and percentage values. Percentages resolve against the containing content box; root/full-screen app wrappers can use the actual viewport width/height. |
| `min-width` / `min-height` | Works | Length values and percentage values. |
| `max-width` / `max-height` | Works | Length or percentage value; used by block layout. |
| `aspect-ratio` | Works | Positive number or `w / h`, including `auto w / h`. Used for intrinsic box height. |
| `font-size` | Works | Length values. |
| `font-weight` | Subset | `normal`, `bold`, `bolder`, `lighter` and numeric weights. Software fallback approximates bold; platform text painters decide final glyph weight. |
| `line-height` | Works | Unitless multiplier or length. |
| `text-align` | Works | `left`, `right`, `start`, `end`, `center`. |
| `text-indent` | Works | Length value. |
| `text-decoration` / `text-decoration-line` | Subset | `none`, `underline` and `line-through` paint cheap solid decoration lines. Color/thickness/style variants and wavy/double lines are deferred. |
| `text-shadow` | Subset | First shadow is painted as offset text; blur is parsed for compatibility but not rasterized, and multiple shadows are not painted yet. |
| `box-sizing` | Works | `content-box`, `border-box`. |
| `overflow` | Subset | `visible`, `hidden`, `clip`, `auto`, `scroll`; clipping creates layer boundaries, but native scroll containers are not complete. |
| `white-space` | Subset | `normal` and `nowrap`. `nowrap` is inherited into text layout and prevents cheap wrapping. |
| `text-overflow` | Diagnostic subset | `clip` and `ellipsis` are accepted so app authors can express intent. Current rendering still clips through the box/text backend path; layout diagnostics warn when measured text exceeds the available box. Browser-grade ellipsis shaping is deferred. |
| `opacity` | Subset | 0..1; creates composited layer in software compositor. |
| `position` | Subset | `relative` applies visual offsets without changing normal-flow space. `absolute`/`fixed` boxes are taken out of flow and positioned by simple insets. `sticky` is only stored as a layer hint. |
| `top` / `right` / `bottom` / `left` | Subset | Length and `auto` values. Absolute/fixed boxes use parent content box or viewport-like origin. Percentages, shrink-to-fit, full containing-block rules and sticky scrolling are absent. |
| `z-index` | Subset | Integer or `auto`; layer-local ordering only. |
| `transform` | Subset | `translate()`/`translateX()`/`translateY()`, `scale()`/`scaleX()`/`scaleY()` and `rotate()`/`rotateZ()` are parsed into a composited layer and painted by the software compositor. Angles support `deg`, `turn`, `rad` and `grad`. `transform-origin` supports common keywords and percentages. `skew()`, `matrix()`, perspective and 3D transforms are unsupported and diagnosed/ignored. |
| `justify-content` | Subset | `start`, `flex-start`, `normal`, `center`, `space-around`, `space-between` in simplified flex rows. |
| `align-items` | Subset | `stretch`, `normal`, `start`, `flex-start`, `center`, `end`, `flex-end` in simplified flex rows. |
| `flex` | Subset | Shorthand supports common `none`, `auto`, `<grow>`, `<grow> <basis>` and `<grow> <shrink> <basis>` forms for simplified row flex layouts. Full Flexbox grammar is absent. |
| `flex-grow` / `flex-shrink` / `flex-basis` | Subset | Non-negative numeric grow/shrink factors and supported length/`auto` basis values participate in the simplified row sizing pass. |
| `flex-wrap` | Subset | `wrap`/`wrap-reverse` enable simple row wrapping. Wrapped lines use fixed/basis probing and do not run the full per-line Flexbox algorithm. |
| `gap` | Works | 1-2 length values for grid and simplified flex support. |
| `row-gap` / `column-gap` | Works | Length values. |
| `grid-template-columns` | Subset | Extracts minimum track from `repeat(auto-fit, minmax(<length>, 1fr))`, `minmax(<length>, 1fr)`, a length, or `1fr`. |
| simple fixed grid columns | Subset | `grid-template-columns: <length> 1fr`, `repeat(N, 1fr)`, `repeat(N, minmax(0, 1fr))` and similar 2-4 column length/`fr` templates are supported for definition lists, settings forms and compact keypads. |
| `grid-auto-rows` | Subset | Length or `minmax(<length>, auto)` minimum row height. |
| `grid-column` / `grid-row` | Subset | `span N`, clamped to bounded values. Explicit line placement is absent. |
| `list-style` / `list-style-type` | Subset | `none`, disc-like values and decimal-like values. Native-lite list markers are painted for `li`. |
| `content` on `::before` / `::after` | Subset | Plain text and `counter(name) "suffix"` for lightweight list counters, units and badges. Generated content is painted inside the element box and is budgeted as display commands, not real DOM nodes. Full generated-content layout is deferred. |
| `box-shadow` | Subset | First shadow becomes an approximate rounded translucent fill. Real blur and multiple shadows are not rasterized. |
| `object-fit` / `object-position` | Subset | `object-fit` supports `fill`, `contain`, `cover`, `none` and `scale-down`. `object-position` supports the one/two-value keyword and percentage subset, such as `center`, `right top` and `25% 80%`; complex four-value and length-offset syntax is deferred. |
| `image-rendering` | Subset | Supports the standard keywords `auto`, `pixelated` and `crisp-edges`. `auto` allows host image painters to use bilinear/smooth sampling; `pixelated` and `crisp-edges` keep nearest-neighbor sampling for pixel art. |
| `font-family` | Runtime subset | Parses a comma-separated family list. The first listed custom family is normalized to a small runtime hash; generic families such as `system-ui`/`sans-serif` map to the host/system fallback. With an app-font backend, a manifest `.jffont` whose `family` matches that hash is tried before the normal system-first fallback chain, and layout measurement and paint use the same selection. Full browser cascade, `@font-face`, style/stretch/features and multi-size matching are not implemented. Package diagnostics still report generic families, manifest matches and unmatched primary families. Win32 uses GDI by default; pass `--use-app-fonts` to validate package `.jffont` selection. |
| `requestAnimationFrame` | Scripting subset | Available in JerryScript builds. The host pumps callbacks with a per-frame budget and timestamp. Background/low-power profiles may set the animation callback/FPS budget to zero. The Win32 validation shell exposes these budgets through `--animation-fps`, `--animation-callbacks` and frame-script commands for deterministic low-power checks. |
| CSS `transition` | Subset | Supports bounded `transition` and `transition-*` lists. Current animatable properties are `opacity`, `transform: translate()/scale()/rotate()`, `background-color` and `color`. The Win32 debug shell advances the timeline on interaction state changes and uses animation dirty-region helpers to repaint only previous/current motion or paint bounds; layout-property animation does not reflow every frame. |
| `@keyframes` / `animation-*` | Subset | Supports bounded `animation`, `animation-name`, `animation-duration`, `animation-delay`, `animation-timing-function`, `animation-iteration-count` and `animation-direction`. Executed keyframes are limited to `from`/`to` over `opacity`, `transform: translate()/scale()/rotate()`, `background-color` and `color`; layout properties such as width/margin/grid/flex are diagnosed and ignored rather than reflowed every frame. `normal` and `alternate` direction plus positive-integer or `infinite` iteration counts are supported. No fill-mode/play-state/multiple percentage interpolation. |
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
| Flex row | Subset | Simplified row layout with basic grow/shrink/basis sizing, justification, alignment, gaps and optional wrapping. No full Flexbox algorithm, column flex, order, baseline alignment or min-content sizing. |
| Grid cards/forms | Subset | Responsive auto-fit/minmax card grid, gaps, minimum auto rows, spans, `repeat(N, 1fr)`, `repeat(N, minmax(0, 1fr))` and simple fixed 2-4 column templates. No explicit placement, named lines, subgrid or dense packing. |
| Aspect ratio | Works | Provides intrinsic height when explicit height/content height is absent. |
| Positioned boxes | Subset | Bounded `relative`, `absolute` and `fixed` positioning for app overlays, badges and pinned panels. Out-of-flow boxes do not consume block/flex/grid/inline placement space. |
| Replaced elements | Subset | Common controls/media are leaf boxes with fallback sizing; real image/video layout is deferred. |
| Text measurement | Subset | Core exposes `TextMeasureProvider`; fallback is tiny but UTF-8-aware. Win32 shell uses GDI measurement. `HostTextAdapter` can wrap LVGL/vendor measurement callbacks. |
| Bidi/text shaping | Deferred | Production non-Latin text needs a platform text backend or future shaping strategy. |
| Fragmentation/multicolumn | Deferred | Not implemented. |

## Form Controls

| Element / feature | Status | Behavior |
| --- | --- | --- |
| `button` | Works | Native-lite painted box, shrink-wrap-ish default, click events. |
| `input type=text` and default input | Works | Value state, UTF-8 text input from host, Backspace. |
| `input list` / `datalist` | Subset | Options are not shown as a popup. Focused text inputs can accept the first matching datalist option with Tab/Enter. |
| `textarea` | Subset | Value-like state and basic painting; full multiline editing is limited. |
| `input type=checkbox` | Works | Checked state, click activation, input/change events. |
| `input type=radio` | Subset | Checked state and painting; full same-name group exclusivity is limited. |
| `input type=range` | Works | Track/thumb painting, pointer drag updates value. |
| `select` / `option` / `optgroup` | Subset | Paints selected option; click cycles options in validation shell; Up/Down moves across options, including options inside `optgroup`. Popup/grouped menu UI is not implemented. |
| `progress` / `meter` | Works | Value bar painting from attributes. |
| Date/color/file controls | Deferred | Use text/select/range fallbacks for now. |
| Validation | Deferred | No constraint validation UI or form submission. |
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
| Pointer move/down/up | Works | Platform-neutral input controller dispatches mouse-like events plus `pointerdown`/`pointerup` aliases. |
| Click synthesis | Works | Same target down/up creates click. |
| Hash anchor click | Shell-only | Win32 shell handles `<a href="#id">` as viewport scroll. Core only dispatches the click event. |
| Focus tracking | Subset | Input controller stores focused node and drives `:focus` / `:focus-within` style matching. |
| Touch events | Subset | `touchstart`/`touchend` are exposed as mouse-like events for press feedback. Full multi-touch objects are deferred. |
| Keyboard events | Deferred | Core handles simple key actions for controls; DOM keyboard event objects are not complete. |

## JavaScript / JerryScript Binding

JavaScript support exists only when built with `JELLYFRAME_BUILD_SCRIPTING=ON` and a
local JerryScript tree configured through `JERRYSCRIPT_ROOT`.

| API | Status | Behavior |
| --- | --- | --- |
| Classic document scripts | Subset | In scripting builds, pseudo/Win32 shells execute inline classic `<script>` and local external `<script src>` through host callbacks. |
| `window` / `document` | Subset | Bound objects expose the methods below. |
| `document.getElementById` | Works | Returns wrapper or `null`. |
| `document.createElement` | Works | Creates a detached element owned by the runtime until it is attached. Creation is bounded by `HostBudgets::max_detached_dom_nodes`. |
| `document.createTextNode` | Works | Creates a detached text node with the same detached-node budget. |
| `appendChild` / `removeChild` | Works | Moves nodes, prevents cycles and marks dirty. `removeChild` keeps the returned node runtime-owned and reusable while it is detached. |
| `setAttribute` / `getAttribute` / `removeAttribute` | Works | Attribute names are lowercased by binding. |
| `textContent` | Works | Getter/setter; unchanged text avoids dirty work. A sole existing text child is updated in place; replacing mixed children remains structural. |
| `className` | Works | Reflected to the `class` attribute and uses the normal style/layout dirty path. |
| `children` / `parentElement` | Subset | Snapshot element-child array and parent wrapper/null. |
| `matches` / `closest` | Subset | Simple tag, `.class`, `#id`, `[attr]` and `[attr=value]` selectors. No combinators. |
| `dataset` | Subset | Existing `data-*` attributes are exposed as camelCase snapshot properties for event delegation. Dynamic new keys are deferred. |
| `element.style` | Subset | Mutable inline style object for `display`, `color`, `background`, `backgroundColor`, `textAlign`, `fontWeight`, `width`, `height`, `opacity`, `transform`, `borderRadius`, `left`, `top`, `right`, `bottom`, `position`, `whiteSpace`, `textOverflow`, `overflow` and `zIndex`. `style.setProperty(name, value)` accepts the same safe CSS property subset plus CSS custom properties such as `--progress`. |
| `hidden` / `disabled` properties | Subset | Boolean reflection. `hidden` removes rendering; disabled form controls do not activate or accept text input. |
| `addEventListener` / `removeEventListener` | Works | JS callbacks are bridged to core event dispatch. |
| Event object | Subset | `type`, `target`, `currentTarget`, phase, cancel/propagation APIs, mouse/wheel fields. |
| Form properties | Subset | `value`, `checked`, `selectedIndex` on relevant controls. |
| Timers | Works | Host-pumped `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`; callback budget controlled by host. |
| Script execution watchdog | Host/runtime optional | `JerryScriptRuntimeOptions::max_execution_check_count` and `HostBudgets::max_script_execution_checks` can interrupt runaway evals and callbacks with `script execution budget exceeded` when the linked JerryScript library was built with `JERRY_VM_HALT=ON`. If that JerryScript feature is absent, JellyFrame reports the watchdog as unsupported and does not fake preemption. The Win32 validation shell can require this path with `--require-script-watchdog` plus bounded check options for recovery smoke tests. |
| Promise/microtask queue | Deferred | Do not rely on browser task semantics. |
| Modules/import | Deferred | `type="module"`, dynamic import and module loading are skipped. |
| `querySelector` | Deferred | Use IDs for now. |
| `innerHTML` | Deferred | Use DOM creation APIs. |
| XHR/fetch/storage | Partial | Scripting builds support async `XMLHttpRequest` GET V0 and a tiny `localStorage` subset when a non-blocking `AppLocalStorageShadow` is bound. `fetch()` waits for bounded Promise/microtask support. |
| Pipeline diagnostics | Started | HTML tokenizer/parser, CSS parser, style resolver, render tree, layout, layer tree, script collection, package/resource loading and software renderer report caps, skipped input, ignored declarations, load failures and degradation through an optional sink used by desktop tools. `jellyframe_pseudo_browser --diagnostics-json` emits the structured report, and `jellyframe_cli.py check`/`preview`/`package` merge it into `pipelineDiagnostics`. Development-time visual diagnostics also report horizontal overflow (`visual-horizontal-overflow`), vertical paint overflow (`visual-vertical-paint-overflow`), scroll-needed content (`visual-scroll-needed`) and high display-command density (`visual-display-command-density`). Known incompatibilities should include a precise reason; unknown or unclassified recovery should at least include the triggering field or snippet. |
| Responsive profile report | Tool-only | `jellyframe_cli.py check`/`preview`/`package`/`install` can explicitly pass `--targets a,b` or `--all-targets` to run the render-core pseudo browser once per target preset and write `responsiveProfiles[]` into the report. Each profile records viewport, shape, content height, horizontal overflow, scroll need and diagnostic counts. Normal single-target commands do not emit this field or render extra viewports. This is pre-release adaptation validation, not a full browser-grade responsive/layout engine. |
| Font resource checker | Tool-only | `jellyframe_font_resource_check` is currently retained for deterministic font work: emit non-ASCII used characters, estimate bitmap font budgets and verify embedded font coverage. |

## Rendering And Pixels

| Feature | Status | Behavior |
| --- | --- | --- |
| Display list | Works | Rectangles, borders, gradients and text commands, including approximate text weight. |
| CPU framebuffer | Works | Software rasterizer/compositor can produce BMP/PPM. Budgeted compositor renders reject oversized primary framebuffers before allocation. |
| Embedded framebuffer adapter | Works | `embedded_framebuffer` converts `HostFrameBufferView` into caller-owned RGBA8888/BGRA8888, RGB565/BGR565, RGB332, Gray8 or 1-bit monochrome buffers and flushes dirty rects through a callback. RGB565/BGR565 targets may enable 4x4 ordered dithering to reduce low-color-depth gradient banding. |
| Source-over alpha | Works | Straight-alpha composition. |
| Opacity layers | Subset | Offscreen compositing for opacity/composited layers. Embedded hosts can cap offscreen pixels; oversized layers degrade to direct per-command opacity instead of allocating a large temporary buffer. |
| Rounded fills | Subset | Rounded rectangle fill clipping for backgrounds/shadows. Rounded fill/stroke/gradient edges use local coverage antialiasing, while ordinary opaque square rectangles keep the fast fill path. |
| Border painting | Works | Borders emitted as fill rectangles. |
| Linear gradient | Subset | Two-color horizontal or vertical command support. |
| Conic gradient | Subset | Two-segment clockwise progress command, starting at 12 o'clock. Rasterized only when used; ordinary rectangles and linear gradients keep their existing fast paths. Stops must cover `0%..100%` contiguously; out-of-range values are diagnosed instead of silently clamped. |
| Text | Subset | Core fallback is tiny ASCII bitmap painting with UTF-8 placeholder glyphs. Win32 shell injects GDI for UTF-8/Chinese validation. |
| Chinese text | Shell-dependent | Use Win32 shell or future platform text backend. Pseudo-browser fallback will show placeholder glyphs. |
| Images | Host-optional/package BMP V0 | Platform-neutral `ImageDecodeMock`, `AppImageSurfaceCache`, `Surface` handle lifetime and width/height/decoded-byte/pending budgets now exist. Render core supports `ImageHandleResolver`, image display commands and `ImagePainter`; pages should use package-local standard paths such as `<img src="/assets/icon.bmp">` or relative URLs. The Win32 shell can load uncompressed 24/32-bit BMP resources from `.jfapp`/source packages as the in-bundle image V0 path; its private debug fixtures are shell validation internals, not app syntax. `AppImageSurfaceCache` can evict LRU ready surfaces by surface-count and decoded-byte budgets while protecting current display-list references; stale completions are rejected and stale ready entries can be dropped/reported during eviction. Image commands carry the `object-fit`, simple `object-position` and `image-rendering` subsets. The `auto` path uses bilinear scaling, while `pixelated`/`crisp-edges` keep hard-edge sampling. Image request rejections and completion failures are classified into stable reasons such as `capability-denied`, `resource-not-found`, `decode-budget-exceeded` and `surface-budget-exceeded`; `diagnostic_detail_for_url(...)` exposes stable `src`/`state`/`reason`/`submit` plus optional host/job/handle/byte fields for desktop and port logs. PNG/JPEG/WebP, complex position syntax and production MCU codecs are still pending. |
| Audio playback | Host-optional/runtime mock | Core does not own PCM/I2S/codecs. `app_runtime` includes `AudioCommandMock` for open/play/pause/stop/close/setVolume, `AudioStream` handle lifetime, ended/error completions and stream budget checks. Audio request rejections and completion failures classify into stable reasons such as `capability-denied`, `invalid-source`, `source-not-found`, `invalid-handle`, `stream-budget-exceeded`, `command-timeout` and `command-cancelled`; `app_audio_failure_detail(...)` exposes stable `source`/`reason`/`submit`/`host`/`error` fields. JerryScript builds expose a tiny host-optional standards-shaped `Audio` V0: `new Audio(src)`, `src`, `volume`, `play()`, no-op `pause()`, `onended`/`onerror` and `addEventListener`/`removeEventListener` for `ended` and `error`. The Win32 shell binds this to package/local audio resources, dispatches `ended` after its debug playback estimate and still provides `--audio-smoke` for local files or package paths such as `/audio/tone.wav`. `media.audio.mp3` can be declared in manifests, but real MCU MP3/I2S playback remains host/port work. |
| Background services | Manifest intent/runtime policy | `backgroundServices` in `jellyframe.app.json` declares whether network, audio, sensors or location want to continue while suspended, while the screen is off or, for sensors/location, in low-power mode. This does not grant permission by itself. `AppBackgroundServicePolicy` plus host profile/system state produce `AppServiceActivityPolicy`, so foreground apps run normally, non-approved background work pauses, audio can be paused, sensors can be throttled and location snapshots can be delayed without touching render core. |
| Host service workers | Platform-neutral pump | `pump_app_host_service_worker(...)` provides a tiny worker boundary for real host services. It processes one `HostServiceJobKind`, preserves request identity on completions, refuses to pop when the UI completion queue is full and keeps DOM/JS/layout/framebuffer ownership on the UI task. It does not create threads or perform I/O. |
| Lightweight video/MJPEG/H.264 | Experimental/host-optional | Planned only as a low-resolution frame provider. `<video>` is not promised. The ESP32-S3 H.264 retest succeeds but remains below real-time, so it belongs only in an explicit experimental profile, not the default profile. |
| Canvas/SVG | Deferred | No canvas API or SVG renderer. |
| Real shadow blur | Deferred | `box-shadow` blur is approximated. |
| Filters/blend modes | Deferred | Only normal source-over. |
| GPU compositing | Deferred | Current renderer is CPU-only; layer model leaves room for hardware backends. |

## Dirty Work And Rerendering

| Mechanism | Status | Behavior |
| --- | --- | --- |
| Dirty propagation | Works | Mutations OR dirty bits onto the changed node and ancestors. |
| Dirty check | Works | Root check is O(1) because ancestor propagation keeps aggregate bits. |
| Dirty clear | Works | Skips clean branches. |
| Host coalescing | Subset | Win32 shell rerenders only after dirty input/script callbacks. Viewport scroll in the Win32 shell reuses the existing full-content framebuffer and updates the visible blit buffer by moving rows plus copying only newly exposed rows when possible. |
| Incremental style/layout | Subset | Paint-only form-control state changes can reuse render/layout in the Win32 validation shell and rebuild only layer/display commands. A guarded same-box single-line text path can also reuse render/layout when the updated text measures to the existing layout box. A guarded style/class path reuses layout when the render tree shape and all layout-affecting style fields stay unchanged; paint/compositor changes such as color, background, opacity and transform can use this path. Transform changes reuse the animation invalidation helper so old and new bounds are repainted. Wrapping text, layout-affecting style, unknown structural changes and tree changes still rebuild render/layout. |
| Dirty rectangle repaint | Subset | `dirty_region` computes bounded repaint rects for direct text/attribute/form-control paint changes by comparing old and new layout boxes, or by reusing the same layout for paint-only changes. Tree mutations conservatively repaint the viewport. Hosts may also choose full-frame repaint when estimated dirty area is too large for partial flush to pay off. The software compositor drops duplicate or fully contained dirty rectangles before replaying clips, avoiding repeated clear/repaint work for the same area. |
| Animation invalidation | Subset | `animation_invalidation` uses previous/current animation style overrides and the current layout tree to produce local dirty rectangles for opacity/color paint-only animation and translate/scale/rotate transform before/after bounds. |
| Display invalidation diagnostics | Works | `analyze_display_invalidation(...)` reports dirty-rectangle coverage over layers and display commands. This is diagnostic only; retained display-list reuse is still deferred. |
| Frame dirty diagnostics | Works | Win32 scripted capture reports the per-run dirty flag distribution (`tree`, `attributes`, `text`, `style`, `layout`, `paint`, `render_or_layout`), frame-update reasons such as `text_stable` and `style_stable`, and final `layer_tree layers=N display_commands=N` counts. Use this to find whether an app is spending frames on layout-producing DOM changes, cheap paint-only updates or display-command-heavy pages. |
| Per-app budget snapshot | Works | `AppBudgetSnapshot` reports the active app instance, role/state, host service queues, host handles/bytes, app fonts, system events, localStorage-shadow counters, frame-loop callback caps, active animations and script timer/listener/detached-node counts when the host supplies them. Win32 frame capture prints this summary; MCU ports can sample the same counter-only structure for serial diagnostics or recovery decisions. |
| Budget recovery | Works V0 | `app_budget_recovery_for_snapshot(...)` classifies exhausted runtime budgets into `none`, `warn` or `terminate-app`. Queue/handle/font/system-event/script-object exhaustion is treated as `budget-exceeded` app recovery; frame callback and active-animation caps are warnings because frame policy can throttle them. Win32 system-shell mode reports `budget_recovery_teardown reason=budget-exceeded` and returns to the launcher for the validation fixture. |
| Host frame sink | Subset | `present_frame` exposes `FrameBuffer` through `HostFrameSink` with optional dirty rects. A successful `present` is the frame-buffer reuse boundary: asynchronous panel/DMA hosts must wait, copy into driver-owned memory or gate the next render until flush completion. `embedded_framebuffer` supplies portable pixel conversion; real display I/O remains host-owned. |
| Host device capabilities | Draft | `HostDeviceCapabilities` records display, input, memory, budget and service flags for board ports. Current core code treats it as a contract/documented policy input; deeper automatic adaptation is deferred. |
| Host budgets | Subset | `HostBudgets` feeds HTML/CSS parsing, render/layout/layer tree caps, display-list caps, dirty-rect count, frame-loop input/timer/animation caps, JerryScript timer/rAF/listener limits, detached DOM node limits and software-compositor primary/offscreen pixel caps. Render, layout and layer trees now have arena-backed build paths; full mutable DOM arenas remain future work. |
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
- Needs large external subsystems (image decode, font shaping, canvas).
- Requires a complete browser task/loading model.
- Would make unsupported modern styling look half-correct and incoherent.
