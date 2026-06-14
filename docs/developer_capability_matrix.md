# Developer Capability Matrix

Date: 2026-06-15

This document is the practical contract for application authors using WearWeb.
It describes what the engine can do today, what it deliberately degrades, and
what should not be relied on yet. The target reader is a developer building a
small embedded UI or a desktop validation page for a future wearable device.

WearWeb is not a general browser. It is a small HTML/CSS/DOM/script runtime
that keeps the common application model while cutting browser services that are
expensive, hard to bound, or not useful on constrained devices.

## Status Labels

- **Works**: implemented and covered by the current design.
- **Subset**: usable, but only the documented part should be used.
- **Stored**: parsed or kept in style data, but not fully executed visually.
- **Lazy**: skipped or simplified as a unit without corrupting following input.
- **Deferred**: intentionally absent; do not depend on it.
- **Shell-only**: available in desktop examples, not in platform-neutral core.

## Best Fit

WearWeb works best for:

- Weather, clock, timer, calculator and settings apps.
- Small dashboards with cards, text, form controls and host-provided data.
- Local embedded applications that want HTML/CSS/JS authoring instead of canvas
  drawing.
- Desktop validation through `wearweb_pseudo_browser` or
  `wearweb_win32_browser`.

WearWeb is not ready for:

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
| Win32 browser shell | Shell-only | Opens a desktop window, uses GDI text, forwards mouse/wheel/keyboard input, supports capture output and optional scripting builds. |
| Embedded backend | Deferred | The final display/input backend should be provided by the target platform. |
| Linked CSS loading | Shell-only | Example tools can load local `<link rel="stylesheet">`; core exposes callback-style helpers only. |
| Network loading | Deferred | No HTTP, fetch, XHR, WebSocket or remote asset loading. |
| Storage | Deferred | No cookies, localStorage, IndexedDB or filesystem API in core. |

## HTML Parsing

| Feature | Status | Behavior |
| --- | --- | --- |
| UTF-8 input bytes/string | Works | Input is treated as byte/string data. Text rendering quality depends on the renderer backend. |
| Start/end tags | Works | Common tags become DOM elements. |
| Attributes | Works | Quoted and common unquoted forms are parsed. Attribute names are normalized by parser paths that lowercase HTML names. |
| Text nodes | Works | Text is preserved and participates in layout. |
| Comments | Works | Tokenized and ignored by visual tree construction. |
| Doctype | Works/Lazy | Accepted; no quirks mode is entered. |
| Character references | Subset | Common references are decoded. Unknown or unsupported cases degrade to literal/fallback behavior. |
| Raw text for `script`/`style` | Subset | Content is preserved enough for style/script collection. |
| Synthesized `html`/`body` | Works | Missing wrapper structure is repaired. |
| Void elements | Works | Common void tags do not require closing tags. |
| Implied end tags | Subset | Common paragraph/list/table-ish cases are tolerated; full HTML tree-builder compatibility is not a goal. |
| Malformed markup | Subset | Parser recovers with limits; it should not loop forever or crash. |
| Quirks mode | Deferred | Always ignored. WearWeb targets modern authored pages. |
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
| `@media screen/all/empty` | Subset | Plain screen/all blocks are parsed. Conditional media queries are skipped. |
| `@supports` | Lazy | Whole block skipped. |
| `@container` | Deferred/Lazy | Whole block skipped. Avoid it for required UI. |
| `@font-face` | Lazy | Balanced block skipped; no font loading. |
| `@keyframes` | Lazy | Balanced block skipped; no animation model. |
| Unknown at-rules | Lazy | Statement or balanced block skipped. |
| CSS custom properties | Stored/Lazy | Declarations may survive for diagnostics, but `var()` dependency resolution is not implemented. |
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
| Dynamic pseudo-classes | Deferred | `:hover`, `:focus`, `:active`, `:disabled` are not style triggers yet. |
| `:is()`, `:where()`, `:has()` | Lazy | Rules using unsupported modern selector functions are skipped. |
| Pseudo-elements | Deferred | `::before`, `::after`, markers and selection styling are absent. |
| Sibling combinators | Deferred | `+` and `~` rules are skipped/unsupported. |
| Shadow selectors | Deferred | `::part`, `::slotted` skipped. |

## CSS Properties

Only supported values should be used for required UI. Unsupported values do not
clear older supported fallback declarations.

| Property | Status | Supported values / degradation |
| --- | --- | --- |
| `display` | Subset | `block`, `inline`, `inline-block`, `flex`, `inline-flex`, `grid`, `inline-grid`, `none`. Inline flex/grid map to the same simplified layout modes. |
| `color` | Subset | Named basics, hex, `rgb()`, `rgba()`. Unsupported color functions such as `oklch()` do not override fallbacks. |
| `background-color` | Subset | Same color parser as `color`. |
| `background` | Subset | Color extraction for common color backgrounds. Images are ignored. |
| `margin` | Works | 1-4 length values plus horizontal `auto`. |
| `margin-top/right/bottom/left` | Works | Physical longhands. `margin-left/right:auto` works for horizontal centering paths. |
| `padding` | Works | 1-4 length values. |
| `padding-top/right/bottom/left` | Works | Physical longhands. |
| `border` | Subset | Parses `none`, width and color from simple shorthand. Style keyword is tolerated only as ignored text. |
| `border-width` | Works | 1-4 length values. |
| `border-top/right/bottom/left-width` | Works | Physical width longhands. |
| `border-color` | Subset | Single color for all borders. |
| `border-radius` | Subset | Single length radius. Rounded fills are supported; complex corner radii are not. |
| `width` / `height` | Works | Length values in supported units. |
| `min-width` / `min-height` | Works | Length values. |
| `max-width` | Works | Length value; used by block layout. |
| `aspect-ratio` | Works | Positive number or `w / h`, including `auto w / h`. Used for intrinsic box height. |
| `font-size` | Works | Length values. |
| `line-height` | Works | Unitless multiplier or length. |
| `text-align` | Works | `left`, `right`, `start`, `end`, `center`. |
| `text-indent` | Works | Length value. |
| `box-sizing` | Works | `content-box`, `border-box`. |
| `overflow` | Subset | `visible`, `hidden`, `clip`, `auto`, `scroll`; clipping creates layer boundaries, but native scroll containers are not complete. |
| `opacity` | Subset | 0..1; creates composited layer in software compositor. |
| `position` | Stored/Subset | `relative`, `absolute`, `fixed`, `sticky` create stacking/layer hints; full positioned layout is not implemented. |
| `z-index` | Subset | Integer or `auto`; layer-local ordering only. |
| `transform` | Stored/Lazy | Non-`none` creates a compositing boundary; transform math is deferred. |
| `justify-content` | Subset | `start`, `flex-start`, `normal`, `center`, `space-around`, `space-between` in simplified flex rows. |
| `align-items` | Subset | `stretch`, `normal`, `start`, `flex-start`, `center`, `end`, `flex-end` in simplified flex rows. |
| `gap` | Works | 1-2 length values for grid and simplified flex support. |
| `row-gap` / `column-gap` | Works | Length values. |
| `grid-template-columns` | Subset | Extracts minimum track from `repeat(auto-fit, minmax(<length>, 1fr))`, `minmax(<length>, 1fr)`, a length, or `1fr`. |
| `grid-auto-rows` | Subset | Length or `minmax(<length>, auto)` minimum row height. |
| `grid-column` / `grid-row` | Subset | `span N`, clamped to bounded values. Explicit line placement is absent. |
| `box-shadow` | Subset | First shadow becomes an approximate rounded translucent fill. Real blur and multiple shadows are not rasterized. |
| `object-fit` | Deferred | Waits for image decode/replaced-element support. |
| `font-family` / `font-weight` | Deferred | Text backend decides the actual font. Win32 shell uses Microsoft YaHei UI. |
| Animations/transitions | Deferred | Declarations skipped or stored without animation behavior. |
| Filters/backdrop filters | Deferred | Not painted. |

Supported length units are currently `px`, unitless px-like numbers, `rem`,
`em` and a simple `vh` approximation. Percentages, `calc()` and viewport-width
math are not general-purpose layout units yet.

## Layout

| Feature | Status | Behavior |
| --- | --- | --- |
| Block layout | Works | Vertical box layout with margin, padding, border, max-width and horizontal auto margins. |
| Inline text flow | Subset | Text and inline controls flow horizontally and wrap by available width. |
| Inline background/border | Subset | Shrunk to text/content bounds where possible. |
| `inline-block` | Subset | Represented as inline-like render object with usable box behavior. |
| Flex row | Subset | Simplified one-line row layout with basic justification, alignment and column gap. No wrapping or full flex sizing. |
| Grid cards | Subset | Responsive auto-fit/minmax card grid, gaps, minimum auto rows and spans. No explicit placement, named lines, subgrid or dense packing. |
| Aspect ratio | Works | Provides intrinsic height when explicit height/content height is absent. |
| Replaced elements | Subset | Common controls/media are leaf boxes with fallback sizing; real image/video layout is deferred. |
| Text measurement | Approximate | Core fallback is tiny and ASCII-oriented. Win32 shell uses GDI text. |
| Bidi/text shaping | Deferred | Production non-Latin text needs platform text painter or future shaping strategy. |
| Fragmentation/multicolumn | Deferred | Not implemented. |

## Form Controls

| Element / feature | Status | Behavior |
| --- | --- | --- |
| `button` | Works | Native-lite painted box, shrink-wrap-ish default, click events. |
| `input type=text` and default input | Works | Value state, UTF-8 text input from host, Backspace. |
| `textarea` | Subset | Value-like state and basic painting; full multiline editing is limited. |
| `input type=checkbox` | Works | Checked state, click activation, input/change events. |
| `input type=radio` | Subset | Checked state and painting; full same-name group exclusivity is limited. |
| `input type=range` | Works | Track/thumb painting, pointer drag updates value. |
| `select` / `option` | Subset | Paints selected option; click cycles options in validation shell. Popup UI is not implemented. |
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
| Pointer move/down/up | Works | Platform-neutral input controller dispatches mouse-like events. |
| Click synthesis | Works | Same target down/up creates click. |
| Focus tracking | Subset | Input controller stores focused node. CSS `:focus` styling is absent. |
| Touch events | Deferred | Host can map touch to pointer-like input for now. |
| Keyboard events | Deferred | Core handles simple key actions for controls; DOM keyboard event objects are not complete. |

## JavaScript / JerryScript Binding

JavaScript support exists only when built with `WEARWEB_BUILD_SCRIPTING=ON` and a
local JerryScript tree configured through `JERRYSCRIPT_ROOT`.

| API | Status | Behavior |
| --- | --- | --- |
| Classic script evaluation | Shell-only | Pseudo/Win32 shell can evaluate one external script. Automatic script loading is still limited. |
| `window` / `document` | Subset | Bound objects expose the methods below. |
| `document.getElementById` | Works | Returns wrapper or `null`. |
| `document.createElement` | Works | Creates detached element owned by runtime. |
| `document.createTextNode` | Works | Creates detached text node. |
| `appendChild` / `removeChild` | Works | Moves nodes, prevents cycles, marks dirty. |
| `setAttribute` / `getAttribute` | Works | Attribute names are lowercased by binding. |
| `textContent` | Works | Getter/setter; unchanged text avoids dirty work. |
| `addEventListener` / `removeEventListener` | Works | JS callbacks are bridged to core event dispatch. |
| Event object | Subset | `type`, `target`, `currentTarget`, phase, cancel/propagation APIs, mouse/wheel fields. |
| Form properties | Subset | `value`, `checked`, `selectedIndex` on relevant controls. |
| Timers | Works | Host-pumped `setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`; callback budget controlled by host. |
| Promise/microtask queue | Deferred | Do not rely on browser task semantics. |
| Modules/import | Deferred | No module loader. |
| `querySelector` | Deferred | Use IDs for now. |
| `innerHTML` | Deferred | Use DOM creation APIs. |
| Fetch/network/storage | Deferred | Host must provide data. |

## Rendering And Pixels

| Feature | Status | Behavior |
| --- | --- | --- |
| Display list | Works | Rectangles, borders, gradients and text commands. |
| CPU framebuffer | Works | Software rasterizer/compositor can produce BMP/PPM. |
| Source-over alpha | Works | Straight-alpha composition. |
| Opacity layers | Works | Offscreen compositing for opacity/composited layers. |
| Rounded fills | Subset | Rounded rectangle fill clipping for backgrounds/shadows. |
| Border painting | Works | Borders emitted as fill rectangles. |
| Linear gradient | Subset | Simple vertical command support. |
| Text | Subset | Core fallback is tiny ASCII. Win32 shell injects GDI text for UTF-8/Chinese validation. |
| Chinese text | Shell-dependent | Use Win32 shell or future platform text painter. Pseudo-browser fallback will show fallback glyphs. |
| Images | Deferred | No image decode. `img`/media nodes get usable boxes/fallback only. |
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
| Host coalescing | Subset | Win32 shell rerenders only after dirty input/script callbacks. |
| Incremental style/layout | Deferred | Current rerender still rebuilds render/layout/layer trees once dirty. |
| Dirty rectangle repaint | Deferred | Layer structure prepares for it, but framebuffer repaint is still full-frame. |

Practical implication: repeated script mutations should be batched in one event
or timer callback. The host will see one dirty document and rerender once, but
the rerender is still a full simplified pipeline pass.

## Recommended Authoring Rules

- Prefer stable IDs for interactive nodes.
- Use simple selectors and class-based styling.
- Use the supported grid-card subset for dashboards.
- Use `button`, `input`, `select`, `textarea`, `progress`, `meter` instead of
  custom canvas widgets.
- Provide classic CSS fallbacks before unsupported modern values:
  `color: #334155; color: oklch(...);`.
- Avoid required UI inside `@container`, `@supports`, complex media queries or
  unsupported selector functions.
- Keep scripts synchronous and small. Use host-provided data.
- Treat Win32 shell rendering as the desktop validation path for Chinese text.
- Keep pages small and bounded; parser limits exist by design.

## Current Hard Limits

- CSS parser: `max_rules` 4096, `max_declarations_per_rule` 256,
  `max_nesting_depth` 8.
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
