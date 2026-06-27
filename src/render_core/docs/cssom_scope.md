# CSSOM And Cascade Scope

Last checked against CSSOM and CSS Cascade references on 2026-06-13:

- CSSOM: https://www.w3.org/TR/cssom-1/
- CSS Cascading and Inheritance: https://www.w3.org/TR/css-cascade-5/
- CSS Syntax: https://www.w3.org/TR/css-syntax-3/
- Blink CSS sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/css/
- WebKit CSS sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/css

JellyFrame needs a small CSSOM that preserves author intent and gives later
runtime layers a stable object model. The first CSSOM is not a JavaScript-facing
complete API. It is the internal representation used by parsing, cascade,
style resolution, diagnostics and later JerryScript bindings.

## Implemented Model

```text
CssStyleSheet
  -> CssRule list
      -> CssRuleType::Style
      -> selector text
      -> ordered CssDeclaration list
      -> CssSpecificity
      -> source order
```

The public names intentionally mirror CSSOM concepts:

- `CssStyleSheet`
- `CssRule`
- `CssRuleType`
- `CssDeclaration`
- `CssSpecificity`

## Cascade Policy

The current cascade is author-style only:

- Match supported selectors.
- Supported selectors include simple compound selectors, descendant/child and
  sibling combinators, simple attribute selectors, `:root`, dynamic state
  pseudo-classes, and `:is()` / `:where()` selector-list functions.
- Compare `!important`.
- Compare selector specificity.
- Compare source order.
- Apply inline style as a high-specificity author declaration.
- Ignore unsupported values without clearing earlier supported fallback values.

This is enough to keep functional UI structure stable. For example, a modern
search page may lose blur, shadow, rounded corners or advanced color spaces, but
the baseline box, color, size and spacing declarations should survive when they
exist as fallbacks.

## Lazy Handling

- User-agent origin, user origin and animation origin are not modeled yet.
- Cascade layers are flattened during parse; layer ordering is not modeled.
- Custom properties support a direct inherited `var(--token)` /
  `var(--token, fallback)` subset; full dependency graph semantics are absent.
- Unsupported selectors such as `:has()` and shadow selectors are skipped
  before CSSOM insertion or ignored during matching.
- Unsupported values remain in CSSOM for diagnostics but do not override
  supported computed values.
- A small built-in default style layer gives form controls, dialogs and media
  elements usable boxes before a full UA stylesheet exists.

## Low-End Device Rules

- CSSOM construction must be bounded by parser limits.
- Style resolution must remain linear over parsed style rules for now.
- Unsupported modern features should skip cleanly instead of triggering costly
  recovery loops.
- Diagnostic dump tools cap input size and output length.

## Current UI-Oriented Computed Fields

- `display`: block, inline, inline-block, flex, grid, none.
- `margin`, `padding`, `border-width` and their physical edge longhands,
  plus `border-color`, `border-radius`.
- `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height`, including
  layout-resolved percentage values for these sizing properties.
- `color`, `background-color`.
- `font-size`, `line-height`, `text-align`, `text-indent`.
- `box-sizing`, `overflow`, `opacity`, `position`, `z-index`, `transform`.
- Embedded-app layout fields: `aspect-ratio`, `gap`, `column-gap`,
  `row-gap`, simplified grid minimum track sizing, `grid-auto-rows` minimum
  sizing and `grid-column`/`grid-row` spans.
- `box-shadow` is stored and painted as a cheap rounded translucent rectangle;
  blur is approximated, not rasterized.
