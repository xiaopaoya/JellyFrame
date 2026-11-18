# Signed ordinary installed worker qualification

Last updated: 2026-11-19; Applies to: 0.6.0-dev.

This host suite installs a format0 classic-script package over a mocked NOR
partition using the real store, transaction and mbedTLS signature verifier.
After reopening the store it checks scalar route selection, frame13, firmware
system text and BMP pixels, pointer-triggered updates, and identical frame
replay after app, store and resource teardown. The fixture uses a public test
key; target firmware trust is unchanged.

Configure with Clang, a Python with `cryptography`, `JERRYSCRIPT_ROOT`, and
`JFSG_MBEDTLS_SOURCE_DIR` as in [signed_shaped](../signed_shaped/README.md).
Use `JELLYFRAME_RENDER_CORE_PROVIDER=package`, point
`JELLYFRAME_RENDER_CORE_PACKAGE_DIR` to an installed ABI9 Core, and pass an
exact `JELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE`. The Runtime production
lock remains ABI4. Contextual capabilities are not required: they can all be
OFF when the installed profile has them disabled. Use the `signed_scalar`
source directory and run `ctest -R '^jfsg_scalar_'` in the build directory.

The standalone test covers host execution, not physical flash or timing.
Explicit format1 contextual execution has its own `signed_shaped` suite.
