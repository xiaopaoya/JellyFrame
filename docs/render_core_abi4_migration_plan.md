# Render Core ABI4 Integration Plan

> Last updated: 2026-11-16; Applies to: 0.6.0-dev. Core released version: 0.9.0 / ABI4.

> **Latest checkpoint (2026-11-16):** Runtime PR #116 (`f2169d652e4e4c16a84935cd955f4524ace14db0`) merged the signed Core v0.9.0 / ABI4 production lock. PR #117 (`673f23a2778894287e292473980903fbe8e7cff3`) fixed WS147 ABI4 build boundaries. PR #119 recorded the lifecycle-image acceptance scope. PR #120 merged as `7dd34aea69e6163541132fe40dc130ea747be304` after 23/23 checks, completing provider manifest/profile and firmware provenance validation. Core release archive digest remains `8a0568b548a7e30956c515bba753ad7fe7f0fbb501e319cbef926eec2b855628`. Release, production-lock and provider integration gates are complete.

The post-PR117 native image passed the recorded human Home/Activity/Weather and visible-button touch observations. The current board runs `0.6.2-abi4-pr116-inst1`, reflashed on COM19 with esptool hash verification. Typed JFDP info identifies Core 0.9.0 / ABI4 and Runtime source `d4ba0b846ca21a7d48d420651493133d8b1c209a`. Generation200 install was accepted and launch returned `ok`. Logs report `render_status=0`, `accepted_seq=1`, `presents_failed=0`. Human visual/touch confirmation for generation200 remains pending.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Signed Core v0.9.0 / ABI4 archive and sidecar verified. | Complete. |
| Core mainline | Core PR #67 (`16a83211ec0992f8e25ec0fc977eb6fd158716e3`) and ABI4 release gates are complete. | None; Runtime consumes the released package through #116. |
| Runtime mainline | #116 production lock, #117 WS147 build fixes and #120 provider provenance validation are merged; current master `7dd34aea`. #120 checks passed 23/23. | Complete. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | Exact inst1 firmware is flashed; generation200 JFDP install accepted, launch ok and render telemetry successful. | Human visual/touch response for generation200 pending. |
| Native WS147 | Post-PR117 native1 image passed recorded Home/Activity/Weather and button touch observations. | Complete for that exact image and observed scope. Current board runs installed inst1. |
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
a direct measurement of the pack's net cost. CoreText default promotion completed in #111. Preserve deterministic family/weight selection, font-byte lifetime,
missing-font refusal and resource budgets. Do not add more host text callbacks.

### B. Runtime consumer migration

| Consumer | Progress | Exit evidence |
| --- | --- | --- |
| Embedded host | Complete in #100. | Visible ink, click/checkbox/select and RGB565 flush; ABI3 and ABI4 full/cropped. |
| ESP32 installed worker | Merged #101 painterless canonical v10 path; owned catalog/snapshot survives replay; BMP is optional. | Host full/cropped and locked ABI3 matrix, real IDF builds, CI, later exact device profile. |
| ESP32 retained native/default | #114 Core-owned text consumers merged; post-PR117 production-lock native1 accepted. | Exact image identity and observations are recorded below. |
| ESP32 P4/P5/P6 smoke | The independent provider factory in `main.cpp` is used only by a benchmark/smoke fixture; CoreText profile now has separate CoreText smoke build evidence. | Keep the old fixture explicitly legacy-only and give CoreText equivalent ASCII/CJK/weight and widget coverage; this is not a product-default consumer. |
| Win32 default | #114 Core TextSystem path and #111 293-frame/27-flow matrix delivered; #116 published-package and ABI3 compatibility CI passed. | Complete. |
| AppFontSet | Complete in #103: JFFONT2-only catalog, callback accessors/browser precedence removed, samples and tools converted. | No remaining AppFontSet work; generated C++ BitmapFont defaults are tracked separately. |

Canvas2D already uses TextSystem. Runtime #114 removes the remaining production
host measure/paint consumers and qualifies generated pseudo-content paths
against Core-owned preparation or explicit refusal. Core #66 retires the legacy
callback APIs; the merged Runtime/Core pair has passed its PR and post-merge CI.
Package and release qualification completed in #116. Current generation200 installed-device human observation remains pending.

Host evidence for merged #101: full and four-family
cropped Core PR60 each pass 7/7 resource/admission/text tests. Both BMP variants
prove visible prepared text, JavaScript mutation, replay after teardown and
atomic refusal. Locked installed ABI3 passes four admission tests; the two
canonical tests intentionally skip. The CoreText-OFF legacy path is explicit.

### C. Core-owned text API consolidation

Core PR #61 merged as `3152300a` after all four CI jobs passed (run `43670844631`). It implements bounded owner-side paint ellipsis; full MSVC tests pass 33/33 and installed full/cropped text consumers pass 1/1 each. The cropped suite's sole shared-inline shadow assertion reproduces unchanged on its base. Runtime #102 has merged its exact source pin and five real-font incremental/full ellipsis frames, including a visible clip-only control. Core #62 prepares closed native control labels/select arrows transactionally with Core metrics and packets; it merged as `38148e73`. Core #63 (`89acc36a`) now prepares every visible open-select popup row before publishing the overlay and refuses the full overlay on missing glyphs or budget refusal. Runtime #107 verifies the exact Core source through canonical worker input, visible pixels, selection and popup close. Core #65 subsequently completed generated pseudo labels/list markers through Core preparation or explicit refusal. The ownership contract preserves
source layout/intrinsic dimensions, applies owner clipping after final geometry,
accounts for Q1000/spacing and output budgets, and refuses unsupported complete
contexts without host fallback. Mixed owners need whole-paragraph cutoff
ownership; truncating every leaf independently is not acceptable.

Core #66 has merged the coordinated removal of provider constructors/cache
identity, fallback Text emission, compositor painter input, provider-driven
layout reuse/repaint APIs, public measure/range helpers and bitmap callback
helpers. Prepared Core text is the supported text path; unsupported or unbound
text is explicitly refused. Runtime #114 removes the corresponding production
consumers. The weather small-card prepared-text cache regression is covered by
Core tests and the Runtime `watch_weather` checks. ABI4 package publication and Runtime downloaded-package full/cropped and ABI3 compatibility gates completed in #116. Production callback retirement is complete.

### D. Release and production lock

Core v0.9.0 / ABI4 is signed and published. Tag, archive checksum, source manifest, profile and release assets are verified. Runtime #116 merged the exact production lock and installed-artifact CI. #117 completed WS147 build compatibility. #120 completed provider manifest/profile and firmware provenance checks after 23/23 checks. ABI3 rollback evidence retains its original compatibility scope.

### E. Device admission

The historical native candidate at `D:/CodexTemp/abi4-mainline-20261115` pins Runtime
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
The post-PR117 build manifest is `D:/CodexTemp/Runtime-pr116-ws147-build-20261116/build-manifest.json`. Runtime build source is `d4ba0b846ca21a7d48d420651493133d8b1c209a`, tree `2e0326369275400b03c75ee20740b91164fc2ea6`, matching PR117 master tree. Core is v0.9.0 / ABI4. Native1 BIN SHA-256 is `A87BBFDC95F6467A065A0A5A5B436E7CD1730D22C838BEE9D44590E40D148039`, ELF SHA-256 `68642F8BE62C606092F76DE1FDDE04D0E69D659DCC425C2A56DBAD27751B4F92`. Boot reported first_present_ok=1 at 580.297 ms. The user accepted Home text/cards, Activity title/body/return, Weather large card and four small-card labels, and a visible button touch response. These observations belong to native1.

The current installed profile is `0.6.2-abi4-pr116-inst1`, BIN SHA-256 `511c6a48ad1bca5a80d1653ca71435cacabd92e2362a22eaf542c08f660252eb`, ELF prefix `2F34A22D`. Earlier idle blank-screen observations preceded app installation. On November16 this exact BIN was reflashed on COM19 with esptool hash verification. JFDP info confirmed Core0.9.0/ABI4 and source d4ba0b84. Bundle `org.jellyframe.fixture.installed-script-input` is 2420 bytes, SHA-256 `949f9904ef9dadc38926b6c8d433d4340e6218de9f908a0b8a70103370f264bc`. Generation200 install returned accepted and launch returned ok. Logs report render_status=0, accepted_seq=1 and presents_failed=0. Human confirmation of the Installed script / Ready: 0 / Tap screen and its tap progression remains pending. The fixture has three resources and no bitmap image. The first serial capture was empty, so current identity/runtime evidence comes from typed JFDP info/logs. Sampled resource telemetry retains its sampling scope.

The shared IDF v5.3.1 checkout had 97 modified paths; the build manifest records isolated source files matching the fixed IDF tree. Its build-time flashed=false and physicalDeviceTested=false fields precede the later flash and observations above.

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
5. Completed. Core v0.9.0 / ABI4 release and #116 production lock, published-package matrix and ABI3 compatibility checks.
6. Completed. #117 WS147 build fixes and #120 provider provenance validation, with #120 merged after 23/23 checks.
7. Completed. Post-PR117 native1 recorded human acceptance, exact inst1 flash verification and generation200 JFDP install/launch with successful render telemetry.
8. Pending. Human visual/touch confirmation for generation200. Preserve separate native, historical popup and bitmap/profile evidence scopes.
