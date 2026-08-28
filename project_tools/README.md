# Project Tools

> Last updated: 2026-08-28; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Project-maintainer tools for CI, Render Core slicing, performance guards and
HTML/CSS compatibility tables. App authors normally start with
`tools/jellyframe_cli.py` or the VS Code extension instead.

| Goal | Entry point |
| --- | --- |
| Check desktop benchmark regressions | `benchmark_guard.py` |
| Verify a generated Render Core profile and link map | `check_render_core_link_map.py` |
| Inspect or regenerate the Render Core feature catalog | `render_core_feature_registry.py` |
| Package the App Author SDK | `package_app_author_sdk.py` |
| Create a standalone Render Core source archive | `package_render_core_source.py` |
| Rehearse a history-preserving Core repository export | `rehearse_render_core_history_export.py --output-dir build/render-core-history-export` |
| Export the independent Core CI workflow | `render_core_ci.yml` is renamed to `.github/workflows/ci.yml` by the history-export rehearsal |
| Generate the HTML/CSS support tables | `generate_html_support_table.py`, `generate_css_support_table.py` |
| Import a CSS crosswork snapshot | `import_css_support_crosswork.py` |

These tools are not runtime dependencies. Their reports belong in ignored
build or review-artifact directories unless a report is explicitly promoted
to project documentation.

`package_app_author_sdk.py` is a release-maintainer tool. It produces a
versioned ZIP from validated desktop Release builds, optionally including a
scripting build. The archive contains only the App-author CLI, templates,
schema, target presets, feature registry and desktop runtime; it is not a
replacement for source, ports or a Device OS release. See
[`../docs/app_author_environment.md`](../docs/app_author_environment.md).
