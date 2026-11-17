# Shaped Runtime against installed full and cropped Core packages

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Scope and result

Both local candidate profiles pass the seven focused installed/shaped Runtime
CTest executables and the complete 34-test Runtime suite. The seven are a
subset of the 34, not additional independent test suites. Core's own tests are
disabled in these consumer builds; their earlier source-build results are
separate evidence. These are local, unpublished packages, not hosted CI or
device execution. The original production dependency lock is unchanged.

Core input: `D:/CodexTemp/Core-shaped-paint-integration-20261117`. Runtime input:
the current bytes of `D:/CodexTemp/Runtime-shaping-package-v1-20261116`, including
tracked edits and nonignored candidate files. The source snapshot is recorded
in each experiment's `runtime-inputs.json`. Core's exported manifest has 185
files with normalized source hash:

`7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a`.

| Candidate | Exported profile | Library SHA-256 | Focused | Complete |
| --- | --- | --- | --- | --- |
| full | render-core-default | `2286f36ef6731ef501ea99151a6d4b311665ae74cc84996c0e3bb7c94c3ab9a0` | 7/7 | 34/34 |
| cropped | render-core-minimal-no-forms-advanced | `1b6bb29cfe056b4841f2660d2547b87c31336738c8be272fcf2e8fe73f59bd69` | 7/7 | 34/34 |

Both enable text shaping. Cropped disables canvas2d, modern paint, flex/grid
and advanced forms. Runtime keeps scripting and the actual script-task worker
enabled, using the existing JerryScript libraries. CTest has no skipped tests.
Some legacy v5-v8 positive transport checks inside an executable report their
unavailable prerequisites on ABI4; these are not new shaped-v11 coverage.

## Isolation and proof against silent fallback

`project_tools/qualify_shaped_installed_packages.py` creates a fresh Core build,
installs its package, copies Runtime source bytes, and changes only the copy's
dependency lock to the exact candidate identity. Normal package-lock checks
remain enabled. The exported package keeps its local 0.9.0/ABI4 version; this
does not replace or republish the existing 0.9.0 release.

Runtime provenance reports `provider=package`, `lockEnforced=true`, matching
version/ABI/source identity, and an empty source-override directory. The
production lock SHA-256 before and after qualification is:

`124815bf76b90e924a95083fbf50a8cd2a17f64df915df7cd96e147193d29b69`.

`package-linkage-audit.json` checks all 99 Runtime C++ compilation records are
from the isolated Runtime snapshot, not a second compilation of Core. The six
Core-dependent focused executables link the exact installed `.lib`; the device
wire-contract executable correctly has no Core dependency. All focused test
translation units compile with assertions enabled (`-UNDEBUG` after Release
flags). Both profile archives are hashed independently.

Existing positive shaped fixture injection is source-provider-only. The copy
therefore gets explicit test fixture definitions and private Core fixture
header includes. No private Core source is compiled into Runtime. Four test
translation units receive compile errors if the shaping, layout, or fixture
prerequisites are absent. The worker test additionally requires codec11 to be
available rather than silently omitting its positive shaped DOM/input case.
All six qualification-only source edits and their original/qualified SHA-256
values are recorded in `qualification-only-edits.json`.

Covered paths include authenticated installed selection from five real shaping
assets, resource digest and lifetime checks, worker/UI shared Binding ownership,
Core DOM layout/final packets, script mutations, matching renderer pixels,
retained accepted frames and refusal of unsupported/budget-exhausted requests.
These fixtures use a real catalog and a deliberately small generated font;
they are not a general multilingual application or performance qualification.

The complete suite additionally covers native package refusal, frame admission
combinations, scalar/native regressions, device/provider tools, package and
registry tooling, documentation, performance-report tooling, and event policy.

## Complete-suite correction and retained evidence

The first complete-suite run passed 33/34 in both profiles. Documentation
freshness failed because a copied source tree has no Git index. A separate,
disposable Git index now enumerates every snapshot Markdown input, including
new candidate documents; the actual worktree's Git index is untouched. This
also exposed missing freshness metadata on nine new candidate documents, which
was corrected to the current Runtime source version, 0.6.0-dev. Document
changes are hashed in `qualification-doc-metadata-edits.json`.

Initial failing logs are retained; successful final logs are
`runtime-ctest-complete-final2.log`. No C++ implementation or binary needed
changing for this correction. Future script runs create the snapshot document
index before executing the complete suite.

Evidence roots:

- `E:/Experiments/migration-shaped-installed-full-20261117`
- `E:/Experiments/migration-shaped-installed-cropped-20261117`
- Corresponding build roots under `E:/Builds/` with the same names.

Each contains the exact candidate package, source inventory, original focused
result/logs, complete-suite logs, and linkage audit. The later
`complete-result.json` records the final complete-suite log and hashes without
rewriting the original focused result.

## Remaining gates

This closes local full/cropped installed-package consumption for the bounded
v11 path. Publishing a newly versioned matching package, updating the reviewed
production lock, hosted CI, signed physical installation/recovery and shaped
device memory/OOM/latency remain open. Existing ordinary ABI4 device acceptance
is already closed and is not repeated by this qualification.

The real regular catalog alone is 733641 bytes, above the current store/provider
327680-byte cap. Catalog, companion (41598), Unicode tables (50334) and
eligibility (198) already total 825771 bytes before the font, app, JFAPP and
signature envelope. The current 1MiB storage partition has three physical
package slots after two 4KiB registry sectors, only 344064 bytes per slot.
Raising a software cap alone cannot admit this workload. A separately selected
larger storage/profile or a qualified resource-reduction design is required;
the present production partition, cap, defaults and trust keys are unchanged.
