# Render Core ABI4 Integration Plan

> Last updated: 2026-11-14; Applies to: 0.6.0-dev. Scope: integrate the reviewed experimental capabilities into the governed Core/Runtime path and retire the host-owned text compatibility surface.

## Current checkpoint

- Runtime PR #71 is merged as `50170381`. It qualifies Core `b0bfd834` / ABI 4 in full and cropped installed-consumer lanes. The post-merge Runtime workflow [43462107068](https://github.com/xiaopaoya/JellyFrame/actions/runs/43462107068) passed.
- Core PR #54 is merged as `de9733d2`. It fixes a bounded offline shaping prototype and records a deterministic data-encoding spike. The post-merge Core workflow [43462040656](https://github.com/xiaopaoya/JellyFrame-Render-Core/actions/runs/43462040656) passed. It adds no Core package implementation inputs and does not add runtime shaping support.
- Core PR #55 is merged as `64c1cb42`; it removes the unused `HostTextAdapter` wrapper/header while retaining provider interfaces that still have production consumers. Core CI passed. Runtime workflow [43472234209](https://github.com/xiaopaoya/JellyFrame/actions/runs/43472234209) then checked out exactly `64c1cb42` in both full and cropped ABI4 lanes; both installed-consumer suites passed and reported source identity `213ff3196864f2d7da9152bf246c29bda9849da50daff272baf551a7f9179cc5` across 163 implementation inputs. The earlier `b0bfd834` identity remains only the pre-cleanup baseline.
- Core PR #56 merged as `d9cdb239`, with ABI4 source identity `c1af69d7…23a2036f` across 163 inputs. Runtime PR #81 (`e410e4c9`) added full/cropped installed-consumer lanes pinned to that identity; both passed in workflow [43522375005](https://github.com/xiaopaoya/JellyFrame/actions/runs/43522375005). After Core PR #57, Runtime PR #85's post-merge workflow [43542749129](https://github.com/xiaopaoya/JellyFrame/actions/runs/43542749129) also passed, but its logs still checked out `d9cdb239`; it did not qualify the new Core master. This plan update changes the exact pin to Core `9e19766c` / ABI4 / 163 inputs / source identity `0e5000a6…d4e1366c` and requires fresh full/cropped CI before claiming qualification.
- Runtime PR #83 is merged as `eb68fe43` after all 19 CI checks passed. It removes the document-wide mutation generation from Core text-cache validity because unrelated paint-only image changes invalidated every text cache. Text/source, resolved style and geometry, viewport, and TextSystem identity/generation remain the cache keys. The full/cropped ABI4 lanes still qualify Core `d9cdb239` / ABI4 / 163 inputs / source identity `c1af69d7…23a2036f`.
- Runtime PR #72 is merged as `5c45c65a`; its bilingual task table separates ABI4 qualification, production adoption, callback retirement, and device acceptance. Its CI passed.
- Runtime PR #73 is merged as `2c253566`; it adds a checksum-verified, offline bootstrap for the signed Core `0.8.0` / ABI3 archive. Runtime CI passed, including 67 local CTests before PR creation. The bootstrap makes the current production lock reproducible; it does not switch Runtime to ABI4 or establish a default font catalog.
- Runtime PR #75 merged as `6302dd1d`; its CI passed across ABI4 installed consumers, v10 and R3 matrices, Windows, standalone Core, and ASan/UBSan. It adds the shared Noto Sans SC 16 px Regular/Medium/Bold Core catalog, transactional owned/borrowed catalog attachment, provenance tooling, and generated-catalog weight/lifetime tests.
- B2 Runtime PR #76 merged as `2e89b0de`; all CI passed. It stages Core text behind an explicit `--use-app-fonts` validation path, preserving GDI default and legacy JFFONT rendering until compatibility gates pass. B1 Runtime PR #77 merged as `af594be4`; all CI passed, and its full/cropped Core-source host profiles each passed 6/6 focused CTests across installed ownership, four CoreText/BMP combinations, and App Runtime. The named WS147 profile builds with ESP-IDF v5.3.1 at 7,395,718 bytes (12% of the 8 MiB app partition free); the same-code band-shell baseline is 5,230,594 bytes (38% free), a 2,165,124-byte increase. Both use 16,383/16,384 bytes of IRAM, so near-full IRAM use predates this profile. No WS147 serial device was present for physical acceptance.
- Runtime production remains pinned to signed Core `0.8.0` / ABI 3. ABI4 has no signed release and is not yet a production dependency. Core master is `9e19766c` (ABI4, 163 implementation inputs, source manifest `0e5000a685842df82b16ff1a6aa0d3a09844e07e9850deff939912d4e1366c3c`). Workflow `43542749129` passed against the old `d9cdb239` pin; fresh full/cropped qualification against `9e19766c` is pending the updated Runtime lanes.
- The route-switch regression was first traced to `valid_text_layout_cache`: ancestor dirty flags remain set until present, so the layer builder rejected text prepared by the current layout. Core PR #56 stores the document mutation generation to recognize text prepared in the current layout transaction and covers a `display:none` home/Activity route regression. Runtime PR #83 (`eb68fe43`) separately fixed its paint-only image mutation path and passed 240-frame incremental/full comparisons. A new Core-installed desktop probe then found that standalone Core still invalidated static prepared text after an unrelated input-value mutation. Core PR #57 (`9e19766c`) removes the document-wide generation from that cache validity check and adds a regression; the installed source manifest is `0e5000a685842df82b16ff1a6aa0d3a09844e07e9850deff939912d4e1366c3c` across 163 inputs. Runtime PR #85 (`7c0c9197`) strengthens the positive Core repaint gate so a layout refusal cannot make two blank captures pass as equivalent. Core PR #57 CI passed; full Release CTest was 33/33, standalone installed consumer 1/1, and the paired Runtime desktop gate matched 240 frames across 12 flows. Cropped CTest was 31/32; its sole shared-inline assertion failure reproduced on unmodified `d9cdb239` with the same profile.
- The latest physical feedback reports text disappearing after navigation and a blurry/clipped home clock. The exact flashed ELF, selected text backend/profile, and route-specific failure are still unrecorded. The only recent local WS147 image found is ELF `43168E59…A386CB9` / BIN `744538B6…A89AC06`, built around 05:00 on 2026-11-14 against Core 0.8.0 / ABI3; it predates the later routed-text, font-catalog, and paint-only cache fixes. This image cannot establish whether the latest observation contains those fixes. Treat the device symptom as unresolved until the flashed identity is verified and Home/Activity/Weather are tested separately. The clock blur/crop remains a distinct font metrics/line-height concern.
- The WS147 Core font catalog now includes a compact 34 px bold numeric face. Its hhea ascent plus descent is 49.2 px at 34 px, while the band-shell clock previously had a 36 px line box; the weather hero relied on a smaller default line box. The candidate CSS now gives both large-text lines 50 px. Core-font desktop captures and browser captures at 172x320 show complete clock/weather glyphs and no home-screen overflow. Runtime PR #80 merged as `dbe4ca05`; all 17 CI checks passed. It fixes the routed-text cache generation check and 34 px line height, and removes a hardcoded three-Core-font limit that rejected the expanded catalog. The rebuilt ESP-IDF v5.3.1 staged CoreText band-shell image is 7,399,152 bytes with 989,456 bytes (12%) free in the 8 MiB app partition; ELF SHA-256 `058CD0C45CAFD9BAEA7C6EC6C71F985F3B13C7BBDEF9414F7B18BE81F15FACC1`, BIN SHA-256 `EBAC43BDB563FB9462E1BEBF55A7C1D78C178706F103AB94909DF709F48B06F9`. It was flashed to WS147 on COM19; boot logs identify the expected board and `/band_shell.html`, load the font pack, and report `first_present_ok=1`. User visual/route acceptance is pending. This staged image still uses the Runtime's signed ABI3 dependency; it is not ABI4 device qualification.
- Desktop PR #79 (`45c6677d`) is merged. It adds a Core text repaint-equivalence case for `letter-spacing`, checks the selected text backend and visible mutation, and compares incremental captures byte-for-byte with forced full repaint. The focused local repaint, shell CLI, and app-shell workflow tests pass. This gate covers that mutation case; broader supported-property coverage is still required before changing the desktop default.
- Runtime PR #83 fixes the Runtime paint-only image invalidation path. Core PR #57 fixes the corresponding global-generation invalidation in standalone Render Core; Runtime PR #85 adds a positive text-present assertion and deterministic input-mutation repaint coverage. Both fixes have passed their PR CI. These host results do not identify or accept a WS147 firmware image.
- On the production branch, complete-resource Core text remains opt-in. WS147 production defaults still use the callback/v6 route; the named Core-text profile is explicit. Desktop still uses its GDI default, with Core available only through opt-in validation. `TextMeasureProvider` and `TextPainter` remain public Core APIs because production consumers still use them; only the unused `HostTextAdapter` convenience layer has been removed.
- No ABI4 named-device-profile provenance or ABI4 firmware acceptance is recorded. The flashed CoreText image exercises a staged ABI3 dependency profile; it is useful integration evidence but does not satisfy ABI4 rollout.
- Core PRs #38–47 have since merged additional shared-inline decoration, mixed-font ownership, preserved-whitespace, generated-text, and list-marker slices. These are delivered bounded capabilities; their documented unsupported shaping, general Unicode, positive blur, and folded-layer boundaries remain separate scope and are not implied to be browser-complete.
- The accepted F6 text-style fast path is present on Core master. Its paired host measurement recorded a 17.939% reduction on a text-heavy fixture (95% CI 17.267%–18.610%); the all-element control interval crossed zero. Treat this as workload-specific evidence, not an MCU or whole-app guarantee. Do not re-port the old candidate branch.

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
4. **Host cache repair is merged; physical acceptance is unresolved:** Runtime PR #83, Core PR #57, and Runtime PR #85 are merged with CI passing. The prior device report is not bound to the flashed ELF/backend/profile, and the only local image located predates the relevant fixes. Build a fresh named WS147 CoreText candidate with embedded identity, capture its ELF/BIN hashes and startup telemetry, then verify Home, Activity, and Weather separately. Do not change the normal WS147 Kconfig default until that candidate passes physical acceptance.
5. **Next parallel slices:** complete the wider supported-property desktop Core-text repaint gate; verify the already-built natural-inline candidate's exact identity and prepare its human acceptance; audit remaining Core callback consumers against actual production defaults. Use isolated worktrees and exact Core/Runtime source identities. Do not remove `TextMeasureProvider`/`TextPainter` while GDI or callback/v6 production paths still depend on them.
6. After desktop/device consumer acceptance, coordinate callback API removal, final Core ABI4 release/archive, Runtime package-lock/default adoption, and named ABI4 device qualification. ABI4 full/cropped CI proves installed-consumer compatibility only; it is not a signed release or hardware acceptance.
