# Core-owned text consumer retirement

> Last updated: 2026-11-16; Applies to: 0.6.0-dev. Core contract: 0.9.0-dev / ABI4.

Current source audit: 2026-11-16. Runtime PR #114 merged as `a197ceb4`; Core PR
#66 merged as `079de28a`, 0.9.0-dev / ABI4. Both post-merge CI runs passed.
Runtime production remains locked to signed Core 0.8.0 / ABI3; ABI4 package
publication and production locking are still pending.

The merged ABI4 Runtime has no production host text-measure or paint consumers.
Runtime Release CTest passed 113/113 against the exact Core source override,
including Core style-reuse tests, route-tabs, motion-lab and incremental/full
pixel equivalence. Core full/cropped Release CTest passed 33/33 and 32/32 on the
candidate. Core #66 CI, including Standalone Build and Archive, and Runtime
#114 full/cropped source-installed consumers passed. The versioned signed
release archive and Runtime tests against that published archive remain.

## Production consumers

| Consumer | Actual selection and resources | Remaining change |
| --- | --- | --- |
| ESP32 retained UI (`jellyframe_esp32s3_ui_task.cpp`) | Runtime #114 moves production text to Core TextSystem; ABI3 compatibility remains behind ABI guards. The accepted `mainline1` image predates #114/#66. | Qualify a new named device image after pinning the signed ABI4 package. |
| ESP32 installed worker (`jellyframe_esp32s3_installed_script_task.cpp`) | CoreText ON selects canonical v10 and a painterless renderer independently of BMP; owned catalog/snapshot survives accepted-frame replay. The user accepted popup title/options/selection/touch on `0.6.2-abi4-installed-core63-device1`; that image predates Core #66. | Rebuild and qualify a worker image against the pinned signed ABI4 package. |
| ESP32 P4/P5/P6 smoke (`main.cpp`) | The separate generated-BitmapFont provider factory is called only by this benchmark/smoke fixture, not by a product runtime route. The CoreText smoke runs before benchmark framebuffer admission. PR #110 built it with IDF 5.3.1 and exact Core #63 identity `e842b2e6...af1adb67`; the map excludes the legacy Noto C++ pack. | Keep legacy coverage on its explicit legacy profile. The CoreText smoke is build-qualified; physical execution is optional diagnostics, not a product-consumer migration gate. |
| Desktop default (`tools/native/win32_browser.cpp`) | The ABI4 candidate selects Core TextSystem by default and compiles GDI only on ABI3. Text content changes still rebuild without host metrics. Paint-only style changes can reuse layout after Core compares the old/new computed layout fields and render-tree shape; shared-inline caches and DOM tree/text dirtiness are conservatively rejected. Runtime CTest confirms route-tabs `rebuild=1`, motion-lab `style_stable>0`, and incremental/full pixel equivalence. | No remaining Win32 callback migration; retain ABI3 GDI only behind its compatibility build boundary. |
| Desktop application fonts (`src/app_runtime/app_font_set.cpp`) | Complete in Runtime #103: JFFONT2-only, callback loaders/accessors and browser precedence removed; tools/samples/import UI use canonical resources. | No remaining AppFontSet migration; generated C++ port defaults remain separate. |
| Embedded host example | This change binds one TextSystem to layout, layers and compositor using a complete tiny JFFONT2 fixture. | No remaining measurement/painter callback in this example. Device-default changes are separate. |

Runtime #114 qualifies generated pseudo-content paths through Core TextSystem
preparation or explicit refusal; Core #66 removes the legacy callback branches.
Runtime no longer needs to qualify the old callback fallback.
Historical test, benchmark and porting-guide references should be scoped to
ABI3 compatibility or history; do not restore an ABI4 alias.
Canvas2D text already uses TextSystem; it is not a remaining callback branch.
Core #62 migrates closed form-control labels/select arrows through
transactional PreparedText; its full tests pass 33/33, cropped focused and
installed consumers pass, and it merged after CI 4/4 (run `43673424532`). Core
follow-up `89acc36a` migrates visible open-select popup rows to Core-prepared
packets, refuses the complete overlay if any visible row cannot be prepared,
and never falls back to host callbacks. Runtime PRs #107-#111 are merged;
the dedicated Core #63 full/cropped worker lanes pass in CI. Full Core CTest
passes 33/33. Cropped CTest is 31/32 because the existing shared-inline shadow
assertion fails identically on the pre-change base. Runtime #109 adds the
WS147 full-feature profile and select-popup app. The exact
`0.6.2-abi4-installed-core63-device1` image passed user-visible popup
acceptance on 2026-11-16: all four options were visible and selecting CHARLIE
updated the result. Its identity is Runtime `f609a1d45fef39db5f22cbc9ad5c5d98c3468dc7`,
Core source `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`,
and BIN SHA-256 `8dba1f73835e35697ef13c7d63159c966b8ab487dab752d8f3b20ee7a2ad6a0f`.
After acceptance, the board was restored to the already accepted native
`mainline1` image; this does not add new evidence for that image. The painterless
installed profile rejects unsupported ordinary Text commands before writes.
Runtime #114 covers generated pseudo-content through Core-owned preparation or
refusal; Core #66 removes the callback fallback contract.

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

- Local root: `D:\CodexTemp\abi4-current-mainline-20261115` (temporary storage).
- Runtime `d6fcaff4b56a178f3bae6bbc4bf6721f931c2736` (PR103); Core
  `38148e7388edc791e303b9b324f45d5df3d7b63e` (PR62).
- Core source identity `caea681129a7201454fed709f1055a31886ef729779646853cc450e0c71a35e`, 163 inputs.
- IDF v5.3.1 `c8fc5f643b7a7b0d3b182d3df610844e3dc9bd74`, with recursively
  pinned SDK submodules; compiler `esp-13.2.0_20240530`.
- Profile `ws147_core_text;render-core-default`, 240 MHz, `/band_shell.html`.
  BMP adapter and installed script runtime are disabled in this native profile;
  this candidate cannot qualify installed worker or natural-BMP delivery.
- Candidate label `0.6.2-abi4-mainline2`; app BIN 7,614,704 bytes,
  SHA-256 `95cc99f6161d44b5f8802399adbacb36289f5304826dd1e408878739cb346f4d`.
  The 8 MiB app partition has 773,904 bytes free.
- ELF SHA-256 `f95d87a9c540e69d57feaac572dfadb9b1979b7fc989d9273f11d3e62aca4481`;
  build log version `tools-v0.4.72-216-gd6fcaff4`. Use the boot version plus ELF
  hash for board identity because this native profile has no JFDP image-version
  endpoint.

The app segment at factory offset `0x10000` matches the BIN hash. The factory is
16 MiB, SHA-256 `41bbd01594fd5072ad9e88db4e465674744e240dfdadff84893ac70a5a4d1920`;
the offline Developer Image BIN matches the build BIN exactly. Package and
device-manifest regression suites pass 5/5 and 4/4.

Provider ZIP: `jellyframe-ws147-developer-0.6.2-abi4-mainline2-provider-0.1.1-dev.zip`,
6,777,675 bytes, SHA-256 `2984eef0e036622f9dfad9bb1e1350056bd9b1dc670ee06a0b5deeb40a5540a5c`.

This exact `mainline2` image remains unflashed and unaccepted. A separate
`0.6.2-abi4-mainline1` image was tested on WS147 on 2026-11-15:

- Runtime `75a435c5`, Core `34ae4b88`.
- BIN SHA-256 `AFB9F47E015FF5EB06AD441EBC322F50B04D6149CFA3E70B8C96675EF46C696E`.
- ELF SHA-256 `B462F78731662B53A21DEAEE1F86E4E0995F37D2F964BEDA3D27B284FA522910`.
- The boot log confirmed `/band_shell.html`, CoreText at 240 MHz, touch detection
  and successful first present. The user confirmed complete Home text, visible
  Activity and Weather text, visible text in all four Weather small cards, and
  successful return navigation.

This observation closes only the tested native image. It does not qualify the
separate `mainline2` candidate, installed worker/BMP delivery, or the P4/P5/P6
benchmark smoke. Keep those as separate named acceptance gates.

Core ABI4 is still unreleased. Runtime production remains signed Core 0.8.0/ABI3.
ABI5 tracked-style reuse and shaping are separate unqualified candidates.
