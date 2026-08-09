# App Test Fixtures

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

These are small deterministic reproducer packages, not polished app examples.
Choose the fixture by the contract under investigation:

| Area | Fixtures |
| --- | --- |
| Layout | `jelly_flex_grid_probe` |
| Scroll and presentation | `jelly_scroll_probe`, `jelly_scroll_blit_probe`, `jelly_scroll_container_probe` |
| Modal/input behavior | `jelly_dialog_modal` |
| Layer invalidation | `jelly_opacity_layer_reuse` |
| Budget/recovery | `jelly_budget_spam`, `jelly_watchdog_smoke` |
| Host service pressure | `jelly_service_spam` |
| Package-time SVG boundary | `jelly_svg_icon` |

Run the fixture's `.jfcapture` with the Win32 shell described in
`../../../tools/native/README.md`, or use the corresponding CTest target.
Keep additions minimal and diagnostic; visual examples belong in
`samples/apps/packages/` or `tools/templates/apps/`.
