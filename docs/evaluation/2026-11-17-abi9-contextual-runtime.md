# ABI9 contextual Runtime integration checkpoint

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Status and identity

The local Runtime candidate now consumes Core ABI9 contextual layout and typed
paint through an explicit frame-v12 route. It preserves the previous ABI4
frame-v11 route. This is a host-qualified checkpoint, not a product enablement,
published package, target build or device acceptance.

- Runtime branch: `codex/runtime-contextual-abi9-20261117`, based on `c0fe1cb3`.
- Core branch: `codex/shaped-text-abi9-20261117`, commit
  `f35f1f778119f360d508b69b0c7a0eaeafae6cb4`.
- Core package candidate: version `0.6.2`, ABI9, 640 source files, source SHA-256
  `997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61`.
- Evidence: `E:/Experiments/migration-runtime-contextual-abi9-20261117`.

The isolated Runtime worktree inherited 117 changed files from
`D:/CodexTemp/Runtime-shaping-package-v1-20261116`, including the earlier signed
store/provider and allocation-recovery work. Their original hashes and patch
are retained in the evidence directory. Final audit rechecks every original
file against that manifest; the predecessor worktree was not edited. Earlier
ABI4 evaluation documents describe their dated candidate and retain their
original qualification scope.

Production dependency lock, defaults and keys remain unchanged. Only the
disposable installed-qualification snapshot selects the exact ABI9 package.
No serial enumeration, flashing or hardware tests occurred in this slice.

## Behavior

`RuntimeShapedTextBinding` selects contextual public APIs on ABI9 and the
previous shaped APIs on ABI4. ABI9 uses frame12 and manifest kind
`jfsp2-block-v12`; ABI4 retains frame11 and `jfsp1-block-v11`. The historical
ABI4 manifest label does **not** mean packet version1: the qualified ABI4 Core
candidate also emits JFSP2. Explicit frame version, selected capability,
authenticated resources and exact Binding identity select the route. Cross
protocol packages and mismatched packet headers refuse before rendering.

The contextual worker clears scalar text measurement before constructing its
LayoutEngine. It uses Core's immutable contextual LayoutResult, final-line
packets, typed layer capture and background insertion positions. A separate
owned root provides ordinary hit testing without violating the captured
root's null-box lifetime contract. JS mutations and pointer clicks produce
new frames. Accepted frames retain the authenticated Binding after worker,
session and original package storage release.

Contextual geometry is never combined with scalar JFTP text painting or cache
entries. Frame9/10 are admitted only when their canonical API is present;
they are not enabled merely because ABI9 has other text APIs. Shaped raw-frame
rendering validates the complete frame and stages all writes. Truncation,
identity mismatch, malformed final records, resource/work exhaustion and late
paint failures leave the destination unchanged.

The admitted contextual scope remains nested ordinary static blocks, one
direct text child per host, one selected family/weight, auto heights and solid
backgrounds in a plain root. Rounded backgrounds, transforms, clips, effects,
native controls, images, general inline content and font fallback are not
added to v12. Fractional block widths refuse; fractional font sizes and paint
positions have their own coverage. Existing Core public holder layouts remain
unchanged; adding fields requires a coordinated ABI10.

Font registration assigns shared named fields and only sets scalar family
hashes on APIs that expose them. ABI9 scalar family-hash/generation parity is
not established by this adaptation; tests use registered numeric family IDs
where the contextual predecessor lacks those APIs.

## Verification

| Configuration | Passed | Skipped | Failed | Scope |
|---|---:|---:|---:|---|
| ABI9 source ON | 32 | 3 | 0 | Full configured Runtime CTest suite |
| ABI9 source cropped | 32 | 3 | 0 | Same suite, five optional features OFF |
| ABI9 source contextual OFF | 32 | 3 | 0 | Same suite, contextual text/layout/JFSP2 OFF |
| ABI4 shaped compatibility | 36 | 0 | 0 | Previous matching Core source candidate |
| ABI9 installed ON | 4 | 0 | 0 | App, script, worker/frame and device contracts |
| ABI9 installed cropped | 4 | 0 | 0 | Same public installed-consumer suites |

Cropped disables Canvas2D, modern paint, flex/grid, advanced forms and length
math. Source CMake configurations disable Core tests, examples, benchmarks and
sample regression tests; these counts do not represent those omitted suites.

The three configured skips are **open ESP32 port work**:
`installed_resource_owner`, `installed_core_text_0` and
`installed_core_text_1`. Their factory/admission paths remain gated on the
previous canonical text API. Passing generic frame-admission tests does not
qualify a contextual installed-device route.

The positive worker corpus was initially hidden inside an ABI4-only guard;
that guard has been corrected. Final verbose execution explicitly reports:
`shaped DOM worker v12: Core packet/pixel oracle, JS mutation, pointer input and retained lifetime passed`.
Tests compare actual DOM transport with independent Core final packets,
background order and complete framebuffer pixels. Mixed Latin/CJK/combining
source, real pointer down/up clicks, JS text/width updates, retained replay,
all encoded-frame truncation positions and raw-frame atomic rejection execute.

Installed tests consume actual exported archives/public headers, with exact
ABI9 version/source locks enforced. Contextual fixtures use public headers and
offline regular/medium/bold font assets; SHA-256 digests are generated at
configure time. No Core private source include is needed. Final Ninja audits
verify the three Core-dependent test executables link the exact exported
archive, do not recompile Core sources, and the four selected test suites keep
assertions active. Tested consumer code matches the active workspace except
the intentional candidate dependency lock.

An initial attempt at the entire installed Runtime suite exposed five
package/document/tool failures: unchanged production samples target Core0.9.0,
while this isolated exact candidate lock is0.6.2. Pre1.0 compatibility correctly
refuses that combination. Production manifests/locks were not weakened.
The installed result above deliberately covers four relevant consumer suites;
it is not a full installed product/package migration claim. Matching
candidate manifests and the complete installed suite remain pending.

The signed-package Python tests also pass5/5, covering scalar and both shaped
declarations. Earlier ABI4 signed-store execution/OOM and Core allocation
results retain their original ABI-specific scope; they do not qualify ABI9
Runtime allocation, physical installation or performance.

Reproduction/evidence files include `qualify-source-matrix.py`,
`qualify-installed-matrix.py`, `source-matrix-result.json`,
`installed-matrix-result.json`, `test-on-final.log`,
`test-on-positive-v12-final.log`, `test-abi4-final.log`,
`signed-package-tests.log`, `final-audit.py` and `final-audit-result.json`.
Final source/commit receipts are stored alongside these artifacts.

## Remaining gates, in order

1. Port ESP32 installed resource ownership, capability/admission and session
   selection to the explicit v12 route; replace the three skips with positive
   contextual tests and genuine OFF/refusal coverage.
2. Qualify matching installed OFF consumers, candidate package manifests and
   full/cropped complete installed suites. Resolve family policy and broader
   native/Desktop/default feature parity explicitly.
3. Run ABI9 Runtime allocation/refusal/recovery and actual workload heap/latency
   qualification; target-build the exact matched Core/Runtime/resources/config.
4. Qualify signed physical install/reboot/tamper/fallback and exact-image device
   behavior. Partition/profile changes require explicit data migration.
5. Publish matched reviewed packages, run hosted CI and reviewed PRs, then
   decide enablement. Full experimental migration remains incomplete.

Previously closed ABI4 Home/Activity/Weather/generation200 acceptance stays
closed; it is neither repeated nor reused as evidence for the new route.
