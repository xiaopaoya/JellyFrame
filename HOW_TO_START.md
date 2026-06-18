# How To Start With JellyFrame

This guide is for developers opening the repository for the first time. It
explains what JellyFrame is, how the project is organized, how to build and run
it, and what every generated desktop executable is for.

## Table Of Contents

- [1. What JellyFrame Is](#1-what-jellyframe-is)
- [2. What JellyFrame Is Not](#2-what-jellyframe-is-not)
- [3. How To Read The Project](#3-how-to-read-the-project)
- [4. Repository Layout](#4-repository-layout)
- [5. Engine Pipeline](#5-engine-pipeline)
- [6. Build Requirements](#6-build-requirements)
- [7. Build The Default Project](#7-build-the-default-project)
- [8. Run Tests And Benchmarks](#8-run-tests-and-benchmarks)
- [9. Render Pages And Inspect The Pipeline](#9-render-pages-and-inspect-the-pipeline)
- [10. Optional JerryScript Build](#10-optional-jerryscript-build)
- [11. What The Release EXEs Do](#11-what-the-release-exes-do)
- [12. Useful Example Pages](#12-useful-example-pages)
- [13. Documentation Map](#13-documentation-map)
- [14. Common Development Workflows](#14-common-development-workflows)
- [15. Rules For Updating This Guide](#15-rules-for-updating-this-guide)

## 1. What JellyFrame Is

JellyFrame is a small C++ document UI engine for embedded and wearable devices.
It keeps the useful shape of a browser pipeline:

```text
HTML -> DOM -> CSSOM -> style -> render tree -> layout -> layer tree
     -> display list -> software rasterizer/compositor -> host framebuffer
```

Optional JerryScript support adds a small app runtime on top:

```text
classic script -> JerryScriptRuntime -> DOM/event/form/timer bindings
```

The core goal is not browser compatibility. The goal is useful degradation:
common app-style HTML/CSS should keep structure, controls and readable styling
even when expensive web-platform features are skipped.

## 2. What JellyFrame Is Not

JellyFrame deliberately does not provide:

- network loading, `fetch`, XHR or WebSocket;
- browser storage such as cookies, localStorage or IndexedDB;
- full DOM, query selector APIs, Shadow DOM or Web Components;
- ES modules or browser loading algorithms;
- Canvas, SVG, image decoding or video;
- full Flexbox/Grid/positioning/animation/filter behavior;
- GPU compositing or pixel-compatible browser rendering.

Before relying on a feature, check
[docs/developer_capability_matrix.md](docs/developer_capability_matrix.md).

## 3. How To Read The Project

For a first pass:

1. Read [README.md](README.md) for the short project pitch.
2. Read this file for build and tool usage.
3. Read [docs/engine_architecture.md](docs/engine_architecture.md) to understand
   the pipeline.
4. Read [docs/developer_capability_matrix.md](docs/developer_capability_matrix.md)
   before writing pages for the engine.
5. Read the module scope document for the area you want to change:
   tokenizer, tree builder, CSS parser, CSSOM, render tree, layer tree,
   renderer, events, scripting, text backend or HAL.
6. Use the dump tools to inspect what the engine actually produced.

For embedded porting:

1. Read [docs/embedded_hal_api.md](docs/embedded_hal_api.md).
2. Read [docs/porting_work_guide.md](docs/porting_work_guide.md).
3. Start from `jellyframe_embedded_host_demo`.
4. Use `ports/virtual_board` to estimate framebuffer and flush behavior before
   a real board is ready.

## 4. Repository Layout

- `src/core`: platform-neutral engine core.
- `src/script`: optional JerryScript binding layer.
- `examples`: desktop tools, pseudo browser, Win32 shell and sample pages.
- `examples/modern_cases`: modern HTML/CSS compatibility samples.
- `examples/script_cases`: scripting acceptance probes.
- `examples/app_cases`: weather, clock, timer and calculator app samples.
- `examples/font_cases`: font-pack sample input.
- `tests`: platform-neutral regression test sources.
- `benchmarks`: microbenchmarks.
- `ports/virtual_board`: desktop estimator for board-like framebuffer costs.
- `ports/esp32s3-idf`: ESP32-S3 reference bring-up project.
- `docs`: technical documentation and module/API contracts.
- `project_docs`: current status, roadmap and development process notes.

## 5. Engine Pipeline

The core modules roughly map to these files:

- HTML tokenizer: `src/core/html_tokenizer.*`
- HTML tree builder/parser: `src/core/html_tree_builder.*`,
  `src/core/html_parser.*`
- DOM and dirty flags: `src/core/dom.*`
- CSS parser and CSSOM data: `src/core/css_parser.*`, `src/core/style.*`
- Linked style/script collection helpers: `src/core/document_style.*`,
  `src/core/document_script.*`
- Render tree and layout: `src/core/render_tree.*`, `src/core/layout.*`
- Layer tree and hit testing: `src/core/layer_tree.*`,
  `src/core/hit_test.*`
- Events and input: `src/core/event.*`, `src/core/input.*`
- Form controls: `src/core/form_control.*`
- Software rendering: `src/core/software_renderer.*`
- Text backend contract: `src/core/text_backend.*`
- Host/HAL contracts and budgets: `src/core/host.h`, `src/core/budget.h`
- Embedded framebuffer adapter: `src/core/embedded_framebuffer.*`
- Frame update and dirty rectangles: `src/core/frame_update.*`,
  `src/core/dirty_region.*`
- Optional scripting: `src/script/jerryscript_runtime.*`

The current implementation favors simple data structures, bounded recovery,
explicit budgets and predictable fallback over complete web-platform behavior.

## 6. Build Requirements

Required:

- CMake
- A C++17 compiler
- On Windows, Visual Studio Build Tools or Visual Studio with MSVC

Optional:

- JerryScript source checkout and built libraries for scripting builds
- A BDF bitmap font when generating embedded font packs; use
  `jellyframe_capability_check --font-budget WxH` first to choose a font profile
- ESP-IDF when working on `ports/esp32s3-idf`

The commands below use PowerShell because the current workspace is Windows.

## 7. Build The Default Project

Configure and build:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Default options:

- `JELLYFRAME_BUILD_EXAMPLES=ON`
- `JELLYFRAME_BUILD_TESTS=ON`
- `JELLYFRAME_BUILD_BENCHMARKS=ON`
- `JELLYFRAME_BUILD_SCRIPTING=OFF`

Library-only or embedded-oriented build:

```powershell
cmake -S . -B build-core `
  -DJELLYFRAME_BUILD_EXAMPLES=OFF `
  -DJELLYFRAME_BUILD_TESTS=OFF `
  -DJELLYFRAME_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

## 8. Run Tests And Benchmarks

Run the regression suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Run the core microbenchmark:

```powershell
.\build\Release\jellyframe_microbench.exe
```

Run the virtual board benchmark:

```powershell
.\build\Release\jellyframe_virtual_bench.exe 300 300 60 200 80 0.85 40
```

Use Release builds for meaningful performance numbers.

## 9. Render Pages And Inspect The Pipeline

Render a page to BMP or PPM without opening a window:

```powershell
.\build\Release\jellyframe_pseudo_browser.exe `
  examples\modern_cases\article_cards.html `
  examples\modern_cases\article_cards.css `
  article_cards.bmp 390 640
```

Open a Windows interactive shell:

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css
```

Capture through the Win32/GDI text path:

```powershell
.\build\Release\jellyframe_win32_browser.exe --capture `
  calculator.bmp `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css `
  390 640
```

Inspect intermediate structures:

```powershell
.\build\Release\jellyframe_dom_dump.exe examples\modern_cases\search_home.html
.\build\Release\jellyframe_cssom_dump.exe examples\modern_cases\search_home.css
.\build\Release\jellyframe_style_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_render_tree_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_layer_tree_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
.\build\Release\jellyframe_pipeline_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css
```

Scan a page before embedding it:

```powershell
.\build\Release\jellyframe_capability_check.exe `
  examples\app_cases\weather.html `
  examples\app_cases\weather.css `
  examples\app_cases\weather.js
```

## 10. Optional JerryScript Build

Fetch and build JerryScript:

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean
```

Configure JellyFrame with scripting:

```powershell
cmake -S . -B build-script `
  -DJELLYFRAME_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

Run a scripted page:

```powershell
.\build-script\Release\jellyframe_pseudo_browser.exe `
  examples\app_cases\weather.html `
  examples\app_cases\weather.css `
  weather.bmp 360 360 --script examples\app_cases\weather.js
```

Run a timer-driven page:

```powershell
.\build-script\Release\jellyframe_pseudo_browser.exe `
  examples\app_cases\clock.html `
  examples\app_cases\clock.css `
  clock.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
```

The scripting shells automatically collect inline classic scripts and
host-loadable local classic `<script src>` files. `--script extra.js` is also
available for examples that keep JavaScript beside the page instead of linking it
from HTML.

## 11. What The Release EXEs Do

All executable names below are produced under `build\Release` when the relevant
CMake options are enabled.

| Executable | Purpose |
| --- | --- |
| `jellyframe_demo.exe` | Small console demo for the current core pipeline slice. |
| `jellyframe_dom_dump.exe` | Prints tokenizer output and an ASCII DOM tree. Useful when markup is parsed unexpectedly. |
| `jellyframe_cssom_dump.exe` | Prints parsed CSS rules, specificity and declarations. Useful for parser and cascade debugging. |
| `jellyframe_style_dump.exe` | Resolves and prints computed styles for functional UI nodes. |
| `jellyframe_render_tree_dump.exe` | Prints the render tree generated from DOM and style. |
| `jellyframe_layer_tree_dump.exe` | Prints layer boundaries, layer reasons, clips and flattened display-list counts. |
| `jellyframe_pipeline_dump.exe` | Prints end-to-end DOM/render/layout/layer/display-list counts and a display-list preview. |
| `jellyframe_pseudo_browser.exe` | Runs the full pipeline and writes a BMP/PPM image. It is the best non-interactive acceptance shell. |
| `jellyframe_win32_browser.exe` | Windows-only interactive validation shell using Win32/GDI text measurement and painting. |
| `jellyframe_capability_check.exe` | Scans HTML/CSS/JS for supported subsets, degraded features and unsupported APIs. Also supports font coverage checks. |
| `jellyframe_font_pack_gen.exe` | Converts a BDF bitmap font and used-character list into a C++ `BitmapFont` header. |
| `jellyframe_embedded_host_demo.exe` | Platform-neutral bring-up demo using static resources, bitmap text and RGB565 framebuffer output. |
| `jellyframe_microbench.exe` | Runs parser/render/layout/layer/flatten microbenchmarks. |
| `jellyframe_virtual_bench.exe` | Runs a desktop virtual-board benchmark and estimates RGB565 flush costs. |
| `jellyframe_core_tests.exe` | Single platform-neutral regression test executable. |

Notes:

- `jellyframe_pseudo_browser.exe` also supports `--app package_dir output.ppm`
  for M11 app-package source directories containing `jellyframe.app.json`.
- `jellyframe_win32_browser.exe` is only built on Windows.
- In scripting builds, `jellyframe_pseudo_browser.exe`,
  `jellyframe_win32_browser.exe` and `jellyframe_core_tests.exe` link the
  optional scripting target.
- Inspection tools and the Win32 shell merge CSS from explicit CSS files,
  embedded `<style>` and host-loadable local `<link rel="stylesheet">` files.
  The core itself does not perform file I/O.

## 12. Useful Example Pages

- `examples/apps/watch_weather`: first M11 app package sample with
  `jellyframe.app.json`, local HTML/CSS/classic JS and a declared network
  capability for future runtime data requests.
- `examples/modern_cases`: modern HTML/CSS samples that exercise graceful
  degradation.
- `examples/script_cases`: minimal scripting probes for runtime, DOM mutation,
  events and script loading.
- `examples/app_cases/weather.*`: select-driven weather panel.
- `examples/app_cases/clock.*`: timer-driven clock.
- `examples/app_cases/timer.*`: timer/stopwatch UI.
- `examples/app_cases/calculator.*`: button-driven calculator.

Package the M11 sample into a generated resource table:

```powershell
python tools\jellyframe_cli.py validate `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json
```

```powershell
python tools\jellyframe_cli.py package `
  --root examples\apps\watch_weather `
  --target round-300 `
  --output-cpp build\watch_weather_resources.cpp `
  --report build\watch_weather_report.json `
  --debug-dir build\watch_weather.jfdir
```

Render it through the pseudo browser:

```powershell
python tools\jellyframe_cli.py preview `
  --root examples\apps\watch_weather `
  --target round-300 `
  --output build\watch_weather.ppm
```

Run package validation and capability checks together:

```powershell
python tools\jellyframe_cli.py check `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json `
  --font-budget 16x16
```

Collect the package's non-ASCII characters for an embedded bitmap font pack:

```powershell
python tools\jellyframe_cli.py font `
  --root examples\apps\watch_weather `
  --target round-300 `
  --report build\watch_weather_report.json `
  --used-chars build\watch_weather_used_chars.txt `
  --font-budget 16x16
```

Print the manifest schema path for editor setup:

```powershell
python tools\jellyframe_cli.py schema --print-path
```

List target presets:

```powershell
python tools\jellyframe_cli.py targets
```

List and create source-package templates:

```powershell
python tools\jellyframe_cli.py templates
python tools\jellyframe_cli.py new `
  --template calculator `
  --output build\my_calculator `
  --id org.example.calculator `
  --name Calculator `
  --target round-300
```

The optional VS Code helper lives in `tools/vscode-jellyframe`. It adds schema
association for `jellyframe.app.json` and command-palette wrappers around the
same CLI commands; it does not replace the CLI or duplicate the packer.

When an example renders badly, inspect in this order:

1. `jellyframe_capability_check`
2. `jellyframe_dom_dump`
3. `jellyframe_cssom_dump`
4. `jellyframe_style_dump`
5. `jellyframe_render_tree_dump`
6. `jellyframe_layer_tree_dump`
7. `jellyframe_pipeline_dump`
8. `jellyframe_pseudo_browser` or `jellyframe_win32_browser --capture`

## 13. Documentation Map

The documentation index is [docs/README.md](docs/README.md).

Most important technical docs:

- [docs/developer_capability_matrix.md](docs/developer_capability_matrix.md)
- [docs/engine_architecture.md](docs/engine_architecture.md)
- [docs/html_tokenizer_scope.md](docs/html_tokenizer_scope.md)
- [docs/html_tree_builder_scope.md](docs/html_tree_builder_scope.md)
- [docs/css_parser_scope.md](docs/css_parser_scope.md)
- [docs/cssom_scope.md](docs/cssom_scope.md)
- [docs/render_tree_scope.md](docs/render_tree_scope.md)
- [docs/layer_tree_scope.md](docs/layer_tree_scope.md)
- [docs/software_renderer_scope.md](docs/software_renderer_scope.md)
- [docs/events_scope.md](docs/events_scope.md)
- [docs/scripting_scope.md](docs/scripting_scope.md)
- [docs/text_backend.md](docs/text_backend.md)
- [docs/embedded_hal_api.md](docs/embedded_hal_api.md)

Active process docs:

- [project_docs/project_status.md](project_docs/project_status.md)
- [project_docs/roadmap.md](project_docs/roadmap.md)
- [docs/run_loop_contract.md](docs/run_loop_contract.md)
- [project_docs/memory_management.md](project_docs/memory_management.md)
- [project_docs/dom_arena_feasibility.md](project_docs/dom_arena_feasibility.md)
- [CHANGELOG.md](CHANGELOG.md)

## 14. Common Development Workflows

When adding HTML parser behavior:

1. Update tokenizer/tree-builder tests.
2. Run `jellyframe_dom_dump` on a reduced page.
3. Update the relevant scope docs if the supported subset changes.

When adding CSS support:

1. Keep unsupported values from overwriting supported fallbacks.
2. Add parser/style resolver tests.
3. Run `jellyframe_cssom_dump` and `jellyframe_style_dump`.
4. Update the capability matrix and capability checker.

When changing layout or rendering:

1. Add a focused regression test.
2. Compare `render_tree_dump`, `layer_tree_dump` and `pipeline_dump`.
3. Render a BMP through `jellyframe_pseudo_browser`.
4. Use Win32 capture when text quality matters.

When adding scripting APIs:

1. Ensure the C++ core can honor the behavior without browser services.
2. Keep wrappers as non-owning views over native DOM nodes.
3. Retain and release every JerryScript value deliberately.
4. Add scripting tests and update [docs/scripting_scope.md](docs/scripting_scope.md).

## 15. Rules For Updating This Guide

- Keep this file practical. It should help a new developer run and inspect the
  project without reading the entire repository first.
- Update the executable table whenever CMake targets change.
- Update the documentation map when docs are moved, archived or added.
- Maintain the Chinese version, [HOW_TO_START_zh.md](HOW_TO_START_zh.md), in the
  same change.
