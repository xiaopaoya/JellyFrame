# Render Core ABI4 Integration Plan

> Last updated: 2026-11-16; Applies to: 0.6.0-dev. Core release candidate: 0.9.0 / ABI4.

> **Latest checkpoint (2026-11-16):** Runtime PRs #99–#113 are merged; Runtime #111 promotes the WS147 band-shell CoreText profile and the Win32 ABI4 text-property/incremental-versus-full repaint matrix (293 frames, 27 flows). The accepted `mainline1` hardware observation is already recorded and must not be repeated. Core PR #67 merged as `16a83211ec0992f8e25ec0fc977eb6fd158716e3`, completing the reviewed ABI4 contract; Core CI and clean Release CTest passed. The 0.9.0 ABI4 source archive has been generated and its SHA-256 verified as `8a0568b548a7e30956c515bba753ad7fe7f0fbb501e319cbef926eec2b855628`; signed tag and GitHub Release remain pending GPG authentication. The Runtime lock update is prepared on a branch from master `2e116182`; full and cropped local builds each pass CTest 22/22, including installed Core ABI4 consumer coverage, and package/profile regressions pass. The user has separately accepted `Core 63 Select Popup` on named profile `0.6.2-abi4-installed-core63-device1`: labels, all four options, selected result and touch worked; JFDP accepted the first frame with zero render/present failures. These device observations do not replace signed-package integration. Runtime master remains on signed Core 0.8.0 / ABI3 until the candidate lock PR passes CI and merges. `mainline2` remains a separate pending profile.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Runtime master still locks signed Core v0.8.0 / ABI3; signed ABI4 v0.9.0 candidate archive is hash-verified locally. | Complete GPG signing and GitHub Release, then open the Runtime lock PR and merge only after CI passes. |
| Core mainline | Core PR #67 (`16a83211ec0992f8e25ec0fc977eb6fd158716e3`) is merged; ABI4 contract and clean Release gates passed. | Finish signed v0.9.0 publication. |
| Runtime mainline | PRs #99–#113 are merged. A new candidate branch updates the exact package lock, app manifests/templates, provenance CI and ABI4 installed consumers; full/cropped local CTest each pass 22/22. | Publish Core package, open/validate the Runtime PR, then merge after CI. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PR #101 makes canonical v10/painterless CoreText independent of BMP. PR #107 tests Core #63 popup input, prepared packets, visible row pixels, selection and close through the canonical v10 worker on the exact source identity. PR #109 adds a full-feature WS147 profile and select-popup app, with IDF build, packaging and provider regressions passing. Core #65 generated pseudo-content is included in the Runtime source-override build/CTest. The user accepted title, four options, selected result and touch on `0.6.2-abi4-installed-core63-device1`; first frame accepted, render/present failures zero. | Popup named-profile visual acceptance passed; retire the remaining provider/painter paths. |
| Native WS147 | `0.6.2-abi4-mainline2` built from Runtime #103/Core #62; app BIN 7,614,704 B, 773,904 B free in the 8 MiB app partition, 16 MiB factory/package hashes verified. A distinct `mainline1` image passed Home/Activity/Weather/back observation on 2026-11-15. | Flash/observe the exact `mainline2` artifact; `mainline1` evidence does not qualify it or installed worker/BMP, which are disabled in these profiles. |
| ABI5 and shaping | Separate optional candidates; ABI5 tracked reuse has correctness/package evidence but no qualified performance gain. | Not enabled in Runtime and not an ABI4 release prerequisite. |

The experimental source declares 0.6.2 / ABI9. Neither that number nor a count
of merged PRs proves complete migration: capabilities must have governed Core
contracts, Runtime consumers, resource delivery and applicable tests.

## Parallel workstreams

### A. Resource delivery and catalog defaults

Completed: signed-package offline bootstrap (PR #73), shared Noto Sans SC
JFFONT2 catalog and ownership API (#75), explicit desktop/device catalog paths
(#76/#77), backend/source identity reporting and route/repaint gates (#79-99).
Core PR #60 adds bounded letter spacing and JFTP v3 positions, retaining v1/v2
decoding. Core selection, metrics, preparation and rasterization are canonical;
ports register immutable resources rather than implementing text policy.

Completed in Runtime #103: serialized JFFONT0/1 and AppFontSet callback
precedence are removed; manifests/imports use canonical JFFONT2 with offline
Core preparation. A local port change has now separated the ESP32 build
boundary: CoreText builds do not generate or link the legacy Noto C++ bitmap
pack, while CoreText-off builds and explicitly selected legacy painter fixtures
retain it. ESP-IDF 5.3.1 builds succeeded for both profiles. The CoreText band
shell and P4/P5/P6 smoke maps contain no legacy font symbols; the smoke is
compiled to log CoreText measurement, prepared-line and full/dirty present
checks when run. The legacy profile generated and linked 27 faces. These were
different sdkconfig profiles, so BIN sizes are not
a direct measurement of the pack's net cost. Remaining: make CoreText the
qualified default, migrate/retire remaining generated-font consumers, and
preserve deterministic family/weight selection, font-byte lifetime,
missing-font refusal and resource budgets. Do not add more host text callbacks.

### B. Runtime consumer migration

| Consumer | Progress | Exit evidence |
| --- | --- | --- |
| Embedded host | Complete in #100. | Visible ink, click/checkbox/select and RGB565 flush; ABI3 and ABI4 full/cropped. |
| ESP32 installed worker | Merged #101 painterless canonical v10 path; owned catalog/snapshot survives replay; BMP is optional. | Host full/cropped and locked ABI3 matrix, real IDF builds, CI, later exact device profile. |
| ESP32 retained native/default | Runtime #111 enables CoreText in the WS147 band-shell defaults; profile regression and exact Core #63 ESP-IDF build passed, while legacy typography acceptance remains explicit. | Keep legacy typography isolated; future device profile acceptance must name the exact image. The accepted `mainline1` image is not repeated. |
| ESP32 P4/P5/P6 smoke | The independent provider factory in `main.cpp` is used only by a benchmark/smoke fixture; CoreText profile now has separate CoreText smoke build evidence. | Keep the old fixture explicitly legacy-only and give CoreText equivalent ASCII/CJK/weight and widget coverage; this is not a product-default consumer. |
| Win32 default | Explicit CoreText catalog mode is merged; GDI remains default. PR #110 makes CoreText text mutation bypass reuse validation that depends on legacy provider metrics. PR #111 merges the ABI4 property/repaint matrix: 293 frames across 27 flows. | Decide catalog default adoption after API/fixture retirement; GDI remains an intentional selectable path. |
| AppFontSet | Complete in #103: JFFONT2-only catalog, callback accessors/browser precedence removed, samples and tools converted. | No remaining AppFontSet work; generated C++ BitmapFont defaults are tracked separately. |

Canvas2D already uses TextSystem and is not an outstanding callback consumer.
The installed-worker change refuses ordinary callback-dependent `Text` commands
before framebuffer writes; that is a bounded admission policy. Core #65 closes
the generated pseudo-content/list-marker fallback by preparing through the
registered TextSystem or refusing without publishing legacy Text commands.
Runtime must qualify the exact Core #65 source where those producers are
reachable, then inventory other callback-dependent Core consumers and
test/fixture-only uses before the coordinated API retirement.

Host evidence for merged #101: full and four-family
cropped Core PR60 each pass 7/7 resource/admission/text tests. Both BMP variants
prove visible prepared text, JavaScript mutation, replay after teardown and
atomic refusal. Locked installed ABI3 passes four admission tests; the two
canonical tests intentionally skip. The CoreText-OFF legacy path is explicit.

### C. Core-owned text API consolidation

Core PR #61 merged as `3152300a` after all four CI jobs passed (run `43670844631`). It implements bounded owner-side paint ellipsis; full MSVC tests pass 33/33 and installed full/cropped text consumers pass 1/1 each. The cropped suite's sole shared-inline shadow assertion reproduces unchanged on its base. Runtime #102 has merged its exact source pin and five real-font incremental/full ellipsis frames, including a visible clip-only control. Core #62 prepares closed native control labels/select arrows transactionally with Core metrics and packets; it merged as `38148e73`. Core #63 (`89acc36a`) now prepares every visible open-select popup row before publishing the overlay and refuses the full overlay on missing glyphs or budget refusal. Runtime #107 verifies the exact Core source through canonical worker input, visible pixels, selection and popup close. Generated-content ownership and broader property/default qualification remain separate. It must preserve
source layout/intrinsic dimensions, apply owner clipping after final geometry,
account for Q1000/spacing and output budgets, and refuse unsupported complete
contexts without host fallback. Mixed owners need whole-paragraph cutoff
ownership; truncating every leaf independently is not acceptable.

After every production consumer migrates, remove `TextMeasureProvider`,
`TextPainter`, range/family/additive hooks and callback branches in layout,
intrinsics, cache/repaint, layer emission and rasterization. Convert dependent
tests, benchmarks, package consumers and porting guides in the same coordinated
pre-release change. Only the unused HostTextAdapter wrapper has been removed.

Exit: no production callback consumer remains; full Core, source archive,
installed full/cropped consumers and disabled-feature gates pass. Hardware
qualification is a downstream adoption gate, not a reason to retain dead host APIs.

### D. Release and production lock

ABI4 is **not released**. Cut a signed deterministic Core release from the final
reviewed API/resource contract; verify tag, archive checksum, source manifest,
profile and standalone install/consumers. Update Runtime's exact version, ABI,
source/archive lock and installed-artifact CI, together with compatibility docs.
Keep ABI3 as rollback evidence; do not imply binary interchangeability.

### E. Device admission

The native candidate at `D:/CodexTemp/abi4-mainline-20261115` pins Runtime
`75a435c5`, Core `34ae4b88` and clean IDF5.3.1 with recursively fixed submodules.
It uses `/band_shell.html`, CoreText ON, BMP/installed scripting OFF. Its BIN,
package/ZIP and factory segment are identical. Use boot app version plus ELF
SHA for board identity; see the inventory for full hashes.

The separately tested `mainline1` identity is Runtime `75a435c5`, Core
`34ae4b88`, BIN SHA-256
`AFB9F47E015FF5EB06AD441EBC322F50B04D6149CFA3E70B8C96675EF46C696E`, and ELF
SHA-256 `B462F78731662B53A21DEAEE1F86E4E0995F37D2F964BEDA3D27B284FA522910`.
Boot logs identified `/band_shell.html`, CoreText at 240 MHz, touch detection and
successful first present. The user confirmed that Home text was complete and
legible, Activity and Weather rendered their text, all four Weather small cards
contained text, and navigation returned normally. This closes that observation
only for this exact native image. It does not qualify the separate `mainline2`
artifact, installed worker/BMP, or the P4/P5/P6 benchmark smoke. The previous
missing-small-card-text report remains relevant to those other identities.
Installed worker/BMP tests require their own named image and resource profile.
The installed-worker popup profile `0.6.2-abi4-installed-core63-device1` passed
user observation on 2026-11-15: title, OPTION label, selector, ALPHA/BRAVO/
CHARLIE/DELTA, `SELECTED: CHARLIE`, and touch all worked; JFDP accepted the
first frame with zero render/present failures. This result applies only to
that named profile and artifact.
The `mainline2` device acceptance remains pending; no device acceptance can be
inferred from a build or positive assumption.

## Dependency order

```text
catalog/resource contract -> native + worker + desktop consumers
                                     |
                            callback API retirement
                                     |
                          signed ABI4 + exact Runtime lock
                                     |
                    named device profile + hardware acceptance
```

Host implementation/review, installed-package qualification and device build
preparation can proceed in parallel. Default adoption follows property/resource
gates. Optional ABI5 reuse and production shaping remain separate.

## Immediate next actions

1. Completed: Runtime #111 merges the CoreText-default WS147 profile and the
   Win32 293-frame/27-flow property and incremental/full repaint matrix.
2. Completed in Core #65: generated pseudo labels/list markers use Core
   TextSystem preparation or explicit refusal; all four Core CI lanes pass.
3. Completed: Runtime Release build and CTest 60/60 with exact Core #65 source
   identity `52ae4354...7302f62d`; the Core test hook is now included in the
   Runtime in-tree source-override test configuration. This qualifies host
   integration, not the signed package lock or any device image.
4. Finish the production/test/fixture callback-consumer inventory and decide
   whether the Win32 GDI default changes. Convert remaining
   callback-dependent tests/fixtures, then remove provider/range/family/additive
   APIs and implementation branches in one coordinated pre-release change.
5. After final API review, full/cropped, feature-disabled, installed-consumer
   and archive gates pass, sign and publish ABI4 and update Runtime's exact
   production dependency lock.
6. Track `mainline2` WS147 and installed-worker popup as separate named images
   when useful; the accepted `mainline1` observation is complete and must not be
   repeated.

### Callback API retirement slices (2026-11-15 audit)

Review of Core `1d5c746d` and Runtime PR #112 (head `8e833b35`, merge
`59f61c40`) confirms this is not a declaration-only cleanup: provider identity
participates in layout-cache validity, LayerTree can still emit callback-based
`Text` commands, and SoftwareRasterizer can still paint them through
`TextPainter` or its built-in bitmap fallback. Do not remove the public
declarations before these ordered slices are complete:

| Slice | Main file boundary | Required result |
| --- | --- | --- |
| 1. Layout / intrinsic / repaint | Core `layout.h/.cpp`, `intrinsic_source_query*`, `text_layout_reuse*`, `text_repaint*`, `style_repaint*` | Text measurement, wrapping, grid intrinsic sizing and text-dirty reuse use only the bound TextSystem/Core cache identity. Missing catalog, glyph or budget must refuse explicitly. Remove provider/context identity from caches. |
| 2. LayerTree producers | Core `layer_tree.h/.cpp` and prepared-text/shared-inline producers | Stop reading `LayerTreeBuilderOptions::text_measure` and emitting ordinary callback-based `Text`; supported text becomes PreparedText, and other cases refuse transactionally without host or bitmap fallback. |
| 3. Raster consumer | Core `software_renderer.h/.cpp` | Remove public TextPainter constructors, state and dispatch. Keep Core TextSystem painting for PreparedText. Legacy Text commands must refuse deterministically without target writes. |
| 4. Runtime consumers | `tools/native/win32_browser.cpp`, ESP32 retained UI/installed worker/explicit typography fixtures, P4/P5/P6 smoke, ScriptTaskFrameRenderer/Canvas/benchmarks | Remove GDI as a text measurement/paint backend; products and workers register Core font resources. Decide and implement the fate of legacy acceptance fixtures so callback code is no longer a required build path. |
| 5. API / consumer sweep | Core public headers, tests/benchmarks/package consumers, Runtime installed-package consumers and porting docs | Delete `TextMeasureProvider`, `TextPainter`, range/family/additive hooks and implementation branches. Rewrite callback-specific tests and `c5_flex_grid_consumer` / `wrapped_row_flex_consumer`; rerun full/cropped, feature-disabled and installed-package gates. |

Already migrated and not to be counted again: AppFontSet is JFFONT2-only;
Canvas2D and the embedded host use Core TextSystem; the installed worker has a
bounded painterless v10 path, but it refuses general legacy Text commands and
does not establish full migration. WS147 `mainline1` hardware acceptance is
complete and must not be repeated; it does not qualify `mainline2` or separate
worker/BMP named profiles.

Test migration notes: Core `shared_inline_tests.cpp::generated_text_fallback_and_invalidation`
already covers #65 generated text preparation/refusal; `text_system_tests.cpp::core_prepared_text_ellipsis`
covers whole-owner refusal when generated text conflicts with ellipsis. The
callback-only `text_provider_tests.cpp` should be rewritten as TextSystem
measurement/wrapping coverage or removed. Runtime's full/cropped
`canonical-abi4-installed-consumers` is the main installed-consumer gate, but
must include the exact Core #65 source identity. Convert
`c5_flex_grid_consumer` and `wrapped_row_flex_consumer`, and retain the R3
full/cropped, Node identity on/off and no-RTTI matrix. The 16 feature-profile
matrix currently configures/checks manifests; it is not 16 full build runs.
