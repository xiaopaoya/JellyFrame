# JellyFrame Active TODO

> Updated: 2026-08-25; Applies to: `0.6.0-dev`
>
> This is the near-term execution queue for the [active roadmap](roadmap.md). It does not repeat closed acceptance, performance micro-experiments or historical port work.

## Now: Wider A2 Product Exit and B1 Boundary Maintenance

- [ ] Complete the clean-machine, read-only VS Code smoke on the published WS147 image: discovery, identity and installed-App list must match the manifest/registry. Use `../docs/ws147_provider_vscode_smoke_20260825_zh.md`; this item does not install or flash anything.
- [ ] Complete the clean-machine VS Code device workflow on the published WS147 image: package, deploy, launch, live logs, update, rollback, stop and remove. Keep desktop and device sessions distinct and preserve actionable ownership in the final report.
- [ ] Run real installed-App panel/input acceptance through the provider workflow. Record the app launch marker, touch/input response, panel/present errors and recovery behavior; do not treat provider lifecycle PASS as visual or input evidence.
- [ ] Maintain B1 as a release gate. The first signed Core `v0.6.0` is historical and Runtime currently locks `v0.6.1`; any Core bump must download or otherwise authenticate the reviewed release artifact, verify its archive SHA-256, update the exact version/ABI/source lock and pass standalone, package-consumer and source-override tests.
- [ ] Execute [the 0.6 engineering review plan](engineering_review_plan_20260819_zh.md): begin with R0 package/profile/provenance, then R1 document/style, layout/dirty and renderer/text. Change an interface only for a demonstrated semantic or safety defect, never as a mechanical rename.

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
