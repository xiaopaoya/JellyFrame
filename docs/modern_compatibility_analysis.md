# Modern HTML/CSS Compatibility Analysis

Date: 2026-06-13

References:

- WHATWG HTML Living Standard: https://html.spec.whatwg.org/
- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSSOM: https://www.w3.org/TR/cssom-1/
- CSS Cascade: https://www.w3.org/TR/css-cascade-5/

This document compares a few intentionally modern HTML/CSS samples against
expected browser behavior and current JellyFrame behavior. The goal is not pixel
compatibility. The goal is usable degradation: modern pages should keep their
functional skeleton even when visual enhancements are skipped.

## Samples

- `examples/modern_cases/search_home.html`
- `examples/modern_cases/search_home.css`
- `examples/modern_cases/app_shell.html`
- `examples/modern_cases/app_shell.css`
- `examples/modern_cases/article_cards.html`
- `examples/modern_cases/article_cards.css`

Generated with:

```powershell
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\search_home.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\search_home.css
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\app_shell.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\app_shell.css
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\article_cards.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\article_cards.css
```

## Case 1: Search Home

Modern features used:

- `modulepreload`
- `script type="module"`
- `form role="search"`
- `input type="search"`
- boolean attributes such as `autofocus`
- `template`
- `@layer`
- `@supports`
- `:has()`
- `oklch()`, `color-mix()`, `backdrop-filter`
- pill radius and shadow declarations

Expected browser behavior:

- The browser builds a normal `html/head/body` tree.
- The module script is script text, not parsed as HTML.
- The form contains label, search input and submit button.
- The template element has inert template contents exposed through
  `template.content`, not normal rendered children.
- CSSOM keeps cascade layers, parses the feature query, and applies `:has()` if
  supported.

Current JellyFrame behavior:

- DOM keeps the functional search skeleton:
  `main -> form -> label/input/button`.
- `script type="module"` is preserved as raw text and does not corrupt the DOM.
- Boolean attributes are preserved as empty-value attributes.
- `template` is parsed as an ordinary element with children. It is hidden by
  default style, so it should not render, but `template.content` semantics do
  not exist yet.
- CSSOM flattens `@layer`.
- CSSOM skips `@supports` and the `:has()` rule.
- Fallback box styling survives:
  `#search.search-box` keeps `display`, `width`, `padding`, `background`,
  `border-radius` and `box-shadow`.
- Unsupported `oklch()` does not overwrite earlier supported color fallback.

Impact:

- No catastrophic failure.
- The search box remains a form with an input and a button.
- Visual enhancements such as blur, focus ring from `:has()`, and advanced color
  spaces are skipped.
- This matches the current usability-first policy.

## Case 2: App Shell

Modern features used:

- custom element `app-root`
- `popover` and `popovertarget`
- `dialog`
- inline `style`
- CSS custom properties
- `display: flex`
- `@container`
- `:is()` / `:focus-within`
- attribute selector `dialog[open]`

Expected browser behavior:

- Unknown custom elements remain valid elements.
- `popover` and `dialog` have browser-managed interactive behavior.
- CSS variables resolve through the cascade.
- `display: flex` lays out the topbar.
- `@container` applies only when the container condition matches.
- `:is()` and attribute selectors participate in selector matching.

Current JellyFrame behavior:

- DOM preserves the custom element, buttons, nav links, cards, dialog and form.
- Boolean `popover` is preserved as an empty attribute.
- Inline `style` text is preserved in `head`.
- CSSOM keeps `:root` custom property declarations, but style resolution does
  not resolve custom properties yet.
- CSSOM skips `@container` and `:is()` rules.
- CSSOM keeps `dialog[open]`, but the current selector matcher does not apply
  attribute selectors yet.
- `display: flex` is parsed but not applied as a supported display mode. The
  topbar falls back to block flow.

Impact:

- No parser-level catastrophic failure.
- Navigation, cards and dialog contents remain in the DOM.
- Main risk is interaction semantics, not parse integrity: popover/dialog
  behavior needs event/runtime support later.
- Visual degradation is coherent but plain: flex/container enhancements are
  skipped instead of partially applied.

## Case 3: Article Cards

Modern/common features used:

- omitted `p` and `li` end tags
- `picture/source/img`
- selector list `.story, article.featured`
- descendant selector `.story img`
- conditional `@media`
- `:where()`

Expected browser behavior:

- The first `p` is implicitly closed by the second `p`.
- `li` elements are implicitly closed by following `li` starts.
- `picture` contains `source` and `img`.
- Conditional media rules apply when the viewport matches.
- `:where()` has zero specificity but still matches when supported.
- Descendant selector `.story img` applies image sizing.

Current JellyFrame behavior:

- DOM correctly creates sibling `p` elements.
- DOM correctly creates sibling `li` elements.
- `picture`, `source` and `img` are preserved.
- CSSOM splits `.story, article.featured` into separate style rules.
- Conditional `@media (max-width: 480px)` is skipped.
- `:where()` rule is skipped.
- CSSOM keeps `.story img`, but style resolution does not yet support descendant
  selectors.

Impact:

- No catastrophic failure.
- Article text, list items and image node remain available.
- Potential functional visual issue: image sizing from `.story img` will not
  apply until descendant selector matching is implemented.
- This is a priority gap before real rendering tests.

## Overall Assessment

The current parser stack meets the basic degradation goal for these samples:

- Modern HTML is tokenized without losing the rest of the document.
- Functional DOM nodes are preserved.
- Raw script/style content does not break parsing.
- Unsupported CSS enhancement blocks are skipped cleanly.
- Fallback declarations remain available.
- CSSOM records enough metadata for cascade diagnostics.

No catastrophic parser failure was observed.

## Important Gaps

These gaps are not catastrophic at parse time, but they matter for usable
rendering:

- Descendant selector matching, for example `.story img`.
- Attribute selector matching, for example `dialog[open]`.
- Basic pseudo-class handling policy for `:root`, `:focus`, `:disabled`,
  `:checked` and `:open`.
- CSS custom property fallback resolution for simple `var(--x, fallback)`.
- More default display values for form controls, media, dialog and custom
  elements.
- Basic `inline-block`, `flex` fallback strategy, and box border/radius/shadow
  storage even before full rendering.

## Recommended Next Steps

1. Add a selector matcher module with support for compound, descendant and
   simple attribute selectors.
2. Add a small UA stylesheet for controls and common HTML elements.
3. Add computed-style fields for border, border radius, box shadow and overflow,
   even if rendering initially ignores some of them.
4. Add simple CSS variable resolution for direct custom property lookups and
   fallback arguments.
5. Add a combined document demo that parses HTML, extracts `style` text and
   linked sample CSS, builds CSSOM, resolves styles and dumps selected computed
   styles for functional nodes such as forms, inputs and buttons.

