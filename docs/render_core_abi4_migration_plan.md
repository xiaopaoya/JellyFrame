# Render Core ABI4 Integration Plan

> Last updated: 2026-11-15; Applies to: 0.6.0-dev. Core development contract: 0.9.0-dev / ABI4.

This is the current execution plan. Earlier checkpoint narratives are preserved
in Git history and their linked PRs; they are not additional open tasks. The
[consumer retirement inventory](render_core_text_consumer_retirement.md) records
actual call sites and the exact native device candidate.

## Current checkpoint

| Boundary | Current state | Remaining gate |
| --- | --- | --- |
| Released dependency | Signed Core v0.8.0 / ABI3 remains Runtime's production lock. | Publish the reviewed final ABI4 contract, then update the exact lock. |
| Core mainline | PR #62, `38148e73`, CI 4/4 (run `43673424532`), 0.9.0-dev / ABI4; source identity `caea681129a7201454fed709f1055a31886ef729779646853cc450e0c71a35e1`, 163 implementation inputs. | Remaining consumer/property migration and callback retirement. |
| Runtime mainline | PR #102, `2f87d52e`, after CI 19/19 (run `43671336751`); exact Core #61 pin and positive ellipsis repaint gate are merged. | Default adoption is separate from successful source-override qualification. |
| Embedded host | PR #100 binds one TextSystem/JFFONT2 fixture to layout, layers and paint. Locked ABI3 and source ABI4 full/cropped tests pass. | No remaining provider callback in this example. |
| Installed worker | PR #101 makes canonical v10/painterless CoreText independent of BMP. | Exact device acceptance remains downstream. |
| Native WS147 | Exact `0.6.2-abi4-mainline1` built and package-verified, not flashed. | Observe this exact artifact on Home/Activity/Weather/back. |
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

Remaining: replace generated C++ BitmapFont default resources. The current
app-font change removes serialized JFFONT0/1 and AppFontSet callback precedence;
manifest/import resources are canonical JFFONT2 with offline Core preparation.
Preserve deterministic family/weight selection, font-byte lifetime, missing-font
refusal and resource budgets. Do not add more host text callbacks.

### B. Runtime consumer migration

| Consumer | Progress | Exit evidence |
| --- | --- | --- |
| Embedded host | Complete in #100. | Visible ink, click/checkbox/select and RGB565 flush; ABI3 and ABI4 full/cropped. |
| ESP32 installed worker | Merged #101 painterless canonical v10 path; owned catalog/snapshot survives replay; BMP is optional. | Host full/cropped and locked ABI3 matrix, real IDF builds, CI, later exact device profile. |
| ESP32 retained native/default | Named CoreText profile is merged; default still uses generated BitmapFont providers. | Migrate default resource contract and qualify supported properties/routes. |
| ESP32 non-retained main entry | Separate provider factories remain. | Migrate or explicitly retire the entry, not only retained UI. |
| Win32 default | Explicit CoreText catalog mode is merged; GDI remains default. | Bounded ellipsis/property parity, repaint gates and catalog default adoption. |
| AppFontSet | Current change accepts JFFONT2 only, removes callback accessors and browser precedence, and converts samples/tools. | Full/cropped/locked-ABI3 consumer gates and named-font visible-output qualification; PR/CI. |

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

Core PR #61 merged as `3152300a` after all four CI jobs passed (run `43670844631`). It implements bounded owner-side paint ellipsis; full MSVC tests pass 33/33 and installed full/cropped text consumers pass 1/1 each. The cropped suite's sole shared-inline shadow assertion reproduces unchanged on its base. Runtime #102 has merged its exact source pin and five real-font incremental/full ellipsis frames, including a visible clip-only control. Core #62 prepares closed native control labels/select arrows transactionally with Core metrics and packets; it merged as `38148e73` after CI 4/4, and is not yet a release. Open select popups and generated-content ownership remain separate. Broader property/default qualification remains separate. It must preserve
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

Home/Activity/Weather/back, all eight forecast text rows, legibility and clipping
remain pending on this exact image. The latest missing-forecast-text observation
is not closed by desktop route tests or by an unidentified older board image.
Installed worker/BMP tests require their own named image and resource profile.
No device acceptance can be inferred from a build or positive assumption.

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

1. Review/CI/merge the current Runtime JFFONT2-only app-font/tool change.
   Core #62 is merged; current CI pins its exact commit/source identity.
2. Extend open-popup/generated-text consumers and native/desktop defaults before
   adoption. #101 worker, #61 ellipsis and #102 qualification are already merged.
3. Convert remaining callback users/tests and remove the obsolete public API
   together after their resource/default migrations.
4. Publish signed ABI4 and switch the exact Runtime lock only after those gates.
5. Run the reserved native board acceptance when the user is available; retain
   device failures separately from host/package completion and prepare installed
   device admission on a capacity-qualified profile.
