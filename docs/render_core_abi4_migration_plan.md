# Render Core ABI4 Integration Plan

> Last updated: 2026-11-16; Applies to: 0.6.0-dev. Core development contract: 0.9.0-dev / ABI4.

> **Latest checkpoint (2026-11-16):** Runtime PR #114 merged as `a197ceb4`; CI run `43758728864` succeeded across all checks. Core PR #66 merged as `079de28a`; post-merge CI run `43759873775` succeeded. Runtime production still locks signed Core 0.8.0 / ABI3; ABI4 is not signed or released. Core #63 popup acceptance passed on `installed-core63-device1` (Runtime `f609a1d4`, Core source `e842b2e6`, BIN SHA-256 `8DBA1F73...E2A6A0F`): all options were visible and choosing CHARLIE updated the result. That device image predates Core #66 and proves popup behavior only, not the identity of a post-merge ABI4 firmware.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Signed Core v0.8.0 / ABI3 remains Runtime's production lock. | Publish and qualify the signed ABI4 package, then update the exact lock. |
| Core mainline | PR #66 is merged as `079de28a`; post-merge CI run `43759873775` passed. Legacy text measure/paint callbacks are retired from Core. | Produce a deterministic signed ABI4 candidate from the merged source; verify archive, manifest/profile and installed consumers. |
| Runtime mainline | PR #114 is merged as `a197ceb4`; CI run `43758728864` passed. Production consumers use Core TextSystem; Win32 keeps callback-free reuse after Core compares computed layout styles. | Pin the qualified ABI4 package and run both ABI3 compatibility and ABI4 package matrices. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PRs #101/#107/#114 are merged. The user accepted popup options, selection and touch on `installed-core63-device1`, built before Core #66 merged. | Rebuild and accept a named worker image after the signed ABI4 package is pinned. |
| Native WS147 | `mainline1` Home/Activity/Weather acceptance remains valid; the board was restored to it after popup testing. | Build and accept a new image tied to the post-release Runtime/Core identities; do not repeat the old `mainline1` test. |
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
| ESP32 retained native/default | Runtime #114 is merged with Core-owned text consumers; ABI3 compatibility references stay behind ABI guards. The previously accepted `mainline1` image predates #114/#66. | Build and accept a named image after the signed ABI4 package is pinned. |
| ESP32 P4/P5/P6 smoke | The independent provider factory in `main.cpp` is used only by a benchmark/smoke fixture; CoreText profile now has separate CoreText smoke build evidence. | Keep the old fixture explicitly legacy-only and give CoreText equivalent ASCII/CJK/weight and widget coverage; this is not a product-default consumer. |
| Win32 default | PR #114 moves the default path to Core TextSystem, while preserving a Core-computed-style-only layout reuse path. GDI compatibility remains on ABI3. The #111 matrix covers 293 frames across 27 flows. | Run the release package matrix on the pinned ABI4 package; no ABI4 GDI callback migration remains. |
| AppFontSet | Complete in #103: JFFONT2-only catalog, callback accessors/browser precedence removed, samples and tools converted. | No remaining AppFontSet work; generated C++ BitmapFont defaults are tracked separately. |

Canvas2D already uses TextSystem. Runtime #114 removes the remaining production
host measure/paint consumers and qualifies generated pseudo-content paths
against Core-owned preparation or explicit refusal. Core #66 retires the legacy
callback APIs; the merged Runtime/Core pair has passed its PR and post-merge CI.
The remaining gates concern packaging and release qualification, not callback
consumer migration.

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

Core #66 has merged the coordinated removal of provider constructors/cache
identity, fallback Text emission, compositor painter input, provider-driven
layout reuse/repaint APIs, public measure/range helpers and bitmap callback
helpers. Prepared Core text is the supported text path; unsupported or unbound
text is explicitly refused. Runtime #114 removes the corresponding production
consumers. The weather small-card prepared-text cache regression is covered by
Core tests and the Runtime `watch_weather` checks. ABI4 is now at the package
release gate: sign only after source-archive, installed full/cropped consumers,
supported-tool build coverage and disabled-feature gates pass on the exact
merged Core source.

Exit: no production callback consumer remains. Source-level full/cropped
consumers and Core CI passed on the merged source identity. The signed release
archive, downloaded-package Runtime matrix and disabled-feature release gates
remain. Hardware qualification is a downstream adoption gate.

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
This pre-merge `mainline2` artifact is not the qualification target for the
upcoming ABI4 package. Build and accept a new image after the production lock is
updated; no device acceptance can be inferred from a build or positive assumption.

## Dependency order

```text
merged Core/Runtime ABI4 source -> signed release archive
                                           |
                                  exact Runtime package lock
                                           |
                            named device builds and acceptance
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
4. Completed on source identity `8328dbf1...02d1186` / 159 files: Core full/
   cropped CTest, archive 3/3, installed full/cropped TextSystem, C5, wrapped-
   row and natural-image consumers, and Runtime CTest 113/113.
5. Completed: Core #66 and Runtime #114 merged; both post-merge CI runs passed.
6. Coverage audit completed; the ABI4 CI change explicitly builds `jellyframe_pseudo_browser` and `jellyframe_virtual_bench` and runs the benchmark smoke. Local ABI4 build and both focused tests pass; PR CI remains pending.
7. Produce and sign the versioned Core archive, verify checksum/source manifest/profile, then run Runtime consumers against the downloaded full/cropped ABI4 package.
8. Pin the qualified ABI4 release in Runtime and run the ABI3 locked-package compatibility matrix alongside the ABI4 release-package matrix.
9. Build named WS147 native and installed-worker images from the locked production state, record BIN/ELF/package identities, and run their separate device acceptance checks.
