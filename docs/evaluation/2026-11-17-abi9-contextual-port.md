# ABI9 contextual ESP32 installed-port checkpoint

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Status and scope

The ESP32 installed resource owner, session/frame admission and worker/UI
resource handoff now select contextual frame12 on the matching Core ABI9
candidate. The three previously configured port skips now execute positive
or feature-OFF refusal tests. An actual ESP-IDF5.3.1 installed-worker target
build also succeeds and naturally retains the contextual production route.
This is local host/compile qualification, not signed physical installation,
native/default visual parity, product enablement or a published package.

This supersedes the port-skips and target-build open items in
[the preceding Runtime checkpoint](2026-11-17-abi9-contextual-runtime.md).
Its dated evidence remains unchanged. Active Runtime worktree:
`E:/Workspaces/Runtime-contextual-abi9-20261117`, branch
`codex/runtime-contextual-abi9-20261117`, parent checkpoint `53fda6fa`.
Core remains unchanged at `f35f1f778119f360d508b69b0c7a0eaeafae6cb4`,
candidate0.6.2/ABI9, source SHA-256
`997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61`
(640 files). Receipts and exact final identities are under
`E:/Experiments/migration-runtime-contextual-port-20261117`.

## Changes

Installed text admission is based on the actual selected canonical or
contextual API. A contextual declaration requires a valid owned Binding and
frame12 codec; it does not require scalar frame10. ABI9 refuses scalar
downgrade when CoreText is selected. Unsupported declarations, missing
resources and unavailable images refuse explicitly. Worker, supervisor and
UI retain the same selected owner; accepted replay binds its own resources
even if the renderer is given stale UI options.

The scalar font catalog remains an immutable resource-identity/lifetime
holder. It is not used to paint or measure contextual text. The previous
ABI4 image adapter remains behind its actual canonical capability;
enabling BMP configuration alone cannot make it available on ABI9.
Font-registration counts are bounded before indexing/reserve, and resource
factory allocation/length failures preserve the previous output handle.

The IDF Core component now exports selected optional feature macros to its
consumers. Omitting them allowed a header default to change feature selection
midway through a translation unit. Main's Core version/ABI identity now comes
from the same external Core tree as the component. Native layout/compositor
constructors use actual API capability rather than assuming ABI>=4 means the
ABI4 canonical constructor is present.

The new refusal tests exposed a shared shaped-rendering omission: its atomic
full-frame staging surface did not honor `max_temporary_pixels`. The renderer
now checks the complete staging pixel count, target storage shape and a
nonzero limit before copying or writing. A small dirty rectangle still needs
the full staging budget. Zero retains the existing unlimited convention.
Tests cover below-limit refusal, exact-limit success and invalid target
storage, with destination preservation.

## Verification

| Configuration | Passed | Skipped | Failed | Scope |
|---|---:|---:|---:|---|
| ABI9 source ON | 35 | 0 | 0 | Full configured Runtime CTest suite |
| ABI9 source cropped | 35 | 0 | 0 | Same suite, five optional features OFF |
| ABI9 source contextual OFF | 35 | 0 | 0 | Positive resource retention and route refusal |
| ABI4 shaped compatibility | 36 | 0 | 0 | Previous matching source candidate |
| ABI9 installed ON | 11 | 0 | 0 | Four prior consumers plus seven ESP port tests |
| ABI9 installed cropped | 11 | 0 | 0 | Same scoped public installed consumers |

The configured source suite omits Core tests, examples, benchmarks and sample
regression tests. Cropped disables Canvas2D, modern paint, flex/grid, advanced
forms and length math. The installed gate is deliberately scoped; it is not
the complete installed candidate-manifest/package suite. The disposable
snapshot alone carries the exact candidate lock. Production samples and
production lock remain unchanged.

The new contextual port tests execute full fixture Binding/session/worker
production, actual JS text mutation, accepted transport and typed painting.
They check changed packets/pixels, late malformed-packet atomic refusal,
foreign raw-frame owner refusal, accepted-frame owner precedence, unavailable
image refusal with BMP both OFF/ON, bounded staging and retained replay.
Before retained replay, worker options as well as supervisor/resource/UI
owners are drained; after both accepted frames release, the weak Binding
expires. This avoids accidental test-local ownership masking a lifetime bug.

The IDF build uses CoreText, scripting/worker, contextual text/layout/JFSP2,
PSRAM allocation and the opt-in6MiB store profile, with BMP OFF. Macro audit
confirms contextual APIs and frame admission enabled, canonical text API
disabled, ABI9 identity. ELF symbols naturally retain Binding construction,
Paragraph layout, contextual DOM layout/layer capture, typed paint, resource
factory and installed task launch; no forced linker roots are used.
Exact binary/ELF/config/partition hashes are in `target-result.json` and
the final source/commit receipt. This profile changes partition boundaries;
the build does not authorize an app-only deployment.

GCC still reports existing Core misleading-indentation warnings. The Core
component keeps those diagnostics visible but exempts that warning category
from IDF's blanket Werror. Those Core statements were not restyled and the
frozen Core source identity was not changed. This is a build limitation, not
a claim that the warnings were repaired.

Reproduction/evidence: `qualify-source-final.py`, `source-result.json`,
`sync-installed-final.py`, `qualify-installed.py`, `installed-result.json`,
`build-target.ps1`, `target-build-verified.log`, `audit-target.py`,
`target-result.json`, final source/installed-linkage audits and commit receipt.
Initial failed budget tests are retained separately from verified results.
No serial enumeration, flashing or hardware test occurred.

## Remaining gates

1. Add explicit contextual capability/route identity to JFDP, provider,
   package inspector and release manifests. Current physical provider still
   negotiates the older `text.shaping` bit; it must not pretend that this
   identifies `text.contextual-jfsp2`/`text.contextual-layout` or frame12.
2. Qualify installed OFF and complete candidate-manifest full/cropped suites.
   Resolve family policy and broader native/Desktop/default parity. The
   legacy native smoke's prepared-line metric does not inspect ABI9's opaque
   snapshot and is not an ABI9 acceptance gate.
3. Requalify the actual signed store/resource/worker pipeline and Runtime
   allocation/refusal/recovery, heap reserve and latency on ABI9. Earlier
   ABI4 host/OOM results are not evidence for these ABI9 gates.
4. Qualify matched signed physical install/reboot/tamper/fallback and exact
   device behavior with reviewed test trust/profile/provisioning.
5. Review matching packages/PRs, hosted CI/publication and enablement.

Production defaults, dependency lock and keys remain unchanged. This checkpoint
is local only. Previously accepted ABI4 Home/Activity/Weather/generation200
remains closed. Full experimental migration remains incomplete.
