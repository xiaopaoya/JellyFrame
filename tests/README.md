# Tests

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This directory contains cross-subproject fixtures and desktop tool regression
tests. Start from the test owner when changing code; start from the fixture map
when reproducing an app or interaction issue. The C++ unit suites remain beside
their production modules so ownership and build dependencies stay obvious.

## Choose By Goal

| Goal | Directory | What it proves |
| --- | --- | --- |
| Parser, style, layout, paint or input behavior | `../src/render_core/tests` | Platform-neutral Render Core contracts |
| App lifecycle, services, storage or package state | `../src/app_runtime/tests` | Platform-neutral runtime contracts |
| JerryScript bindings and script-task protocol | `../src/script/tests` | Optional scripting behavior and value-only boundaries |
| CLI, packer, schemas, profiles and Win32 shell | `tool_regression/` | Developer-tool and desktop acceptance contracts |
| Reproduce a bounded cross-module UI case | `fixtures/apps/` | Small deterministic app input/output cases |

## Fixture Map

The fixtures are intentionally diagnostic, not polished samples:

- `jelly_flex_grid_probe`: Flex/Grid layout geometry.
- `jelly_scroll_probe`, `jelly_scroll_blit_probe` and
  `jelly_scroll_container_probe`: wheel, strip-blit, drag, inertia and edge
  stopping behavior.
- `jelly_dialog_modal`: dialog escape/light-dismiss behavior.
- `jelly_opacity_layer_reuse`: layer reuse and opacity invalidation.
- `jelly_budget_spam`: bounded budget rejection and recovery.
- `jelly_service_spam`: service queue pressure and completion handling.
- `jelly_watchdog_smoke`: intentional endless script for watchdog recovery.
- `jelly_svg_icon`: package-time SVG rasterization boundary.

Use `samples/apps/packages/` for readable app examples and `tools/templates/`
for new app starting points; do not publish a fixture as a design example.

Current subproject tests live next to their owners:

- `../src/render_core/tests`: render pipeline, DOM, CSS, layout, layer,
  framebuffer, input and diagnostics tests.
- `../src/app_runtime/tests`: app-runtime host-service queue and handle tests.
- `../src/script/tests`: optional JerryScript bridge tests.
