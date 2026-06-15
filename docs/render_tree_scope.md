# Render Tree Scope

Last checked against implementation sources on 2026-06-13:

- Blink layout tree sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/
- WebKit rendering tree sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/rendering
- Gecko frame tree sources:
  https://searchfox.org/firefox-main/source/layout/generic

There is no WHATWG Living Standard section that specifies a browser render tree.
This is an implementation-defined layer between DOM/style resolution and layout.
JellyFrame follows the common engine shape while keeping the data model small
enough for low-end wearable devices.

## Reference Shape

- Blink builds a layout tree around `LayoutObject` and specialized layout
  classes such as block, inline and text objects.
- WebKit uses a rendering tree centered on `RenderObject`, `RenderBlock`,
  `RenderInline` and text renderers.
- Gecko uses a frame tree around `nsIFrame` subclasses.

JellyFrame uses WebKit-like names for clarity:

```text
DOM + computed style
  -> RenderTreeBuilder
  -> RenderObject tree
  -> LayoutBox tree
  -> LayerNode tree
  -> DisplayList
```

## Implemented Model

- `RenderObjectType::View`
- `RenderObjectType::Block`
- `RenderObjectType::Inline`
- `RenderObjectType::Text`
- Each render object stores:
  - source DOM node pointer
  - computed `Style`
  - child render objects

## Phase 1 Rules

- `display:none` nodes do not create render objects.
- `head`, `script`, `style`, `meta`, `link`, `title`, `template` and `noscript`
  stay out of the render tree through default style.
- Text nodes inherit text color and font size from the parent render object.
- `inline-block` is represented as `RenderInline` for now.
- `flex` and `grid` keep block-like render object shape while preserving their
  display value in computed style. Layout then applies small dedicated
  algorithms for common flex rows and responsive grid cards.
- Layout consumes the render tree instead of walking the DOM directly.

## Deliberately Deferred

- Anonymous block/inline box generation.
- Pseudo-elements.
- List marker renderers.
- Replaced-element intrinsic sizing.
- Full CSS stacking-context semantics.
- Paint invalidation and retained display lists.
- Fragmentation and multi-column layout.
- Full CSS flex/grid layout algorithms, including named grid lines, explicit
  placement, dense packing, baseline alignment and full track sizing.

## Implemented Layout Subset

- Responsive grid cards using `display:grid`,
  `grid-template-columns: repeat(auto-fit, minmax(<length>, 1fr))`, `gap`,
  `grid-auto-rows: minmax(<length>, auto)` and `grid-column`/`grid-row:
  span N`.
- `aspect-ratio` participates in intrinsic content height for empty media/card
  boxes.
- Simplified flex rows can use `column-gap` in addition to the existing
  justification behavior.

## Usability Policy

Render tree construction must preserve functional UI boxes. A modern page may
lose blur, animation, advanced layout and compositing, but controls such as
forms, inputs, buttons, images, dialogs and cards should create render objects
with usable computed styles.
