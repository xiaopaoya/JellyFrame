# Tool Regression Tests

This directory contains lightweight regression tests for developer tooling.
They focus on package/check behavior that should stay stable independently from
the render-core and app-runtime C++ unit suites.

`package_image_fixture_tests.py` is a cross-tool acceptance check: it runs the
CLI package preflight over the weather sample, verifies `imageDiagnostics`, then
captures the same package through the Win32 shell and inspects the BMP pixels so
package-local images cannot regress into invisible placeholders.

Generated reports and screenshots must stay under `build*/test_outputs` or
`out/`; do not commit those outputs.
