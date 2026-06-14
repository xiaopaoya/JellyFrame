# WearWeb Engine

WearWeb Engine is a deeply trimmed C++ HTML/CSS runtime intended for low-power
wearable devices. The first milestone is not a full browser. It is a small,
portable document UI engine that can later host JavaScript through JerryScript.

## Current slice

- Minimal HTML tokenizer/parser
- DOM tree
- Tiny CSS parser
- Basic cascade for tag, class, id and inline style
- Vertical block layout
- Sparse layer tree with flattening to a platform-neutral display list
- CPU software rasterizer/compositor with BMP/PPM output for validation
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

For an embedded or library-only build:

```powershell
cmake -S . -B build-core -DWEARWEB_BUILD_EXAMPLES=OFF -DWEARWEB_BUILD_TESTS=OFF -DWEARWEB_BUILD_BENCHMARKS=OFF
cmake --build build-core --config Release
```

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
.\build\Release\wearweb_microbench.exe 80 1000
.\build\Debug\wearweb_tokenizer_tests.exe
.\build\Debug\wearweb_css_parser_tests.exe
.\build\Debug\wearweb_render_tree_tests.exe
.\build\Debug\wearweb_layer_tree_tests.exe
.\build\Debug\wearweb_software_renderer_tests.exe
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
- `wearweb_pseudo_browser` runs the current full pipeline and writes a BMP or PPM
  framebuffer image. It is a desktop validation shell, not an embedded UI.
- `wearweb_microbench` runs parser/render/layout/layer/flatten microbenchmarks.
  Use a Release build for meaningful numbers.
- `wearweb_tokenizer_tests` runs the current tokenizer and parser regression
  checks.
- `wearweb_css_parser_tests` runs CSS parser and fallback-style regression
  checks.
- `wearweb_render_tree_tests`, `wearweb_layer_tree_tests` and
  `wearweb_software_renderer_tests` cover render, layer and CPU framebuffer
  behavior.

## Documentation

English documentation uses the base filename, for example `README.md` and
`docs/roadmap.md`. Chinese documentation uses a `_zh` suffix, for example
`README_zh.md` and `docs/roadmap_zh.md`.

User-facing and architecture documentation should be maintained in both
languages.

## Versioning and changelog

- Current version: `VERSION`
- English changelog: `CHANGELOG.md`
- Chinese changelog: `CHANGELOG_zh.md`
- Versioning policy: `docs/versioning.md` and `docs/versioning_zh.md`
