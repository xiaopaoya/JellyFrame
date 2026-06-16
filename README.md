# JellyFrame Engine

JellyFrame is a deeply trimmed C++ HTML/CSS/JS UI runtime for low-power wearable
and embedded devices. It is not a general web browser. It is a small
browser-shaped app engine: HTML describes structure, CSS describes presentation,
and an optional JerryScript bridge adds bounded interaction for local apps.

The project was developed under the early codename `WearWeb`; current code,
targets and documentation use `JellyFrame`.

## Why It Exists

Many wearable UI stacks force application authors to draw every screen through a
canvas-like API. JellyFrame explores a different route: keep the browser
programming model where it is cheap and useful, cut the parts that do not fit a
small MCU, and expose clear host interfaces for display, input, text and
resources.

Typical targets:

- watches and small dashboard devices;
- local HTML/CSS/JS app shells;
- embedded products that need maintainable UI without full browser cost;
- desktop validation tools for board ports.

## Current Capabilities

- Tolerant HTML tokenizer, tree builder and compact DOM.
- CSS parser, CSSOM and style resolver for the documented embedded subset.
- Modern authoring helpers such as CSS variables, bounded `@media`,
  conservative `@supports`, `:is()` / `:where()`, sibling selectors and dynamic
  state pseudo-classes.
- Block, inline, simplified flex row/wrap/sizing, bounded positioned layout and
  responsive grid-card layout.
- Form controls for buttons, text inputs, textareas, checkboxes, radios, ranges,
  selects, progress and meter.
- Hit testing, capture/target/bubble event dispatch and platform-neutral input.
- Optional JerryScript runtime with a small DOM/event/form/timer binding.
- Layer tree, display list, CPU rasterizer/compositor and embedded framebuffer
  adapters for RGBA/BGRA, RGB565/BGR565, RGB332, Gray8 and monochrome targets.
- Desktop tools for DOM/CSSOM/style/render/layer/pipeline inspection,
  capability checking, font pack generation, screenshots and Win32 interaction.

The precise can-do/cannot-do contract lives in
[docs/developer_capability_matrix.md](docs/developer_capability_matrix.md).

## Quick Start

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Render a static page to an image:

```powershell
.\build\Release\jellyframe_pseudo_browser.exe `
  examples\modern_cases\article_cards.html `
  examples\modern_cases\article_cards.css `
  article_cards.bmp 390 640
```

Open an interactive Windows validation shell:

```powershell
.\build\Release\jellyframe_win32_browser.exe `
  examples\app_cases\calculator.html `
  examples\app_cases\calculator.css
```

For the longer onboarding path, including every generated executable and what it
does, read [HOW_TO_START.md](HOW_TO_START.md).

## Optional Scripting Build

Scripting is intentionally optional. `jellyframe_core` does not depend on
JerryScript unless `JELLYFRAME_BUILD_SCRIPTING=ON` is requested.

```powershell
git clone --depth 1 https://github.com/jerryscript-project/jerryscript.git third_party\jerryscript
python third_party\jerryscript\tools\build.py --clean

cmake -S . -B build-script `
  -DJELLYFRAME_BUILD_SCRIPTING=ON `
  -DJERRYSCRIPT_ROOT="%CD%\third_party\jerryscript" `
  -DJERRYSCRIPT_LIBRARIES="%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-core.lib;%CD%\third_party\jerryscript\build\lib\MinSizeRel\jerry-port.lib"
cmake --build build-script --config Release
```

Scripting shells support classic inline scripts, host-provided local external
classic scripts, small DOM mutation APIs, event listeners, form properties and
host-pumped timers. ES modules, network loading and browser storage are outside
the embedded core.

## Repository Map

- `src/core`: platform-neutral engine core.
- `src/script`: optional JerryScript binding layer.
- `examples`: inspection tools, pseudo browser, Win32 browser and acceptance
  pages.
- `tests`: platform-neutral regression tests.
- `benchmarks`: desktop microbenchmarks.
- `ports`: port-support code and desktop virtual-board benchmark.
- `docs`: technical contracts and module/API documentation.
- `project_docs`: current status, roadmap and development process notes.

## Documentation

Start here:

- [HOW_TO_START.md](HOW_TO_START.md): full first-time developer guide.
- [docs/README.md](docs/README.md): technical documentation index.
- [project_docs/README.md](project_docs/README.md): current status, roadmap and
  development process index.
- [docs/developer_capability_matrix.md](docs/developer_capability_matrix.md):
  supported, degraded, lazy and deferred features.
- [docs/engine_architecture.md](docs/engine_architecture.md): pipeline overview.
- [docs/embedded_hal_api.md](docs/embedded_hal_api.md): host/HAL contract for
  board ports.
- [project_docs/project_status.md](project_docs/project_status.md): current
  mainline status and next milestones.

Chinese documentation uses the `_zh` suffix, for example
[README_zh.md](README_zh.md), [HOW_TO_START_zh.md](HOW_TO_START_zh.md),
[docs/README_zh.md](docs/README_zh.md) and
[project_docs/README_zh.md](project_docs/README_zh.md).

## Versioning

- Current version: `0.2.0-dev` in [VERSION](VERSION).
- Changelog: [CHANGELOG.md](CHANGELOG.md) and
  [CHANGELOG_zh.md](CHANGELOG_zh.md).
- Version rules: [docs/versioning.md](docs/versioning.md).

## Status

JellyFrame is suitable for small local embedded UI experiments and desktop
validation today. It is not suitable for arbitrary modern websites, full
frontend frameworks, networked browser apps or pixel-compatible rendering.
