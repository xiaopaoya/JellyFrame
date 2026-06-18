# Examples

This directory contains desktop tools and sample pages used to inspect
JellyFrame output.

- `*_dump.cpp` tools print parser, DOM, CSSOM, render tree, layer tree or full
  pipeline output.
- `pseudo_browser.cpp` runs the platform-neutral pipeline in a desktop shell.
- `win32_browser.cpp` is the Windows validation shell with OS input and capture
  support.
- `apps/` contains small end-user app examples.
- `app_cases/`, `modern_cases/`, `font_cases/` and `script_cases/` contain
  targeted compatibility samples.

Examples may use desktop file I/O. The embedded core does not depend on these
entry points.
