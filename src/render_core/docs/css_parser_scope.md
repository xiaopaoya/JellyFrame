# CSS Parser Scope

> Last updated: 2026-07-22; Applies to: 0.5.0

Last checked against CSS Syntax Module Level 3, Media Queries and browser
parser source layout on 2026-06-16:

- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSS Syntax editor draft: https://drafts.csswg.org/css-syntax/
- CSS Conditional Rules Module Level 3: https://www.w3.org/TR/css-conditional-3/
- Media Queries Level 4: https://www.w3.org/TR/mediaqueries-4/
- Blink CSS parser sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/css/parser/
- WebKit CSS parser sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/css/parser

JellyFrame should parse modern CSS without catastrophic failure, but it should not
pretend to implement the full modern cascade. The parser accepts common syntax,
recovers at rule/declaration boundaries, and leaves unsupported styling as a
coherent fallback rather than mixing partial modern enhancements with old
layout behavior.

## Visual Consistency Policy

- Prefer stable baseline styling over partial modern styling.
- Preserve declaration order so classic fallback declarations survive:
  `color: #333; color: oklch(...);`.
- Unsupported values must not overwrite earlier supported values.
- Unsupported enhancement blocks should be skipped as a unit until their
  evaluator exists.
- Parsing a bad rule must not corrupt following rules.

## Phase 1: Implement Directly

- Comments.
- Qualified style rules.
- Selector lists split on top-level commas.
- Declaration blocks.
- Ordered declarations, including duplicate properties.
- `!important` recognition at parse time.
- Strings, escapes, functions and bracketed component values inside selectors
  and declarations.
- Top-level error recovery for malformed declarations and malformed rules.
- `@layer` block flattening.
- `@media` block flattening when the prelude is empty, `all`, `screen` or a
  bounded query list made from `screen`/`all` plus `min-width`, `max-width`,
  `min-height` and `max-height` conditions in `px` or unitless px-like values.
  Comma-separated media lists use ordinary OR semantics when at least one
  supported item matches the configured parser viewport. Tool and Win32 entry
  points set that parser viewport from the actual render viewport at page load.
  Recognized but non-matching media blocks are diagnostics info, not warnings.
- `@supports` block flattening for conservative declaration feature queries.
  The supported subset accepts `(property: value)`, `not`, homogeneous `and` or
  `or` chains, and parentheses. `selector()` and unknown/unsafe feature queries
  evaluate false so enhancement blocks degrade cleanly.
- CSS custom property declarations and a style-resolution subset of
  `var(--token)` / `var(--token, fallback)`. Values inherit along the DOM path,
  `:root` tokens and inline tokens work, and unresolved values keep earlier
  fallback declarations alive.
- `background` accepts solid colors and a style-resolution subset of
  `linear-gradient(<color>, <color>)`,
  `linear-gradient(to bottom/top/right/left, ...)`, two-segment `conic-gradient()`
  progress rings and two-color center-circle `radial-gradient()` highlights.
  Unsupported angles, stops, focal points, ellipses or image values are reported
  without replacing earlier supported fallbacks.
- `background-image` accepts the same gradient subset, but not solid colors. It
  also accepts one package-absolute `url("/assets/image.bmp")`, which is handed
  to the host image resolver and fills the element background paint area. The
  parser rejects remote/data URLs, relative paths, traversal, query/fragment,
  multiple URL layers and background repeat/position/size syntax.
- Type/class/id/attribute compound selectors, descendant and child combinators,
  and adjacent/general sibling combinators.
- Dynamic pseudo-classes `:hover`, `:active`, `:focus`, `:focus-within`,
  `:checked` and `:disabled`.
- `:is()` and `:where()` selector-list functions for the supported selector
  subset; `:where()` contributes zero specificity.
- CSS nesting's explicit one-level `&` subset. Parent declarations and nested
  rules retain source order; selector-list expansion is capped at 16. Implicit
  nesting, deeper blocks and nested at-rules produce `css-nesting-skipped`.
- UI-oriented declarations for the embedded app subset, including
  physical `margin-*`/`padding-*`/`border-*-width` longhands,
  `outline`, `text-shadow`, `text-decoration`, `text-transform`, `letter-spacing`,
  `white-space: normal`/`nowrap`, their `text-wrap: wrap`/`nowrap` aliases,
  inherited `visibility: visible`/`hidden` (layout stays in flow while hidden
  paint and hit targets are suppressed),
  `overflow-wrap: anywhere`, `aspect-ratio`, `gap`, `column-gap`, `row-gap`, `flex`,
  `flex-grow`, `flex-shrink`, `flex-basis`, signed-integer `order`, `position`, `top`, `right`,
  `bottom`, `left`, `grid-template-columns` with a `minmax()` minimum track,
  bounded `grid-template-rows` fixed/`1fr` tracks,
  `repeat(N, 1fr)`, `repeat(N, minmax(0, 1fr))`, `grid-auto-rows` with a
  minimum track, bounded positive numeric `grid-column`/`grid-row` start/end/span placement, plus `object-fit` and a
  keyword/percentage one/two-value `object-position` subset, and the
  `image-rendering` `auto`/`pixelated`/`crisp-edges` subset.
- `@keyframes` from/to subset. Named blocks keep `from`/`to` or `0%`/`100%`
  declarations for the animation timeline. Intermediate percentages are
  diagnosed and ignored, not partially interpolated.

## Phase 1: Lazy Handling

- Unsupported/complex `@media`, unsupported/complex `@supports`, `@container`,
  `@scope`: skip the full block. Recognized width/height `@media` blocks that
  simply do not match the current viewport are treated as normal branch
  selection.
- `@font-face`, `@page`, `@property`: parse their balanced block boundaries
  but do not expose declarations to style resolution yet.
- Unknown at-rules: skip statement or balanced block.
- Unsupported selectors such as `:has()`, `::part()` and `::slotted()` are
  skipped as full rules for now.

## Explicitly Not Yet Implemented

- Full CSS token stream objects.
- Full selector parser.
- Cascade layers and layer ordering.
- Full media query evaluation, including `not`, range syntax, media features
  other than width/height and dynamic viewport updates after parsing.
- Full feature query evaluation, including `selector()`, font technology tests,
  and exact browser support semantics.
- Full custom property dependency graph, case-sensitive custom property names,
  guaranteed cycle handling beyond the bounded recursion guard, and CSS-wide
  invalid-at-computed-value-time semantics.
- Full CSS nesting semantics, including implicit nesting, nested at-rules and
  deeper recursive blocks.
- Shadow DOM selectors.
- Full animation/keyframe model, including intermediate keyframes,
  fill/play-state/composition and layout-property animation.
- Full grid value grammar, named/negative lines, `grid-*-start/end` longhands, overlap resolution and dense packing.
- Complete `object-position` four-value syntax and length offsets.
- Container query evaluation. This is intentionally deferred until layout/style
  feedback can be bounded without cycles.

## Current Parser Limits

- `max_rules`: 4096
- `max_declarations_per_rule`: 256
- `max_nesting_depth`: 8
- `flatten_layer_blocks`: true
- `parse_plain_media_blocks`: true
- `parse_supports_blocks`: true
- `media_viewport_width`: 360
- `media_viewport_height`: 240
