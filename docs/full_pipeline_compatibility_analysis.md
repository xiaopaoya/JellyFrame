# Full Pipeline Compatibility Analysis

Date: 2026-06-14

This document analyzes the current end-to-end pipeline:

```text
HTML -> DOM
CSS -> CSSOM
DOM + CSSOM -> computed style
computed style -> render tree
render tree -> layout tree
layout tree -> layer tree
layer tree -> display list
```

## Samples

- `examples/modern_cases/search_home.html` + `.css`
- `examples/modern_cases/app_shell.html` + `.css`
- `examples/modern_cases/article_cards.html` + `.css`

Commands:

```powershell
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css 360
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\app_shell.html examples\modern_cases\app_shell.css 360
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\article_cards.html examples\modern_cases\article_cards.css 360
```

## Search Home

Modern browser expectation:

- `head`, `script`, metadata and `template` do not create ordinary visual boxes.
- `form`, `input` and `button` create functional UI boxes.
- `@supports`, `:has()`, advanced colors and blur enhance the visual style but
  are not required for the form to be usable.

WearWeb result:

- `dom_nodes=21`
- `render_objects=10`
- `layout_boxes=10`
- `layers=2`
- `display_commands=14`
- Display list includes:
  - background rectangles
  - search form rectangle
  - input background and border rectangles
  - button background and border rectangles
  - text commands for labels/buttons

Assessment:

- No catastrophic failure.
- Functional search UI survives as a box, input and button.
- Missing visual features are acceptable degradation: blur, focus `:has()` ring,
  advanced colors and rounded-corner rasterization are not essential.

## App Shell

Modern browser expectation:

- Custom elements create ordinary boxes.
- `popover` needs runtime/event support.
- `dialog` is not rendered unless it has `open` or CSS explicitly makes it
  visible.
- `@container` and `:is()` may enhance layout and focus styling.

WearWeb result after correction:

- `dom_nodes=40`
- `render_objects=29`
- `layout_boxes=29`
- `layers=1`
- `display_commands=18`
- The closed `dialog` is filtered from the render tree by default.
- Header button, nav links and metric cards remain visible.

Assessment:

- No catastrophic failure.
- The closed dialog bug was fixed because it would have shown UI that a modern
  browser keeps hidden.
- Runtime behavior for popover/dialog is still not implemented, but parsing and
  visual fallback are coherent.

## Article Cards

Modern browser expectation:

- Optional `p` and `li` end tags produce sibling paragraphs/list items.
- `picture/source/img` are preserved.
- Descendant selector `.story img` styles the image.
- Conditional media and `:where()` may enhance small viewport styling.

WearWeb result:

- `dom_nodes=22`
- `render_objects=22`
- `layout_boxes=22`
- `layers=1`
- `display_commands=8`
- Paragraph and list-item implied closures are preserved.
- `.story img` now applies through descendant selector matching.

Assessment:

- No catastrophic failure.
- Article content and image node survive.
- Image sizing now reaches computed style and layout.
- Conditional media and `:where()` are skipped as intended.

## Embedded-Oriented Observations

- Display list is still simple: fill rectangles and text commands.
- Borders are decomposed into four fill rectangles, suitable for framebuffer
  backends with no path rasterizer.
- Render tree filters non-visual nodes before layout, reducing layout work.
- File-based diagnostic tools cap input at 512 KiB.
- Parser limits remain in place for DOM and CSS.

## Known Non-Fatal Gaps

- No real text shaping, bidi or full font fallback.
- Platform-neutral input dispatch exists, but browser-like loading, network and
  framework-facing DOM APIs remain intentionally small.
- Flex/grid are still subsets: useful flex rows and responsive grid cards work,
  while full CSS algorithms, subgrid, explicit placement and dense packing are
  deferred.
- No retained layout or dirty rectangle invalidation.
- Rounded fills and cheap shadows exist, but no true shadow blur or advanced
  filter pipeline.

## Next Functional Priorities

1. Dirty layer invalidation and rectangle repaint.
2. Host frame sink and deployable embedded framebuffer backend.
3. Arena allocation and compact DOM/layout object storage.
4. Text shaping/font fallback strategy for non-Latin production devices.
5. Selector/module additions only when embedded apps prove they need them.
