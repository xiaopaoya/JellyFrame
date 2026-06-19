# Native Tools

This directory contains native C++ desktop tools used to inspect JellyFrame
output. Sample pages and app packages live in `../../samples`.

- `*_dump.cpp` tools print parser, DOM, CSSOM, render tree, layer tree or full
  pipeline output.
- `pseudo_browser.cpp` runs the platform-neutral pipeline in a desktop shell and
  can emit structured pipeline diagnostics with `--diagnostics-json`.
- `win32_browser.cpp` is the Windows validation shell with OS input and capture
  support. It can open either loose HTML/CSS files or a source package via
  `--app`.

Native tools may use desktop file I/O. The embedded core does not depend on these
entry points.
