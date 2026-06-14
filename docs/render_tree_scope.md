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
WearWeb follows the common engine shape while keeping the data model small
enough for low-end wearable devices.

## Reference Shape

- Blink builds a layout tree around `LayoutObject` and specialized layout
  classes such as block, inline and text objects.
- WebKit uses a rendering tree centered on `RenderObject`, `RenderBlock`,
  `RenderInline` and text renderers.
- Gecko uses a frame tree around `nsIFrame` subclasses.

WearWeb uses WebKit-like names for clarity:

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
- `flex` and `grid` are represented as block-like render objects for layout
  fallback, while preserving their display value in computed style.
- Layout consumes the render tree instead of walking the DOM directly.

## Deliberately Deferred

- Anonymous block/inline box generation.
- Pseudo-elements.
- List marker renderers.
- Replaced-element intrinsic sizing.
- Full CSS stacking-context semantics.
- Paint invalidation and retained display lists.
- Fragmentation and multi-column layout.
- True flex/grid layout algorithms.

## Usability Policy

Render tree construction must preserve functional UI boxes. A modern page may
lose blur, animation, advanced layout and compositing, but controls such as
forms, inputs, buttons, images, dialogs and cards should create render objects
with usable computed styles.
