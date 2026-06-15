# WearWeb Engine

WearWeb Engine is a deeply trimmed C++ HTML/CSS runtime intended for low-power
wearable devices. The first milestone is not a full browser. It is a small,
portable document UI engine that can later host JavaScript through JerryScript.

## Current slice

- Minimal HTML tokenizer/parser
- DOM tree
- DOM mutation primitives with dirty invalidation for future JavaScript bindings
- Tiny CSS parser
- Basic cascade for tag, class, id and inline style
- Vertical block layout with simplified inline flow and wrapping
- Sparse layer tree with flattening to a platform-neutral display list
- CPU software rasterizer/compositor with BMP/PPM output for validation
- Platform-neutral embedded framebuffer adapter for RGBA8888/BGRA8888, RGB565,
  RGB332, Gray8 and 1-bit monochrome targets
- Core hit testing and DOM-style event dispatch
- Platform-neutral pointer/wheel input controller
- Lightweight platform-neutral form-control state for text inputs, textareas,
  checkboxes, radios, ranges and selects
- Optional JerryScript runtime shell for script evaluation, kept outside
  `wearweb_core`
- Optional JerryScript DOM/event/form bridge for small host-driven apps
- Embedded-app DOM helpers such as `dataset`, `children`, `parentElement`,
  simple `matches`/`closest`, small `element.style`, `hidden` and `disabled`
- Automatic classic document script loading in scripting builds, with local
  external scripts supplied by shell callbacks
- Desktop capability checker for scanning unsupported/degraded HTML/CSS/JS
- Optional platform text measurement and paint callbacks for desktop validation
- Windows-only interactive browser shell for observation and testing
- Console demo

## Intended architecture

```text
JS app
  |
JerryScript binding layer
  |
DOM + style + layout core
  |
Layer tree
  |
Display list / retained platform layers
  |
Platform renderer: CPU framebuffer, GDI, SDL, LVGL, custom wearable HAL
```

The core is deliberately independent from windowing, GPU APIs, fonts, networking
and operating system services.

## Build

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\wearweb_demo.exe
```

Default CMake options build examples, tests and benchmarks:

- `WEARWEB_BUILD_EXAMPLES=ON`
- `WEARWEB_BUILD_TESTS=ON`
- `WEARWEB_BUILD_BENCHMARKS=ON`
- `WEARWEB_BUILD_SCRIPTING=OFF`

For an embedded or library-only build:

```powershell
cmake -S . -B build-core -DWEARWEB_BUILD_EXAMPLES=OFF -DWEARWEB_BUILD_TESTS=OFF -DWEARWEB_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

Optional JerryScript runtime support is built as a separate `wearweb_script`
target. It is disabled by default so embedded/core builds never pull in
JerryScript headers or libraries accidentally:

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean

cmake -S . -B build-script `
  -DWEARWEB_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

Scripting support evaluates JavaScript, exposes a tiny `window`/`document`
bridge and lets scripts mutate the native DOM through `getElementById`,
`createElement`, `createTextNode`, `appendChild`, `removeChild`, attributes and
`textContent`. It also bridges `addEventListener` / `removeEventListener` into
the existing C++ event flow and exposes lightweight form-control properties
(`value`, `checked`, `selectedIndex`). Host-pumped timers expose `setTimeout`,
`clearTimeout`, `setInterval` and `clearInterval`. Scripting example shells can
also collect and run inline classic `<script>` and local external
`<script src>` files through host callbacks.

Run the regression suite through CTest:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## Examples

```powershell
.\build\Debug\wearweb_demo.exe
.\build\Debug\wearweb_dom_dump.exe
.\build\Debug\wearweb_dom_dump.exe path\to\page.html
.\build\Debug\wearweb_cssom_dump.exe
.\build\Debug\wearweb_cssom_dump.exe path\to\style.css
.\build\Debug\wearweb_style_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_render_tree_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_layer_tree_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_pipeline_dump.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_pseudo_browser.exe path\to\page.html path\to\style.css output.bmp 360 240
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\runtime_probe.html examples\script_cases\runtime_probe.css output.bmp 360 240 --script examples\script_cases\runtime_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\dom_mutation_probe.html examples\script_cases\dom_mutation_probe.css output.bmp 360 260 --script examples\script_cases\dom_mutation_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\script_cases\event_probe.html examples\script_cases\event_probe.css output.bmp 360 260 --script examples\script_cases\event_probe.js
.\build\Debug\wearweb_pseudo_browser.exe examples\app_cases\weather.html examples\app_cases\weather.css output.bmp 360 360 --script examples\app_cases\weather.js
.\build\Debug\wearweb_pseudo_browser.exe examples\app_cases\clock.html examples\app_cases\clock.css output.bmp 360 360 --script examples\app_cases\clock.js --pump-timers 3200
.\build\Debug\wearweb_win32_browser.exe path\to\page.html path\to\style.css
.\build\Debug\wearweb_win32_browser.exe examples\script_cases\event_probe.html examples\script_cases\event_probe.css --script examples\script_cases\event_probe.js
.\build\Debug\wearweb_win32_browser.exe examples\app_cases\calculator.html examples\app_cases\calculator.css --script examples\app_cases\calculator.js
.\build\Debug\wearweb_win32_browser.exe --capture output.bmp path\to\page.html path\to\style.css 390 640
.\build\Debug\wearweb_capability_check.exe path\to\page.html path\to\style.css path\to\app.js
.\build\Debug\wearweb_capability_check.exe --emit-used-chars used_chars.txt path\to\page.html path\to\style.css path\to\app.js
.\build\Debug\wearweb_font_pack_gen.exe --bdf font.bdf --chars used_chars.txt --output font_pack.h --name app_font
.\build\Debug\wearweb_embedded_host_demo.exe
.\build\Release\wearweb_microbench.exe 80 1000
.\build\Debug\wearweb_core_tests.exe
```

- `wearweb_demo` runs the current layout/layer/display-list slice.
- `wearweb_dom_dump` prints tokenizer output and an ASCII DOM tree. It caps file
  input at 512 KiB so the tool remains usable on low-end targets.
- `wearweb_cssom_dump` prints parsed CSSOM style rules, specificity and
  declarations. It also caps file input at 512 KiB.
- `wearweb_style_dump` prints computed styles for functional UI nodes such as
  forms, inputs, buttons, dialogs and cards.
- `wearweb_render_tree_dump` prints the render tree generated from DOM and
  computed style.
- `wearweb_layer_tree_dump` prints layer boundaries, reasons, clips and flattened
  display-list counts.
- `wearweb_pipeline_dump` prints end-to-end DOM/render/layout/layer/display-list
  counts and a display-list preview.
- `wearweb_capability_check` scans HTML/CSS/JS files and reports supported
  subsets, degraded features and unsupported APIs before deploying to a small
  target. It can also emit non-ASCII characters used by a page and verify them
  against a UTF-8 font coverage file with `--emit-used-chars` and
  `--font-coverage`.
- `wearweb_font_pack_gen` subsets a BDF bitmap font into a C++ `BitmapFont`
  header that can be compiled into an embedded text backend.
- `wearweb_embedded_host_demo` is a platform-neutral bring-up demo. It uses
  static HTML/CSS, the bitmap text backend, focus activation and an RGB565
  framebuffer sink without Win32, files or hardware I/O, so board ports can
  mirror its structure.
- `wearweb_pseudo_browser` runs the current full pipeline and writes a BMP or PPM
  framebuffer image. It is a desktop validation shell, not an embedded UI. Its
  built-in fallback font is intentionally tiny; use the Win32 browser shell for
  readable UTF-8/Chinese text validation. In scripting builds, `--script`
  evaluates one external JavaScript file after binding the parsed DOM and before
  rendering, then prints the stringified result or exception.
  `--pump-timers ms` advances host-pumped timers before rendering so timer-driven
  app examples can be smoke-tested without a window.
- `wearweb_win32_browser` is available on Windows builds. It opens an
  interactive Win32/GDI window, renders through the same core pipeline, injects a
  GDI text measurement and painter callbacks and forwards mouse/wheel input into
  the platform-neutral input controller. It is for desktop
  observation only; the core remains windowing- and OS-independent. In scripting
  builds, `--script` keeps a JerryScript runtime alive so JavaScript event
  listeners can react to native input and dirty DOM mutations can rerender.
- `wearweb_win32_browser --capture` renders through the same Win32/GDI text path
  and writes a BMP or PPM file without opening an interactive window.
- The inspection tools and Win32 shell merge author CSS from the explicit CSS
  file, embedded `<style>` elements and local `<link rel="stylesheet">` files.
  Linked CSS loading lives in example code; the core exposes only a callback and
  stays platform-neutral.
- `wearweb_microbench` runs parser/render/layout/layer/flatten microbenchmarks.
  Use a Release build for meaningful numbers.
- `wearweb_core_tests` is the single platform-neutral regression executable. It
  covers tokenizer/parser, DOM mutation, CSS parser, events, hit testing, input,
  render tree, layer tree and CPU framebuffer behavior. In scripting builds it
  also includes the JerryScript runtime tests.
- `examples/app_cases` contains small app-style acceptance pages for weather,
  clock, timer and calculator scenarios. See `docs/embedded_app_subset.md` for
  the supported authoring subset and the M7 readiness decision.
- `docs/developer_capability_matrix.md` is the developer-facing can-do/cannot-do
  contract. Check it before relying on an HTML/CSS/DOM/script feature.
- `docs/host_abstraction.md` sketches the thin host boundary for resources,
  time, framebuffer presentation, text and embedded budgets.
- `docs/embedded_hal_api.md` lists the hardware-side APIs a board port should
  implement, including ESP32-S3 mapping notes.
- `docs/text_backend.md` describes the host text measurement/painting contract
  and current fallback limits.
- `docs/memory_management.md` summarizes current embedded memory behavior,
  remaining risks and the next allocator/container optimizations.

## Documentation

English documentation uses the base filename, for example `README.md` and
`docs/roadmap.md`. Chinese documentation uses a `_zh` suffix, for example
`README_zh.md` and `docs/roadmap_zh.md`.

User-facing and architecture documentation should be maintained in both
languages.

## Versioning and changelog

- Current version: `0.2.0-dev` (`VERSION`)
- English changelog: `CHANGELOG.md`
- Chinese changelog: `CHANGELOG_zh.md`
- Versioning policy: `docs/versioning.md` and `docs/versioning_zh.md`
