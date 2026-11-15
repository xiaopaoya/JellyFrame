# Render Core ABI4 Integration Plan

> Last updated: 2026-11-16; Applies to: 0.6.0-dev. Core released version: 0.9.0 / ABI4.

> **Latest checkpoint (2026-11-16):** Runtime PRs #99–#115 are merged. #111 promotes the WS147 band-shell CoreText profile and adds the Win32 ABI4 property/incremental-versus-full repaint matrix (293 frames, 27 flows); #114 retires Runtime text callbacks, and #115 expands ABI4 desktop-tool/benchmark build coverage. Core PR #67 (`16a83211ec0992f8e25ec0fc977eb6fd158716e3`) completes the ABI4 contract. Core v0.9.0 / ABI4 is signed and published; the archive and `.sha256` sidecar are uploaded, and GitHub's archive digest matches `8a0568b548a7e30956c515bba753ad7fe7f0fbb501e319cbef926eec2b855628`. Runtime lock candidate `codex/core-0.9.0-abi4-lock-20261116` updates the lock, manifests/templates, provenance checks and installed full/cropped consumers; local full/cropped CTest each pass 22/22. Three ESP32 probe generators were also corrected to emit Core 0.9.0. PR #116 is open; after master advanced through #115 it required a merge update, and CI has not yet started. Core #63 popup hardware acceptance passed on named profile `0.6.2-abi4-installed-core63-device1`. Runtime master remains on Core 0.8.0 / ABI3 until #116 passes CI and merges. `mainline2` remains separate device work.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Core v0.9.0 / ABI4 is signed and published; both assets are uploaded and the remote archive digest matches the local hash. | Runtime lock PR #116 must pass full/cropped, ABI3 source-override and remaining CI. |
| Core mainline | Core PR #67 (`16a83211ec0992f8e25ec0fc977eb6fd158716e3`) and ABI4 release gates are complete. | None; Runtime consumes the released package through #116. |
| Runtime mainline | PRs #99–#115 are merged. Candidate #116 updates the exact lock, app manifests/templates, provenance CI, ABI4 installed consumers and three ESP32 probe manifests; full/cropped local CTest each pass 22/22. | Resolve latest-master conflicts and run PR CI; merge only after all required checks pass. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PRs #101/#107/#109/#114 are merged. The user accepted the Core #63 popup app: title, all options, selection result and touch work. | Build and accept the separate production-lock worker image after #116 merges. |
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
Core tests and the Runtime `watch_weather` checks. ABI4 package publication is
complete; Runtime's downloaded-package full/cropped matrix and locked ABI3
compatibility lane are now the remaining integration gates in PR #116.

Exit: no production callback consumer remains. Core v0.9.0 is signed and
published, and source-level full/cropped consumers and Core CI passed. Runtime's
downloaded-package matrix and ABI3 compatibility lane remain in PR #116.

### D. Release and production lock

Core v0.9.0 / ABI4 has been released from the final reviewed source and signed.
The tag, archive checksum, source manifest, profile and Release assets are
verified. Runtime's exact version, ABI, source/archive lock and installed-artifact
CI are updated on the candidate branch, pending PR CI and merge. Keep ABI3 as
rollback evidence; do not imply binary interchangeability.

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
4. Completed: Core #66/#67 and Runtime #114/#115 retire the callback paths,
   expand tool/benchmark coverage and pass their merged CI gates.
5. Core v0.9.0 / ABI4 is signed and published; its archive and sidecar hashes
   are verified. Runtime PR #116 pins the exact release and adds full/cropped
   installed-consumer CI; local CTest passes 22/22 in each profile.
6. Update #116 to current master, resolve CI/docs/test conflicts, and ensure its
   pull-request workflows start. Merge only after the full required check set
   passes.
7. After the lock merges, build named WS147 native and installed-worker images
   from the production lock, record BIN/ELF/package identities, and run hardware
   acceptance for those exact profiles.
