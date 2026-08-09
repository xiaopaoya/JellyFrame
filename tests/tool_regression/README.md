# Tool Regression Tests

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This directory contains lightweight regression tests for developer tooling.
They focus on package/check behavior that should stay stable independently from
the render-core and app-runtime C++ unit suites.

## Choose By Goal

| Goal | Tests |
| --- | --- |
| Manifest, resource and package safety | `package_preflight_tests.py`, `package_image_fixture_tests.py` |
| App install/update/delete/rollback | `app_registry_tests.py`, `win32_browser_cli_tests.py` |
| HTML/CSS audit and profile declarations | `html_support_table_tests.py`, `css_support_table_tests.py`, `render_core_feature_profile_tests.py`, `render_core_feature_registry_tests.py` |
| Visual diagnostics and layout captures | `pipeline_visual_diagnostics_tests.py`, `flex_grid_capture_tests.py` |
| Template and external-author workflow | `template_trial_tests.py` |
| Font/resource policy | `font_policy_report_tests.py` |
| Link-map and build slicing | `render_core_link_map_tests.py` |

Run one file directly only when narrowing a failure; prefer the named CTest
test because it supplies the correct build executable and working directory.

`package_image_fixture_tests.py` is a cross-tool acceptance check: it runs the
CLI package preflight over the weather sample, verifies `imageDiagnostics`, then
captures the same package through the Win32 shell and inspects the BMP pixels so
package-local images cannot regress into invisible placeholders.

`font_policy_report_tests.py` validates the app-font path. It checks that the
font policy sample reports two usable `.jffont` runtime families, keeps the
intentional missing-glyph warning stable and captures the sample through Win32
with `--use-app-fonts`.

`win32_browser_cli_tests.py` checks CLI/help/error contracts for the interactive
Win32 shell, including registry install/update/rollback/enable/disable command mode, the
authorized file-broker smoke command and the bad-app system survival smoke.

`jellyframe_cli_external_trial_evidence` is a Windows-only CTest that runs the
clean-directory `jellyframe_cli.py trial` flow. It keeps the official trial
package diagnostics, expected capability rejection, package previews and
install/update/rollback recovery in one reproducible evidence directory.

Generated reports and screenshots must stay under `build*/test_outputs` or
`out/`; do not commit those outputs.
