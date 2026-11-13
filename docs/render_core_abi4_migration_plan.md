# Render Core ABI4 Integration Plan

> Last updated: 2026-11-14; Applies to: 0.6.0-dev. Scope: integrate the reviewed experimental capabilities into the governed Core/Runtime path and retire the host-owned text compatibility surface.

## Current checkpoint

- Runtime PR #71 is merged as `50170381`. It qualifies Core `b0bfd834` / ABI 4 in full and cropped installed-consumer lanes. The post-merge Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) passed.
- Core PR #54 is merged as `de9733d2`. It fixes a bounded offline shaping prototype and records a deterministic data-encoding spike. The post-merge Core workflow [43462040656](https://github.com/xiaopaoya/JellyFrame-Render-Core/actions/runs/43462040656) passed. It adds no Core package implementation inputs and does not add runtime shaping support.
- Core PR #55 is merged as `64c1cb42`; it removes the unused `HostTextAdapter` wrapper/header while retaining provider interfaces that still have production consumers. Core CI passed. Runtime workflow [43472234209](https://github.com/xiaopaoya/JellyFrame/actions/runs/43472234209) then checked out exactly `64c1cb42` in both full and cropped ABI4 lanes; both installed-consumer suites passed and reported source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5` across 163 implementation inputs. The earlier `b0bfd834` identity remains only the pre-cleanup baseline.
- Runtime PR #72 is merged as `5c45c65a`; its bilingual task table separates ABI4 qualification, production adoption, callback retirement, and device acceptance. Its CI passed.
- Runtime PR #73 is merged as `2c253566`; it adds a checksum-verified, offline bootstrap for the signed Core `0.8.0` / ABI3 archive. Runtime CI passed, including 67 local CTests before PR creation. The bootstrap makes the current production lock reproducible; it does not switch Runtime to ABI4 or establish a default font catalog.
- Runtime PR #75 merged as `6302dd1d`; its CI passed across ABI4 installed consumers, v10 and R3 matrices, Windows, standalone Core, and ASan/UBSan. It adds the shared Noto Sans SC 16 px Regular/Medium/Bold Core catalog, transactional owned/borrowed catalog attachment, provenance tooling, and generated-catalog weight/lifetime tests.
- B2 Runtime PR #76 merged as `2e89b0de`; all CI passed. It stages Core text behind an explicit `--use-app-fonts` validation path, preserving GDI default and legacy JFFONT rendering until compatibility gates pass. B1 Runtime PR #77 merged as `af594be4`; all CI passed, and its full/cropped Core-source host profiles each passed 6/6 focused CTests across installed ownership, four CoreText/BMP combinations, and App Runtime. The named WS147 profile builds with ESP-IDF v5.3.1 at 7,395,718 bytes (12% of the 8 MiB app partition free); the same-code band-shell baseline is 5,230,594 bytes (38% free), a 2,165,124-byte increase. Both use 16,383/16,384 bytes of IRAM, so near-full IRAM use predates this profile. No WS147 serial device was present for physical acceptance.
- Runtime production remains pinned to signed Core `0.8.0` / ABI 3. ABI4 has no signed release and is not yet a production dependency.
- On the production branch, complete-resource Core text remains opt-in. WS147 production defaults still use the callback/v6 route; the named Core-text profile is explicit. Desktop still uses its GDI default, with Core available only through opt-in validation. `TextMeasureProvider` and `TextPainter` remain public Core APIs because production consumers still use them; only the unused `HostTextAdapter` convenience layer has been removed.
- No ABI4 named-device-profile provenance or ABI4 firmware acceptance is recorded. This is a rollout gate after host integration, not evidence supplied by desktop CI.

## Parallel workstreams

### A. Resource delivery and catalog defaults

The clean-checkout, checksum-verified bootstrap for the current signed Core package is complete in Runtime PR #73. Runtime PR #75 merged the shared default resource set: Noto Sans SC 16 px Regular/Medium/Bold, 4-bit coverage, 4,539 codepoints per face, under OFL 1.1. The shared API supports owned copies and stable borrowed views. Desktop and WS147 consumers are merged in PRs #76 and #77. The font catalog totals 2,159,771 bytes. With IDF v5.3.1, the named Core-text profile links to 7,395,718 bytes with 12% app-partition margin, versus 5,230,594 bytes and 38% margin for the same-code band-shell baseline. Both profiles use 16,383/16,384 bytes of IRAM, so the near-full IRAM result is pre-existing. Keep app resources owned by Runtime while Core owns font selection, metrics, shaping, and glyph preparation.

**Exit evidence:** fresh checkout builds without user-local artifacts; full/cropped/feature-disabled profiles pass; package identity and generated provenance match; resource and memory budgets are documented and tested.

### B. Runtime consumer migration (parallel subtracks after A's resource contract)

- **B1 Native UI and installed worker (PR #77 merged):** switches retained UI and installed-worker resources to the shared catalog and defines a named `sdkconfig.ws147_core_text.defaults` profile while leaving production Kconfig defaults unchanged. Full/cropped host combinations, PR CI, and an IDF v5.3.1 build passed; the image is 7,395,718 bytes with 12% app-partition headroom. Physical WS147 acceptance remains required because no board serial port was present.
- **B2 Desktop (PR #76 merged, staged):** adds an explicit Core-catalog validation path and loads JFFONT2 app supplements, while preserving the current GDI default and legacy JFFONT bitmap path. The first attempt to make Core unconditional failed existing font-policy and incremental-repaint tests and refused `letter-spacing`; those failures established compatibility gates, not test noise. The staged implementation passed local desktop checks and all PR CI. Switch the default only after supported-property coverage and the full incremental-repaint equivalence suite pass under Core.

**Exit evidence:** no production default selects callback measurement/paint; installed-package and source-override captures agree; app-font and malformed-resource refusal tests pass.

### C. Core-owned text API consolidation

After B's consumers use the owned catalog, route Core layout, intrinsic sizing, layer construction, repaint/reuse, rasterization, and Canvas2D through `TextSystem`/owned text services directly. Remove callback-identity cache contracts and migrate callback-based tests, fixtures, archive tests, and public porting docs. `HostTextAdapter` was removed separately in PR #55 because repository-wide inventory found no production caller; this does not retire the underlying `TextMeasureProvider`, `TextPainter`, provider-based font metrics/range hooks, or their fallback implementations. Remove those remaining interfaces only in a coordinated pre-release API change after all consumers migrate.

**Exit evidence:** repository-wide production search finds no legacy callback path; the full Core suite, source archive, installed full/cropped consumers, and disabled-feature profiles pass with callbacks removed; public guides describe only the owned-font contract.

### D. Release and production lock

Cut a signed deterministic Core release only from the final reviewed API/resource contract. Verify the source archive checksum, manifest, feature profile, standalone build/install/tests, then update Runtime's exact version/ABI/source lock and published-artifact CI. Retain the ABI3 lane as rollback/migration evidence; do not claim ABI3/ABI4 binary interchangeability.

**Exit evidence:** signed tag and archive; Runtime installed-package plus matching source-override tests on the same revision; CI and EN/ZH compatibility/release documents agree.

### E. Device OS admission (downstream)

Bind the final Runtime/Core provenance and selected feature families to a named WS147 profile and firmware manifest. Run physical installed-app, text/image, input, lifecycle/recovery, memory, concurrency, and performance acceptance on that exact image. Existing ABI1/ABI3 image evidence cannot qualify ABI4.

**Exit evidence:** reproducible image manifest and raw device logs identify the exact Core release/profile/source hash; all applicable port gates pass. This is required for device adoption, but can proceed after the host-owned migration is merged and released.

## Dependency order

```text
A1 bootstrap complete; A2 catalog/API merged (#75)
       |------------------|
       v                  v
B1 native UI/worker     B2 desktop
       \                  /
        \                /
         v              v
       C remove callback APIs
                 |
                 v
       D signed release + Runtime lock
                 |
                 v
       E named device profile + hardware acceptance
```

Core API inventory and test migration can be audited in parallel with A and B, but public API removal must wait until every production consumer has moved. Shaping lookup encoding remains a separate experiment: its current byte counts are not firmware cost or runtime qualification and are not a prerequisite for integrating already-qualified ABI4 capabilities.

## Immediate next actions

1. **Complete:** PRs #54, #55, #71, #72, and #73 are merged. Runtime's signed ABI3 source bootstrap is reproducible; Core's unused wrapper is gone.
2. **Complete:** Runtime full/cropped ABI4 qualification now covers Core `64c1cb42` and source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5` (163 inputs), via workflow `43472234209`.
3. **Complete:** PR #75 merged the reviewed A2 shared catalog/API after all CI checks passed.
4. **Host integration and target build complete, device gate pending:** PR #76 (desktop) and #77 (WS147) are merged with CI passing; the named WS147 profile builds under IDF v5.3.1 with 12% app-partition headroom. Keep the normal WS147 Kconfig default unchanged until physical acceptance; keep Core text opt-in on desktop until supported-property coverage and incremental-repaint equivalence pass.
5. Continue C's callback-consumer inventory after B; defer provider API removal until all consumers move. Keep signed ABI4 release/Runtime lock and physical device adoption gated on B, C, and D.
