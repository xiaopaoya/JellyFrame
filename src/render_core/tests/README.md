# Render Core Tests

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

These tests belong to `jellyframe_render_core` and must stay independent from
JerryScript, app packaging, app registries, network/storage services and OS APIs.

CTest target: `jellyframe_render_core_tests`.

Group failures by pipeline area before editing:

- HTML/DOM: parser, tokenizer, tree-builder and DOM fixtures.
- CSS/style/layout: selector, computed-style, flex/grid and form geometry.
- Paint/output: display commands, rounded geometry, gradients, text and
  framebuffer conversion.
- Runtime interaction: events, hit testing, focus, scroll and dirty-region
  invalidation.

Use `tests/fixtures/apps/` for a cross-module reproducer and
`tools/native/README.md` for the matching desktop dump or capture tool.
