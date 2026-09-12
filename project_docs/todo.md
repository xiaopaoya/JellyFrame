# JellyFrame Active TODO

> Last updated: 2026-09-11; Applies to: 0.6.0-dev
>
> This is the near-term execution queue for the [active roadmap](roadmap.md). It does not repeat closed acceptance, performance micro-experiments or historical port work.

## Now: Wider A2 Product Exit and B1 Boundary Maintenance

Execution order is fixed: triage confirmed security/correctness findings first,
then complete the performance-observability loop, then close device exits. Neither
performance tooling nor editor work substitutes for A2 hardware evidence.

### 1. Review finding governance (highest priority)

- [ ] Re-read and classify every finding from both review rounds in
  `review_findings_INDEX.md`, `render_core_code_review.md`,
  `review_findings_render_core_rest.md`, `review_findings_app_runtime_services.md`,
  `review_findings_app_runtime_lifecycle.md` and
  `review_findings_app_runtime_script_task.md` as fixed, disproven, deferred by RFC
  or requiring hardware evidence. Do not infer validity from report headings.
- [ ] Close confirmed P0/P1 items first: privileged-service authorization,
  resource/handle lifetime, dirty/layout budgets, viewport units, text measurement
  and cross-task completion semantics. Each fix needs positive, refusal/fault and
  repeated-lifecycle coverage.
- [ ] Re-read the new performance review's P1/P2 items before changing code, to
  avoid reopening already-closed work. Modify hot paths only with a stable workload
  or a demonstrated correctness benefit.
- [ ] Update the review index and project status with evidence, commits, tests and
  residual risk for each finding.

### 2. Render performance-observability loop

- [x] Reconcile the current Render Trace/Performance Profile status: desktop per-frame
  trace, stage/command/owner attribution and device aggregate profile are delivered;
  per-element device trace is not implemented and aggregate data is never element timing.
- [ ] Run profile OFF/ON A/B with the same App, input, build and machine; verify
  pixel hashes, p50/p95, profiling overhead and trace-I/O boundaries. Drag/scroll
  remains the current interaction-performance fixture.
- [x] The VS Code Render Trace viewer provides stage proportions, dirty overlay,
  slowest-frame navigation, correct per-frame Top-64 command ranking, missing
  capture/stage/command states and explicit `unattributed`/truncation handling.
- [x] Deliver cross-frame command aggregation in the Render Trace viewer; a dedicated
  in-VS-Code Device Performance Profile viewer remains to be evaluated. Keep static
  reports separate from frame traces and never present aggregate device telemetry as
  per-element data.
- [ ] Run Device Profile windows on one static and one continuous drag/scroll
  developer-image workload; hardware claims must come from versioned reports.
- [ ] Only after those measurements stabilize, compare aligned workloads with Cairo/
  SDL software rendering and LVGL. Do not claim a general “faster/slower” result
  beforehand.

### 3. Short-term interaction performance and device exit

- [ ] Complete the [WS147 Panel Scroll synchronization and port acceptance](../docs/ws147_panel_scroll_te_acceptance.md): confirm actual JD9853 TE capability, fix VSCSAD/strip ordering, and verify reproducible input-to-DMA-complete p50/p95 on the same firmware/profile. The current auto-scroll profile proves render-to-present only, not gesture latency. Correlate each input sample, dispatch, mutation/frame, paint end and DMA completion; claim input-to-present only with TE/latch or optical evidence.

- [ ] Complete the clean-author-machine, read-only VS Code smoke on the WS147 image: discovery, identity and installed-App list must match the manifest/registry. Use `ws147_provider_vscode_smoke_20260825_zh.md`; this item does not install or flash anything. The earlier local candidate smoke is not a substitute.
- [ ] Complete the VS Code device workflow on the same clean author machine: `new -> check -> package -> deploy -> launch -> live log -> update -> rollback -> stop -> remove`. Keep desktop and device sessions distinct and preserve actionable ownership in the final report.
- [ ] Run real installed-App panel/input acceptance through the provider workflow. A 2026-09-08 user observation reports normal physical interaction response, but the archived `posted=0` sample is not structured input evidence. Record per-App launch marker, touch/input response, panel/present errors and recovery behavior; do not treat provider lifecycle PASS or an informal observation as complete visual/input evidence.
- [x] WS147 physical-GRAM panel-scroll performance and fallback retest completed, but visual acceptance failed; commit `eaa52a67` added an acceptance gate and removed it from the default-usable path. Investigate TE/vblank synchronization only; do not re-enable release configuration.
- [ ] Maintain B1 as a release gate. The signed Core `v0.6.2` release is the current Runtime dependency; any Core bump must download or otherwise authenticate the reviewed release artifact, verify its archive SHA-256, update the exact version/ABI/source lock and pass standalone, package-consumer and source-override tests.
- [x] Build and accept the WS147 Developer Image `0.6.2-ws147.1` from the merged Runtime Core `0.6.2` lock. Keep the historical `0.6.1` manifest and evidence immutable; the complete R1-R17, host/provider and package-smoke report is `core062-developer-image-final-20260905`.
- [x] Publish the App Author SDK `app-sdk-v0.6.0-dev.2` from Runtime `ca747011`; standard and scripting desktop runtimes consume Core `0.6.2`, and the release archive SHA-256 is `c3245edd...7dc8a9f`.
- [ ] Execute [the 0.6 engineering review plan](engineering_review_plan_20260819_zh.md): begin with R0 package/profile/provenance, then R1 document/style, layout/dirty and renderer/text. Change an interface only for a demonstrated semantic or safety defect, never as a mechanical rename.
- [ ] Continue the R1 Core-only audit after the responsive-layout foundation delivery: review parser/style ownership, malformed-input budgets, cache invalidation and deterministic capture behavior. The `300x300`, `320x240` and `172x320` matrix and Flex cross-axis regressions are now maintained as gates; prioritize demonstrated semantic defects over new controls or browser-only declarations.
- [x] Close the focused 2026-09-07 review findings: Flex sizing skips an unchanged probe layout, generic workers preserve nonzero `client_token`, worker pipeline rebuilds restore autofocus state without replaying `focus`, and worker layout uses host budgets. Debug, scripting MinSizeRel and ASan/UBSan regressions pass; the review probe confirms linear `anywhere` measurement on the current baseline.
- [x] Close the follow-up callback/service findings: timer cleanup is deferred during a timer pump so callbacks added by a callback cannot reorder the current batch; rAF callbacks remain cancellable until their turn and are reclaimed after the batch; queued XHR cancellation drops provider-owned fixture copies while worker-owned requests retain late-completion cleanup. Debug and scripting MinSizeRel focused tests pass. The `client_token` finding is already covered above and is not counted twice.
- [x] Close the 2026-09-07 performance/behavior follow-up: animation overrides are indexed by node; the built-in font path reuses scalar letter-spacing measurements; providers that explicitly guarantee additive measurement use incremental wrap widths; CSS class candidate indexing uses stable `string_view` keys; one failing rAF callback no longer drops later callbacks in the same frame. Unknown font providers retain whole-string measurement semantics, and ownership-transfer `std::move` calls remain intentional.
- [x] Complete the 2026-09-10 low-risk maintenance batch: layout-owned text normalization/wrap results are reused by layer generation; rounded-clip and built-in bitmap fallback hot paths skip redundant work; dirty-rect coalescing uses bounded generation-aware candidates; flex layout/paint ordering shares one helper; transformed raster and opacity paths avoid repeated invariant work and command string copies; render-trace capture lookup snapshots its directory once. Release, scripting and tool regressions pass.
- [ ] Maintain the B2 script-runtime boundary: common hosts and worker code may include only `script_runtime.h`; engine headers, values and discovery stay in the selected backend. Do not introduce a second backend or alter public developer guidance without a separately approved RFC and parity evidence.

## In Parallel: A3 Trial Preparation

Status: **in progress, with outreach and preparation coordination owned by external collaborators.**

- [ ] Prepare the smallest trial kit: released Developer Image/provider, VS Code extension installation instructions, the `blank` template start flow, known capability boundaries and a support channel.
- [ ] Fix the feedback archive format: App `.jfapp` or source package, image/provider/extension versions, reproduction steps, JellyFrame Output, device logs and a minimal reproducible capture where possible.
- [ ] Prepare first-round triage: installation, execution, recovery, data corruption and documented-capability failures are P0; unclaimed Canvas, full-screen 30 FPS or full browser APIs are not implied defects.
- [ ] Complete Stage 1 of the [Visual App Editor plan](visual_app_editor_plan.md), then add only the Stage 2/3 source-conflict and real-shell handoff slices needed before deciding whether it is included in trial promotion.

Preparation does not open the external trial. Trial access and product-usability data collection begin only after the two A2 formal evidence items and installed-App panel/input acceptance close.

## Core Post-Release Candidate

New Render Core capability work starts only on the independently governed Core release line, or in the same approved extraction release window. Each candidate requires a reproducible author need, an RFC, positive/negative behavior tests, three-target desktop capture, capability-matrix/diagnostic/recipe updates and a hot-path benchmark.

- [ ] Re-evaluate and finish `text-wrap: balance` candidate evidence on the independent Core line; the current `0.6.2-dev` head does not advertise it. Do not add it to the Runtime author matrix until the evidence is complete and Runtime explicitly chooses its package/default-provider integration and lock update.
- [ ] Choose the next Core candidate only after a reproducible author need and an RFC establish the feature, profile impact and hardware budget. Do not reopen broad CSS compatibility work by default.

Core work intervenes only for a required platform-neutral contract. A reference endpoint never substitutes for hardware evidence.

## Explicitly Out of the Queue

- [ ] Do not continue full-frame rounded/gradient copy, span or DMA micro-optimisation. Only telemetry from a real developer-image workload can reopen it.
- [ ] Do not enable retained replay, framebuffer reuse or a tile/scanline renderer without their separate evidence gates.
- [ ] Do not make Canvas, full SVG/video, Shadow DOM, Worker, iframe, `:has()` or container queries default `0.6` scope.
- [ ] Do not begin an external hardware developer trial before the wider A2 clean-machine and panel/input exits pass.

## Minimum Check Per Item

- [ ] `git diff --check` plus relevant Debug/Release CTest.
- [ ] Focused regression for scripting, tools, packages or profiles.
- [ ] Focused benchmark for hot paths; versioned port report for hardware claims.
