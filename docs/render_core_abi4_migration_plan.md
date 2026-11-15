# Render Core ABI4 Integration Plan

> Last updated: 2026-11-15; Applies to: 0.6.0-dev. Core development contract: 0.9.0-dev / ABI4.

> **Latest checkpoint (2026-11-15):** Runtime PRs #99-#106 are merged; #99-#103 and #105-#106 passed all 19 CI checks each. PR #103 (`d6fcaff4`) makes application fonts JFFONT2-only, removes the legacy AppFontSet/AppHost callback path, and qualifies Core #62 in current-mainline and ordinary ABI4 lanes. Core PR #63 (`8aceb6d8`, source commit `89acc36a`, source identity `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`, 163 inputs) moves open select popup text to Core `PreparedText`; this Runtime branch adds worker input/render regression coverage and pins that source in CI. Runtime PR #105 (`81d99750`) separates the ESP32 build boundary: CoreText omits generation/compilation of the legacy Noto C++ bitmap pack; the legacy profile and explicitly selected bitmap-painter fixtures retain it. A CoreText P4/P5/P6 benchmark smoke and benchmark Kconfig allowance are built from Runtime `8b59afd6` and Core follow-up `89acc36a` with IDF 5.3.1: CoreText enabled, legacy font macro disabled, map free of legacy font symbols, BIN 3,503,280 bytes (SHA-256 `29634ea9860508ff46f1ad6c1d7d65e0d16b8296f18a2c6e13771f85dc8e5535`). This is build evidence only; the smoke has not run on hardware. CoreText remains opt-in, and non-retained/Win32 production defaults remain open. Runtime still locks signed Core 0.8.0 / ABI3; ABI4 has not been signed or released. `mainline1` passed native Home/Activity/Weather/back visual acceptance; the distinct `mainline2` image remains unaccepted.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Signed Core v0.8.0 / ABI3 remains Runtime's production lock. | Publish the reviewed final ABI4 contract, then update the exact lock. |
| Core mainline | PR #63 (`8aceb6d8`) includes source commit `89acc36a`, CI passed; 0.9.0-dev / ABI4, source identity `e842b2e62fd51777cbc6dd6a6a9fea3b9ea68938ce4a6e434eb7c2baaf1adb67`, 163 implementation inputs. Open select popup rows now use Core `PreparedText`. | Runtime consumer qualification, remaining generated-content and callback retirement. |
| Runtime mainline | PR #103, `d6fcaff4`, CI 19/19 (run `43675905306`); #99-#103 and #105-#106 are merged, including ABI4 identity/consumer qualification, embedded host, installed worker, ellipsis repaint qualification, JFFONT2-only app fonts, CoreText font-pack isolation and mainline1 device record. | Runtime Core #63 popup consumer CI is in this branch; remaining generated BitmapFont/default consumers, full callback retirement and release criteria. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PR #101 makes canonical v10/painterless CoreText independent of BMP. This branch tests the Core #63 popup through canonical v9/v10 input, prepared packets, visible row pixels and selection. | Core #63 consumer CI and exact device/profile acceptance remain downstream. |
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
| ESP32 retained native/default | Named CoreText profile is merged; Runtime commit `66be335c` omits the generated C++ pack when CoreText is selected. CoreText remains opt-in; legacy remains the default profile. | Qualify CoreText as default, then remove or explicitly retain only isolated legacy consumers; qualify supported properties/routes. |
| ESP32 non-retained main entry | Separate provider factories remain. | Migrate or explicitly retire the entry, not only retained UI. |
| Win32 default | Explicit CoreText catalog mode is merged; GDI remains default. | Bounded ellipsis/property parity, repaint gates and catalog default adoption. |
| AppFontSet | Complete in #103: JFFONT2-only catalog, callback accessors/browser precedence removed, samples and tools converted. | No remaining AppFontSet work; generated C++ BitmapFont defaults are tracked separately. |

Canvas2D already uses TextSystem and is not an outstanding callback consumer.
The installed-worker change refuses ordinary callback-dependent `Text` commands
before framebuffer writes; that is a bounded admission policy, not proof that
all formerly supported generated/widget text contexts have migrated.

Host evidence for merged #101: full and four-family
cropped Core PR60 each pass 7/7 resource/admission/text tests. Both BMP variants
prove visible prepared text, JavaScript mutation, replay after teardown and
atomic refusal. Locked installed ABI3 passes four admission tests; the two
canonical tests intentionally skip. The CoreText-OFF legacy path is explicit.

### C. Core-owned text API consolidation

Core PR #61 merged as `3152300a` after all four CI jobs passed (run `43670844631`). It implements bounded owner-side paint ellipsis; full MSVC tests pass 33/33 and installed full/cropped text consumers pass 1/1 each. The cropped suite's sole shared-inline shadow assertion reproduces unchanged on its base. Runtime #102 has merged its exact source pin and five real-font incremental/full ellipsis frames, including a visible clip-only control. Core #62 prepares closed native control labels/select arrows transactionally with Core metrics and packets; it merged as `38148e73`. Core #63 (`89acc36a`) now prepares every visible open-select popup row before publishing the overlay and refuses the full overlay on missing glyphs or budget refusal. The Runtime consumer test in this branch verifies the exact Core source through input, visible pixels, selection and popup close. Generated-content ownership and broader property/default qualification remain separate. It must preserve
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

1. Merged: Runtime #99-#103; all five PRs passed 19/19 CI. #103 closes the
   serialized AppFontSet/JFFONT0/1 path and validates against exact Core #62.
2. Review/merge the Runtime Core #63 consumer PR after CI; then migrate generated
   C++ BitmapFont defaults and remaining native/non-retained and Win32 GDI
   consumers, and close generated-content ownership/property qualification.
3. Convert remaining callback-dependent tests/fixtures, then remove the public
   provider/range/family/additive APIs and implementation branches in one
   coordinated pre-release change.
4. Only after consumer, full/cropped, feature-disabled and archive gates pass,
   sign and publish ABI4 and update Runtime's exact production dependency lock.
5. Run the reserved `mainline2` WS147 acceptance when the user is available;
   the successful `mainline1` observation is recorded but does not substitute
   for this exact artifact or the host/API release gates.
