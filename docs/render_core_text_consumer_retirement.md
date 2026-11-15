# Core-owned text consumer retirement

> Last updated: 2026-11-15; Applies to: 0.6.0-dev. Core contract: 0.9.0-dev / ABI4.

Current source audit: 2026-11-15. Runtime base `75a435c5` (PR99), Core
`34ae4b88` (PR60), 0.9.0-dev / ABI4. This is a consumer migration checklist,
not a declaration that legacy callback APIs have been removed.

## Production consumers

| Consumer | Actual selection and resources | Remaining change |
| --- | --- | --- |
| ESP32 retained UI (`jellyframe_esp32s3_ui_task.cpp`) | Default providers use generated C++ Noto `BitmapFont` faces. The named CoreText profile binds the shared JFFONT2 catalog and clears the providers. | Qualify the catalog path, migrate the default resource contract, then remove callback construction and port-owned measurement/scaling. |
| ESP32 installed worker (`jellyframe_esp32s3_installed_script_task.cpp`) | Worker can receive TextSystem and prepared-frame resources. Its renderer still unconditionally receives `make_production_text_painter()`. | Require owned prepared text/resources in the admitted transport path, then remove the renderer painter dependency; a CoreText flag alone does not remove this consumer. |
| ESP32 non-retained entry (`main.cpp`) | Builds generated-BitmapFont providers and painter independently of retained UI. | Migrate or explicitly retire this entry configuration too. |
| Desktop default (`tools/native/win32_browser.cpp`) | GDI measurement/painting using the OS font; Core catalog is opt-in. | Close CoreText property parity, then adopt the catalog default. |
| Desktop application fonts (`src/app_runtime/app_font_set.cpp`) | Legacy serialized JFFONT loads BitmapFont and selects AppFontSet callbacks before CoreText. JFFONT2 uses TextSystem. | Define and migrate the pre-release app-font resource contract and fixtures, then delete legacy callback accessors/selection. |
| Embedded host example | This change binds one TextSystem to layout, layers and compositor using a complete tiny JFFONT2 fixture. | No remaining measurement/painter callback in this example. Device-default changes are separate. |

Core still contains callback/default layout, wrapping, ellipsis, repaint/reuse,
layer-cache identity and compositor branches. Remove them together only after
the production consumers above migrate. Tests, benchmarks and ESP32 acceptance
fixtures also need conversion, but are not product-default consumers.
Canvas2D text already uses TextSystem; it is not a remaining callback branch.

The embedded-host example passed MSVC Release execution with the locked
installed Core0.8/ABI3 package and source-overridden Core PR60 in full/cropped
profiles (all four optional families off for cropped). Each run proves 54
heading-ink pixels, one button click, checkbox state, profile-specific select
behavior and one RGB565 full-frame flush. Cropped ABI4 evidence is source-based,
not an installed-package check. The example is registered as
`jellyframe_embedded_host_core_text` when examples and tests are enabled.

## CoreText ellipsis prerequisite

Core `src/layout.cpp` refuses `text_overflow_ellipsis` before preparation;
shared-inline admission also excludes it. The font service admits U+2026, so
this is an integration gap rather than an inability to represent that scalar.
The legacy callback path paints ASCII `...` and is not a CoreText fallback.

The next bounded implementation should handle the established single direct
LTR nowrap run in Core, prepare source and marker once, select a scalar-safe
prefix with Q1000 advances/spacing, and emit the existing prepared packet.
Keep full-source/marker resource bounds and atomic refusal. Negative spacing
does not justify assuming monotonic prefix widths. Mixed-owner paragraphs need
whole-paragraph cutoff ownership; removing their refusal or truncating each
leaf independently would be incorrect. No new host measurement hook is needed.

## Native WS147 candidate for hardware qualification

- Local root: `D:\CodexTemp\abi4-mainline-20261115` (temporary storage).
- Runtime `75a435c5eac607126987bb4e3c5f1ab99c4975a2`; Core
  `34ae4b8877e8ba11a2f424791efebc4b1dbba3ba`.
- Core source identity `bbfa81de99ca687d036ef151f87b3e7bac1a29cd8f2c9e1c8c45547b7c1f4552`, 163 inputs.
- IDF v5.3.1 `c8fc5f643b7a7b0d3b182d3df610844e3dc9bd74`, with
  recursively pinned SDK submodules; compiler `esp-13.2.0_20240530`.
- Profile `ws147_core_text;render-core-default`, 240 MHz, `/band_shell.html`.
  BMP adapter and installed script runtime are disabled in this native profile;
  this candidate cannot qualify installed worker or natural-BMP delivery.
- Candidate label `0.6.2-abi4-mainline1`; app BIN 7,608,592 bytes,
  SHA-256 `ec5087ab6da4a1d67f725500ef8f4dd45ae74afa0c0df67b69cb04b0119923e6`.
- Boot app version `tools-v0.4.72-203-g75a435c5`; ELF SHA-256
  `9125ef1d9aaabec70f2f430c07c1296f0b9a325493b3d0e70245aa4fa427bef7`.
  Use these boot fields for board identity: the native profile does not retain
  the JFDP image-version endpoint.

The BIN, offline package/ZIP and factory app segment compare byte-for-byte;
the factory image is 16 MiB. `artifact-verification.json` holds the checks.
Hardware acceptance is pending; no board was connected or flashed in this
build session. Validate Home, Activity, Weather's eight forecast labels and
return navigation on this exact image before closing the missing-text report.
The older pre-PR60 build is historical evidence, not the current candidate.

Core ABI4 is still unreleased. Runtime production remains signed Core0.8/ABI3.
ABI5 tracked-style reuse and shaping are separate unqualified candidates.
