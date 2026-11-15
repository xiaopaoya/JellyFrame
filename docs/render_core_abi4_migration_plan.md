# Render Core ABI4 Integration Plan

> Last updated: 2026-11-15; Applies to: 0.6.0-dev. Core development contract: 0.9.0-dev / ABI4.

> **Latest checkpoint (2026-11-15):** Runtime PR #111 merged as `fa5176a0`; Runtime #99-#111 are merged. #111 promotes the WS147 band-shell CoreText profile and merges the Win32 ABI4 text-property/incremental-versus-full repaint matrix (293 frames, 27 flows). The accepted `mainline1` hardware observation is already recorded and must not be repeated. Core PR #65 merged as `1d5c746d` (source commit `d327eee4`): generated pseudo labels and list markers now use Core TextSystem preparation when shared-inline declines, or report refusal without emitting legacy Text commands; unbound legacy behavior remains available. Core #65 CI passed all four jobs. Runtime `fa5176a0` was then configured and built against the exact Core #65 source identity `52ae4354efdae95d4dbfdfb6a2ef1ad8fbbfb99029bdd922105ac2447302f62d` (163 files); after adding the Core test-hook source to the Runtime in-tree test integration, Release build and CTest passed 60/60. This is host source-override evidence, not a signed package lock or device acceptance. Runtime still locks signed Core 0.8.0 / ABI3; ABI4 is not signed or released. `mainline2` and installed-worker popup remain separate named device profiles; no new hardware observation is implied here.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Signed Core v0.8.0 / ABI3 remains Runtime's production lock. | Publish the reviewed final ABI4 contract, then update the exact lock. |
| Core mainline | PR #65 (`1d5c746d`, source `d327eee4`) follows Core #63 and routes generated pseudo labels/list markers through Core TextSystem or explicit refusal. All four CI jobs passed. | Retire remaining provider/painter/range/family/additive callback surfaces after inventorying non-generated consumers and fixtures. |
| Runtime mainline | PR #111, merge `fa5176a0`; #99-#111 are merged. WS147 CoreText-default profile and Win32 CoreText property/repaint matrix are included. Runtime Release build plus CTest 60/60 pass with the exact Core #65 source override. | Finish consumer/fixture inventory and callback API retirement; ABI4 release gates remain. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PR #101 makes canonical v10/painterless CoreText independent of BMP. PR #107 tests Core #63 popup input, prepared packets, visible row pixels, selection and close through the canonical v10 worker on the exact source identity. PR #109 adds a full-feature WS147 profile and select-popup app, with IDF build, packaging and provider regressions passing. Core #65 generated pseudo-content is included in the Runtime source-override build/CTest. | Capture user-visible popup rendering/touch on the dedicated profile when scheduled; inventory any remaining callback-dependent widget text. |
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
