# Core-owned text consumer retirement

> Last updated: 2026-11-15; Applies to: 0.6.0-dev. Core contract: 0.9.0-dev / ABI4.

Current source audit: 2026-11-15. Runtime base `2f87d52e` (PR102), Core
`38148e73` (PR62), 0.9.0-dev / ABI4. This is a consumer migration checklist,
not a declaration that legacy callback APIs have been removed.

## Production consumers

| Consumer | Actual selection and resources | Remaining change |
| --- | --- | --- |
| ESP32 retained UI (`jellyframe_esp32s3_ui_task.cpp`) | Default providers use generated C++ Noto `BitmapFont` faces. The named CoreText profile binds the shared JFFONT2 catalog and clears the providers. | Qualify the catalog path, migrate the default resource contract, then remove callback construction and port-owned measurement/scaling. |
| ESP32 installed worker (`jellyframe_esp32s3_installed_script_task.cpp`) | CoreText ON selects supported canonical v10 and a painterless renderer, independently of BMP. Owned text catalog/snapshot survives accepted-frame replay. CoreText OFF retains the explicit legacy painter path. | Qualify the bounded path on device; migrate remaining callback-dependent generated/widget text before retiring the legacy path. |
| ESP32 non-retained entry (`main.cpp`) | Builds generated-BitmapFont providers and painter independently of retained UI. | Migrate or explicitly retire this entry configuration too. |
| Desktop default (`tools/native/win32_browser.cpp`) | GDI measurement/painting using the OS font; Core catalog is opt-in. | Close CoreText property parity, then adopt the catalog default. |
| Desktop application fonts (`src/app_runtime/app_font_set.cpp`) | Current change makes AppFontSet JFFONT2-only, removes callback loaders/accessors and browser precedence; tools/samples/import UI use canonical resources. | Complete PR/CI and positive named/default output gates; generated port defaults remain separate. |
| Embedded host example | This change binds one TextSystem to layout, layers and compositor using a complete tiny JFFONT2 fixture. | No remaining measurement/painter callback in this example. Device-default changes are separate. |

Core still contains callback/default layout, wrapping, ellipsis, repaint/reuse,
layer-cache identity and compositor branches. Remove them together only after
the production consumers above migrate. Tests, benchmarks and ESP32 acceptance
fixtures also need conversion, but are not product-default consumers.
Canvas2D text already uses TextSystem; it is not a remaining callback branch.
Ordinary generated pseudo-content and open native select popup text still have
legacy `Text` emission paths in Core. Core #62 migrates closed form-control
labels/select arrows through transactional PreparedText; its full tests pass
33/33, cropped focused and installed consumers pass, and it merged after CI 4/4 (run `43673424532`).
The painterless installed profile rejects ordinary Text commands before writes;
this is not a claim that all generated/widget contexts have migrated.

The embedded-host example passed MSVC Release execution with the locked
installed Core0.8/ABI3 package and source-overridden Core PR60 in full/cropped
profiles (all four optional families off for cropped). Each run proves 54
heading-ink pixels, one button click, checkbox state, profile-specific select
behavior and one RGB565 full-frame flush. Cropped ABI4 evidence is source-based,
not an installed-package check. The example is registered as
`jellyframe_embedded_host_core_text` when examples and tests are enabled.

## Installed worker host and cross-build evidence

The new BMP0/1 tests use actual prepared glyphs and prove visible ink,
JavaScript text mutation, replay after worker/resource/session teardown,
snapshot retention until the final envelope releases, and atomic refusal of
ordinary Text, malformed packets, foreign resource IDs and rendering budgets.
Core PR60 full and cropped each pass 7/7 focused resource/admission/text tests.
The locked installed Core0.8/ABI3 package passes four admission tests; the two
canonical tests intentionally skip. CI builds and runs both text targets in
full/cropped canonical ABI4 profiles and the R3 ownership lane.

Real ESP-IDF5.3.1 compilation selects lifecycle + script worker + CoreText,
explicit R3 events (the Xtensa build has no RTTI), 240 MHz, and immutable Core
PR60. With all four optional Core families disabled, BMP OFF/ON both produce
size-checked images: 8,341,360 / 8,351,648 bytes, leaving 47,248 / 36,960 bytes
in the 8 MiB app partition. These margins are small and do not qualify a
production profile. The full-feature BMP-OFF build compiles and links but its
8,504,560-byte BIN exceeds that partition by 115,952 bytes; do not report it as
a successful flashable image or silently expand the partition.

Local evidence root: `D:/CodexTemp/installed-coretext-20261115`; host CTest
logs are under `build-{full,cropped,abi3-installed}/Testing/Temporary`, IDF
logs under `idf-bmp{0,1}-cropped-{configure,build}.log`, and the full overflow
log is `idf-bmp0-build.log`. Cropped BIN SHA-256 values:

- BMP OFF: `cadd3d3e28d7c71baacd012e3bc6fefd157f8579a5f5e92f49164912f5bb4eab`.
- BMP ON: `af743507278d5056ca963d24d448eec33b164d4da1220647a8ea310d2378cf7a`.

These are build-only artifacts from this change, not flashed/accepted images.
The native launcher candidate below is separate and remains byte-for-byte fixed.

## Bounded CoreText ellipsis

Core PR #61 is merged as `3152300a` after CI 4/4. It finalizes the retained
paint packet on one actual direct-run owner after final geometry; it preserves
source layout/intrinsics, uses Q1000 advances/spacing and enforces complete
source/output budgets. U+2026 requires actual coverage; only missing marker
coverage permits three real dots. A clipped lone/first scalar needs no invisible
marker resource. There is no host fallback or new packet/API version.

Mixed owners, generated/native-marker contexts, overflowing multiline text and
active negative indent remain outside this bounded profile. Refused inline
contexts suppress legacy generated fragments while independent block contexts
continue normally. This is not general paragraph ellipsis or complete desktop
property parity. See Core's current TextSystem reference for exact admission.

Full MSVC CTest passes 33/33; the cropped suite is 31/32 with its only failure
reproduced on untouched base. Installed full/cropped text consumers each pass
1/1; real Noto visible output passes. Runtime #102's merged exact-Core pin adds five
deterministic real-font frames across width/source changes: visible ink, restored
pixels, incremental/full equality, no refusal, and narrow frames different from
clip-only references. Source identity:
`c11045754fa28b9bf3e5e0e9a8720dc202efd7dd9c66c159c058f5edee7dd69b`, 163 inputs.

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
