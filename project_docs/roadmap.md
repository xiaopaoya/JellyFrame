# JellyFrame Active Roadmap

> Last updated: 2026-08-25; Applies to: 0.6.0-dev; this is the single active plan.

## Governing Decision

`0.5.0` is released. The completed desktop `trial`, registry and Win32-shell evidence proves a **desktop author workflow**, not a viable external hardware trial. The previous plan implicitly required an app author to prepare a board, build ESP-IDF, port the project, create an OS layer and flash/debug firmware. That is not a reasonable prerequisite.

An external trial now requires a first-party developer image and VS Code/CLI workflow. App authors must be able to check, install, update, launch, recover and inspect an app without installing ESP-IDF or changing firmware. Desktop tools remain preflight and development tools; they are no longer an external-trial admission gate by themselves.

Only unfinished work appears below. Completed work belongs in changelogs, tests and archived evidence. A phase must meet its exit criteria; later optimisation or feature work cannot substitute for them.

## Current Baseline

- The history-preserving `xiaopaoya/JellyFrame-Render-Core` repository owns the physical Core branch. The first signed `v0.6.0` release is historical; Runtime `0.6.0-dev` now locks Core `v0.6.1`, ABI `1` and source identity `105d0166...b797c52b`. CI downloads that release artifact, verifies archive SHA-256 `f9d24aca...e18c7`, installs it and runs the Runtime package-consumer tests. The in-tree provider remains only for synchronized local development. For Core ABI `1`, installed `render_core/` headers are the C++ consumer surface; there is no hidden header tier or C ABI. On 2026-08-19, Core-only/Device-contract CMake boundary coverage was added: Core-only cannot create contracts targets/tests, contracts-only remains independently buildable, and the archive/install/package/source-override loop was rechecked.
- App Runtime has `.jfapp` lifecycle, registry reference semantics, an optional selected script backend and the script-worker session/generation/epoch, value-only frame/input/service/fatal protocol. WS147 P3 worker, service, recovery and mixed-soak evidence is closed.
- WS147 value-frame-v2 dirty/recovery passes. The full-screen rounded-gradient workload is not 30 FPS. Canvas has no real host binding and remains `not-tested`.
- JFDP/1 framing, capability, typed status/progress payloads and staged-install contracts have the isolated `device_runtime_contracts` source owner. WS147 native USB Serial/JTAG wire, A1-2 persistent lifecycle acceptance and the provider handoff are closed. The `provider-handoff-afdcf75-20260821` report passes same-image Identity matching, real in-flight cancellation, durable update/rollback/remove and 30 mixed cycles; the versioned `jellyframe-device@0.1.1-dev` provider is delivered and declares the lifecycle UI capabilities. `0.1.0-dev` remains only as the read-only `discover/info/list` baseline. The Developer Image baseline has a strict manifest and hash-verified factory recovery image; this still does not prove the clean-machine VS Code product workflow or installed-App panel/input behavior.

## Closed Performance Stage

**O1: value-frame-v2 baseline and low-risk raster optimisation is closed.**

The constrained WS147 dirty/recovery workload passes but authorises neither retained replay, framebuffer reuse nor a general 30 FPS promise. Full-frame rounded-gradient attribution reached its shadow/coverage/composite boundary; copy/span micro-optimisations no longer produce meaningful gains. Packed RGB565 and double-DMA A/B results are not defaults.

Future performance work needs a real official-image workload and phase telemetry. Tile/scanline work first needs evidence that framebuffer memory or bandwidth is dominant.

## Track A: Official Board and Device OS

### A1: First Developer Image

Target: ESP32-S3 Waveshare Touch LCD 1.47, `rect-172x320`.

The storage/recovery and image-identity slices are closed: protected launcher/fallback, staged app storage, persistent registry, one real USB JFDP/1 transport and its fault matrix, the WS147 measured manifest and hash-verified factory recovery procedure are integrated. The port reports only measured profiles and bound features.

Exit: a clean machine can flash once using documented tooling and then repeatedly install, update, rollback, remove, recover a bad app and reconnect without reflashing, reset, watchdog or registry corruption. The WS147 port handoff now meets the physical lifecycle portion; A1-2 alone does not replace A2's actual app-render and author-tool workflow.

### A2: Author Tools

CLI and VS Code select a real device and expose profile, storage, capabilities, lifecycle, install/update/launch/stop/remove and app-scoped logs. Desktop and device debug sessions remain separate. Device telemetry is the only source of device performance data.

The WS147 provider handoff sub-gate is closed by `provider-handoff-afdcf75-20260821`: identity, in-flight cancellation,
durable lifecycle and 30 mixed cycles pass on one published image. The versioned `0.1.1-dev` provider,
capability-gated lifecycle UI and local candidate smoke are complete. The wider author-tool gate has two remaining
formal evidence items: a clean machine must complete `new -> check -> device install -> live log -> update -> rollback
-> stop -> remove` from VS Code, and a real installed App must provide panel/input evidence with actionable failure
ownership across package, transport, registry, Runtime and port.

### A3: Controlled External Trial

Begins only after A1/A2. Participants require no ESP-IDF. Reproducible package, image version and device log are required for feedback; only package, lifecycle, recovery or documented-capability blockers become P0.

Preparation status: **in progress**. Outreach coordination, trial instructions and feedback collection may be prepared
in parallel, but no trial access, non-release image flashing or product-usability conclusions are allowed before the A2
clean-machine and panel/input exits pass.

## Track B: Independent Engine Projects

### B0: Freeze Extraction Policy

Define release, ABI, feature profile, archive, signature, dependency-lock and local-override policy. Create a filtered-history export for `jellyframe-render-core`; do not replace it with a history-free copy. Validate standalone Core, pinned Runtime package, Runtime local override and Device OS profile consumers.

Exit: all consumers use Core only through public packages/headers; Runtime/port private includes never re-enter Core.

### B1: Physical Split and Versioning (Core/Runtime boundary closed)

| Project | Initial line | Dependency rule |
| --- | --- | --- |
| `jellyframe-render-core` | `0.6.1`, Core ABI `1` | Runtime pins exact version, ABI and source identity; release metadata records the signed archive SHA-256 |
| `jellyframe` | `0.6.0` | Bumps Core only in an explicit dependency change |
| `jellyframe-device-os` | `0.1.0-dev` | Pins JellyFrame release and board feature profile |
| JFDP | `JFDP/1` | Breaking wire changes require a major version |

A Core release contains sources, headers, CMake package, feature registry/profile schema and provenance. It is not one fixed full-feature firmware: Device OS selects a build-time profile, and app feature negotiation rejects unavailable features. `.jfapp` files never ship native modules.

Physical Core repository migration is complete. The first independently released Core is signed, reproducible and consumed by a locked Runtime build; the Core/Runtime B1 exit is closed. The future Device OS must consume the same provenance contract before its own physical migration. Device OS, ports and launcher then migrate together, not piecemeal.

### B2: Script Runtime Backend Boundary

The Runtime owns the documented JavaScript/DOM/service semantics while a selected
script backend owns its realm, wrappers, callbacks and native values.
`ScriptRuntime` is the framework-facing contract; its factory is selected at
configure time through `JELLYFRAME_SCRIPT_ENGINE`. The current implementation is
JerryScript, but it is no longer a public dependency of worker or desktop-host
code. Engine discovery and linkage remain backend-private.

This is not a runtime plugin system. A build contains exactly one backend; apps
cannot request or bundle an engine, and hot callback/value paths must not pass
through a generic conversion layer. A later backend candidate needs an explicit
compatibility/resource RFC, a native implementation of the complete contract,
behavior/watchdog/ownership/recovery parity, desktop benchmark comparison and
target-specific acceptance before any default changes. External developer
material must not imply a preferred future backend before that evidence exists.

## Track C: Render Core Capability Evolution

Capability growth does not compete with Device OS but must stop accumulating at a temporary monorepo boundary. Freeze B0 first, then implement capability packs as independent Core releases; perform the physical move before, or in the same release window as, the first high-value capability pack.

### C1: 0.6 Closure Pack Evidence (desktop closed)

The following low-cost/high-value slices are implemented and desktop-evidence
closed: the fresh Core-only 2026-08-15 build passed the full Core regression,
three `jelly_controls` captures (`300x300`, `320x240`, `172x320`) and the
logical/`hsl()` plus flex-order microbenchmarks. This is desktop evidence, not a
hardware performance claim:

1. Logical size/spacing physical mapping for LTR horizontal writing mode.
2. Text layout: long-word wrapping, ellipsis consistency and cross-backend validation of implemented letter spacing.
3. Common flex/grid placement: `order`, `align-self`, `place-*`, limited `grid-template-rows` and areas.
4. `hsl()` plus common background size/position/repeat.

Any additional C1 candidate needs positive/negative tests, three-target desktop
capture, capability matrix, diagnostics, recipe and hot-path benchmark.

### C1.1: Independent Core Candidate (release-gated)

The independently governed Core line additionally contains bounded
`text-wrap: balance` for short naturally wrapped text. Its standalone build,
unit, install and deterministic-archive CI are green. It is not yet an
author-facing Runtime capability while normal Runtime builds use the
synchronized in-tree provider. Before it can move into the Runtime matrix,
complete the candidate evidence required above, make an explicit Runtime
dependency/default-provider decision, update the lock where needed and run the
package-consumer regression.

### C2: Deferred Candidates

Container queries, `oklch()`, complex grid, `:has()`, filters, Shadow DOM, iframe, Worker, full SVG/video and complex shaping require a reproducible trial need and an RFC with hardware budget; they are not promised work.

## Version Exits

- `0.6`: A0 contracts, the closed Core/Runtime B1 boundary and proven C1 packs. Canvas and full-screen 30 FPS are not release gates.
- `0.7`: A1/A2 author-tool delivery for the first developer image and the actual start of the controlled external trial.
  Core/Runtime B1 was completed in `0.6` and is no longer a `0.7` exit.
- `1.0`: one stable official board, reproducible Device OS lifecycle, frozen Japp/manifest/diagnostic/profile contracts, supported-port matrix and a dependency/security update policy. Full-browser compatibility is not a 1.0 goal.

## Change Gate

Every change needs `git diff --check` and relevant Debug/Release CTest. Script, tool, package and profile changes need focused regression. Performance claims record workload, target/profile, p50/p95, memory watermarks and failures. Hardware claims require a versioned port report.
