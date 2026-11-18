# ABI9 complete configured installed-consumer matrix

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Result and scope

The matching public installed ABI9 packages now pass all configured Runtime
tests in full, cropped and contextual-OFF configurations: **36/36 each**, no
skips. This replaces the preceding 11-test scoped installed-consumer result.
It is not a claim of complete sample/Desktop/product-route parity or signed
package execution. Examples, benchmarks, sample regression and Core's own
test suite are excluded from these Runtime configurations.

Runtime production code is commit `6dd25c88dd1dc0f31da34a8492be36ae6f1d8139`.
All three installed packages identify Core candidate0.6.2/ABI9, 640 files,
source SHA-256 `997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61`.
Core remains unchanged at `f35f1f778119f360d508b69b0c7a0eaeafae6cb4`.
Final evidence is
`E:/Experiments/migration-contextual-installed-verified-20261117/final-audit`;
its parent contains unchanged build/configure/CTest logs and source inputs.

## Isolation and checks

`project_tools/qualify_contextual_installed_packages.py` accepts explicit
installed full/cropped/off prefixes and creates a new isolated Runtime source
snapshot and build trees. It verifies profile dependencies, common source
identity, source-manifest digest and archive hashes. Package consumers retain
exact version, ABI and source-hash enforcement; provenance must say the lock
was enforced. No Core C++ source implementation is compiled into consumers.
Offline fixture bytes are supplied separately; no Core private include tree
is added. All compiled production C++ bytes match the current Runtime source.

Candidate CMake qualification and production author-tool policy are separate:
only the snapshot's CMake lock include selects a new exact candidate lock.
The original production dependency lock, source manifests, tools, tests and
documentation remain unchanged. Thus source-tool tests continue checking the
released0.9.0 manifests, while C++ consumers compile/link against exact0.6.2/
ABI9. No compatibility checks are relaxed. This avoids falsely treating a
candidate CMake lock edit as completed migration of every product manifest.
The snapshot-only changes and original hashes are recorded explicitly.

The 36th test is an additional installed-route probe. It checks actual
contextual/layout API selection, refuses scalar canonical availability,
accepts frame12 only in full/cropped, and rejects old frame10/frame11 in all
three configurations. Existing resource-owner, worker, renderer, wire and
port admission tests run without configured skips, including the OFF refusal
paths. Cropped disables Canvas2D, modern paint, flex/grid, advanced forms and
length math; OFF disables contextual text/layout/JFSP2.

Compile commands are audited for active assertions. The existing independent
CPU timing test uses explicit `throw std::runtime_error` checks instead of
assert; the audit records that narrowly checked exception. Its checks remain
active in Release. Other consumer test units use `-UNDEBUG`.

## Earlier failures retained

The preflight rejected the old installed OFF prefix: its contextual layout
header and implementation predated the final Core checkpoint, giving source
hash `e7a94107…b69ae`. A fresh OFF package was built from the unchanged current
Core into a new prefix. The old package and evidence were preserved.

The first new route probe linked only app_runtime, omitting its frame codec's
script_task_runtime library. The qualification harness now links the owning
library. No Runtime implementation fix was required for that linker failure.

All three subsequent 36-test CTest runs passed. Their initial evidence audit
then refused the CPU timing test's lack of `-UNDEBUG`. Inspection confirmed
its checks use unconditional throws. The final audit applies the narrow
always-on-check rule to the existing logs, compilation records and sources;
it does not rerun tests or change source/test flags to hide that refusal.
Original error results remain alongside `final-audit/result.json` and its
receipt. These harness/audit corrections are distinct from product bugs.

## Next gates

Actual candidate app manifests and broader native/Desktop/default routes
remain unqualified, including family/fallback policy beyond the bounded
contextual static-block route. Next qualify complete signed ABI9 resource
installation through store, authenticated lease, Binding, worker and paint,
then refusal/recovery and Runtime resource/latency limits. Earlier ABI4
signed/OOM results cannot close these gates.

The exact committed target/provider checkpoint is documented in
[the capability report](2026-11-17-abi9-contextual-capabilities.md). Physical
signed transport/reboot/tamper/fallback and display/touch remain separate.
Production defaults/lock/keys are unchanged; no serial enumeration, flashing,
push, publication or product enablement occurred. Full migration is incomplete.
