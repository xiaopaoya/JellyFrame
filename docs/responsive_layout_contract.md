# Responsive Layout Contract

> Last updated: 2026-08-30; Applies to: 0.6.0-dev
>
> Status: implementation baseline for Render Core 0.6.x. This document is a
> behavioral contract, not a claim of browser compatibility.

JellyFrame responsive layout is a bounded, integer-pixel layout model. An App
must be authored against the target viewport and then checked at every claimed
target. A CSS declaration is usable only when its property, value form and
owning layout mode are listed below and the target feature profile enables it.

## Coordinate Model

- The layout origin is the top-left of the target viewport.
- Coordinates and resolved sizes are non-negative integer CSS pixels unless a
  documented out-of-flow offset is negative.
- The viewport width and height are explicit inputs to layout and `@media`.
  They are not inferred from a desktop window or the author's monitor.
- The current public writing mode is LTR. Logical `inline-*` and `block-*`
  properties map to physical width/height and left/right or top/bottom in this
  mode only. Vertical writing and bidirectional layout are deferred.
- A round target clips paint to its target shape. A round viewport is not a
  second layout width; keep important content inside a rectangular safe area.

## Sizing

| Form | Status | Contract |
| --- | --- | --- |
| Omitted width/height | Supported | Uses intrinsic or containing-block sizing according to the owning layout mode. |
| Explicit `auto` width/height | Supported | Equivalent to the omitted dimension and does not produce a style warning. It is not a magic `100%`. |
| Non-negative `px` | Supported | Resolved as an integer pixel size. |
| Percentage width/height | Supported subset | Resolves against the containing block's corresponding available size. Percentage height requires a definite containing height; otherwise intrinsic sizing applies. |
| `min-*` / `max-*` in `px` or `%` | Supported subset | Applied after the preferred size and before placement. Values are bounded by the target's integer layout range. |
| `em`, `rem`, `vw`, `vh`, bounded `calc()` | Supported subset | Converted to integer pixels during style resolution. `vw`/`vh` use the target viewport. `calc()` is not a general expression language. |
| Negative, non-finite or unsupported lengths | Rejected | The declaration is ignored and a diagnostic identifies the property. It must not silently become an unrelated size. |
| `auto` on min/max dimensions | Not part of the author contract | Omit the declaration to express no minimum/maximum. |

`box-sizing: border-box` is supported for the documented box model. Without
it, declared width/height are content-box sizes. Padding and borders must be
included in the target budget; do not rely on browser overflow hiding a size
mistake.

## Flex and Grid

Flex is the preferred responsive primitive for ordinary App layouts:

- Supported flex directions are `row` and `column`.
- Direct children support bounded `flex-grow`, `flex-shrink`, `flex-basis`,
  `order` and `align-self`.
- `align-items` and `justify-content` support `start`, `center`, `end` and
  the documented distribution values. `stretch` is valid for cross-axis
  alignment. These values are resolved in the container's current axis; they
  are not interchangeable aliases for absolute positioning.
- `stretch` changes only an auto cross-axis size. An explicit cross-axis
  width/height remains authoritative; margins, padding and borders stay inside
  the available cross-axis extent.
- `gap`, `row-gap` and `column-gap` are pixel/integer bounded values.
- `flex-wrap: wrap` is supported as a bounded row-wrapping mode. Column flex
  remains non-wrapping, and `wrap-reverse` is rejected rather than approximated.
  It is not a replacement for scroll and does not provide browser line
  balancing. Each resulting line is its own cross-axis alignment context.
- Flex sizing is integer and bounded. Remainders are distributed deterministically
  in source/order order, so adjacent targets can differ by at most the integer
  rounding remainder.

Grid is intentionally narrower: use two to four fixed or `1fr` columns/rows,
bounded `minmax()` forms, and positive numeric placement/span. Complex
auto-placement, named areas, subgrid and content-driven track algorithms are
not promised.

## Responsive Rules

Only `@media` conditions using `min-width`, `max-width`, `min-height` and
`max-height` with finite pixel values are in the public responsive contract.
Use separate rules for width and height when a layout depends on both. Media
rules are evaluated against the actual target viewport before style resolution.
Unsupported media features are reported and do not create a hidden fallback
mode.

Recommended pattern:

```css
.screen { display: flex; flex-direction: row; gap: 8px; }
.card { flex: 1 1 0; min-width: 0; max-width: 100%; }
@media (max-width: 200px) {
  .screen { flex-direction: column; gap: 4px; }
  .card { width: 100%; }
}
```

Use an explicit fixed-height `overflow-y: auto` region for content that may
exceed a small display. A page that is taller than the viewport is not
automatically scrollable, and a fixed bottom bar must remain outside the
scroll region if it is intended to stay visible.

## Text and Controls

Text measurement is part of layout, not a paint-only detail. Use explicit
`font-size` and `line-height` for stable controls. The supported wrapping
subset is UTF-8 scalar-boundary wrapping with `overflow-wrap: anywhere`,
normal opportunities, or `white-space: nowrap` plus UTF-8-safe ellipsis.
Hyphenation, balanced wrapping, complex shaping and automatic browser font
substitution are not guaranteed.

Every visible target needs a font report. A missing requested face or weight
must be resolved by packaging a matching `.jffont` resource or by changing the
design; it must not be treated as evidence that desktop and device layout are
equivalent.

## Acceptance Matrix

An author-facing layout change is complete only when it has:

1. Positive and negative parser/style tests for each property/value form.
2. Layout tests at `300x300`, `320x240` and `172x320`, including one narrow
   media branch and one round target when shape affects paint.
3. Deterministic desktop captures with no unexpected layout overflow or
   unsupported-style diagnostics.
4. A capability-table entry and actionable diagnostic text for rejected forms.
5. A benchmark or bounded-memory measurement when the change adds a layout
   pass, cache or per-node allocation.
6. Hardware evidence before the behavior is described as port-supported.

The next Core candidates are therefore selected by reproducible author need,
not by the number of CSSWG properties marked `partial`.
