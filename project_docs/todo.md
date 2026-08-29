# JellyFrame Active TODO

> Last updated: 2026-08-25; Applies to: 0.6.0-dev
>
> This is the near-term execution queue for the [active roadmap](roadmap.md). It does not repeat closed acceptance, performance micro-experiments or historical port work.

## Now: Wider A2 Product Exit and B1 Boundary Maintenance

- [ ] Complete the clean-author-machine, read-only VS Code smoke on the WS147 image: discovery, identity and installed-App list must match the manifest/registry. Use `../docs/ws147_provider_vscode_smoke_20260825_zh.md`; this item does not install or flash anything. The earlier local candidate smoke is not a substitute.
- [ ] Complete the VS Code device workflow on the same clean author machine: `new -> check -> package -> deploy -> launch -> live log -> update -> rollback -> stop -> remove`. Keep desktop and device sessions distinct and preserve actionable ownership in the final report.
- [ ] Run real installed-App panel/input acceptance through the provider workflow. Record the app launch marker, touch/input response, panel/present errors and recovery behavior; do not treat provider lifecycle PASS as visual or input evidence.
- [ ] Maintain B1 as a release gate. The first signed Core `v0.6.0` is historical and Runtime currently locks `v0.6.1`; any Core bump must download or otherwise authenticate the reviewed release artifact, verify its archive SHA-256, update the exact version/ABI/source lock and pass standalone, package-consumer and source-override tests.
- [ ] Execute [the 0.6 engineering review plan](engineering_review_plan_20260819_zh.md): begin with R0 package/profile/provenance, then R1 document/style, layout/dirty and renderer/text. Change an interface only for a demonstrated semantic or safety defect, never as a mechanical rename.
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

- [ ] Finish `text-wrap: balance` candidate evidence on the independent Core line. Do not add it to the Runtime author matrix until the evidence is complete and Runtime explicitly chooses its package/default-provider integration.
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
