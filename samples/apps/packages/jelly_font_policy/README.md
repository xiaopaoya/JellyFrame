# Jelly Font Policy

Small package used to validate the app font policy path:

- CSS declares `font-family: "Jelly Tiny", system-ui, sans-serif`.
- `jellyframe.app.json` declares the same family and points to a package-local
  `.jffont` supplement.
- `jellyframe_cli.py check` reports the family as a manifest runtime font and
  validates glyph coverage before install.

The font is deliberately tiny and only exists for deterministic package/tool
tests. Product apps should generate their own `.jffont` subsets from licensed
bitmap fonts.
