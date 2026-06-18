# Tests

Regression tests for the platform-neutral engine.

The directory contains many source files, but CMake builds them into a single
`jellyframe_core_tests` executable. The split is intentional: each file follows a
module boundary so failures are easy to locate without creating many test
binaries.

Keep tests that protect:

- Parser recovery and bounded parsing.
- DOM mutation and script-facing APIs.
- Style, render, layout and layer tree behavior.
- Dirty-region and frame-update reuse.
- Input, events, hit testing and form controls.
- Text backends and embedded framebuffer behavior.

Small files can be merged when they share the same ownership boundary, but do
not remove coverage simply to reduce file count.
