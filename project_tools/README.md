# Project Tools

> Last updated: 2026-08-10; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Project-maintainer tools for CI, Render Core slicing, performance guards and
HTML/CSS compatibility tables. App authors normally start with
`tools/jellyframe_cli.py` or the VS Code extension instead.

| Goal | Entry point |
| --- | --- |
| Check desktop benchmark regressions | `benchmark_guard.py` |
| Verify a generated Render Core profile and link map | `check_render_core_link_map.py` |
| Inspect or regenerate the Render Core feature catalog | `render_core_feature_registry.py` |
| Generate the HTML/CSS support tables | `generate_html_support_table.py`, `generate_css_support_table.py` |
| Import a CSS crosswork snapshot | `import_css_support_crosswork.py` |

These tools are not runtime dependencies. Their reports belong in ignored
build or review-artifact directories unless a report is explicitly promoted
to project documentation.
