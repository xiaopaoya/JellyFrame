# ESP32-S3 Resources

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Static resources compiled into the ESP32-S3 bring-up project.

Resource generation should remain bounded and repeatable so small-device memory
usage can be reviewed before flashing.

`app/` is source input for the checked-in smoke and acceptance fixtures. The
generated C++ resource table belongs in the isolated build directory and must
not be hand-edited or committed. Change the app manifest and source files here,
then let the port CMake resource step regenerate the bundle.
