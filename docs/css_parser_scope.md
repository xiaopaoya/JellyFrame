# CSS Parser Scope

Last checked against CSS Syntax Module Level 3 and browser parser source layout
on 2026-06-13:

- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSS Syntax editor draft: https://drafts.csswg.org/css-syntax/
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
- Plain `@media` blocks only when the prelude is empty, `all` or `screen`.
- UI-oriented declarations for the embedded app subset, including
  physical `margin-*`/`padding-*`/`border-*-width` longhands,
  `aspect-ratio`, `gap`, `column-gap`, `row-gap`, `grid-template-columns`
  with a `minmax()` minimum track, `grid-auto-rows` with a minimum track, and
  `grid-column`/`grid-row: span N`.

## Phase 1: Lazy Handling

- `@supports`, conditional `@media`, `@container`, `@scope`: skip the full block.
- `@font-face`, `@keyframes`, `@page`, `@property`: parse their balanced block
  boundaries but do not expose declarations to style resolution yet.
- Unknown at-rules: skip statement or balanced block.
- Unsupported selectors such as `:has()`, `:is()`, `:where()`, `::part()` and
  `::slotted()` are skipped as full rules for now.

## Explicitly Not Yet Implemented

- Full CSS token stream objects.
- Full selector parser.
- Cascade layers and layer ordering.
- Media query evaluation.
- Feature query evaluation.
- Custom property dependency graph.
- CSS nesting semantics.
- Shadow DOM selectors.
- Animation/keyframe model.
- Full grid value grammar, named lines, explicit placement and dense packing.
- Container query evaluation. This is intentionally deferred until layout/style
  feedback can be bounded without cycles.

## Current Parser Limits

- `max_rules`: 4096
- `max_declarations_per_rule`: 256
- `max_nesting_depth`: 8
- `flatten_layer_blocks`: true
- `parse_plain_media_blocks`: true
