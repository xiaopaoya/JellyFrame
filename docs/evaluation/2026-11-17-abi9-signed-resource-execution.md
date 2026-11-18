# ABI9 signed full-resource execution and C++ refusal checkpoint

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Result

The actual signed store-to-authenticated-Binding-to-JerryScript-worker-to-paint
chain is now host-qualified on contextual ABI9. Matching installed full and
cropped Core packages each pass17/17; ABI4 legacy compatibility also passes17/17.
This is an execution gate beyond compile/provider inspection. It remains host
evidence with simulated NOR and provider wire, not device execution, physical
recovery, C malloc/VM exhaustion or product heap/latency qualification.

Runtime parent is `86c032c6`. Core is unchanged at
`f35f1f778119f360d508b69b0c7a0eaeafae6cb4`, candidate0.6.2/ABI9, source SHA-256
`997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61` (640 files).
Evidence: `E:/Experiments/migration-contextual-signed-store-20261117`, including
`complete-result.json`, `audit.py`, exact frozen signed packages/executables,
compile commands, source hashes, images and verbose logs.

## Implementation and trust boundary

The fixture generator now explicitly selects either legacy `jfsp1-block-v11`
or contextual `jfsp2-block-v12`. Contextual selection requires the exact complete
typed4bpp regular font hash/size from frozen Core fixture metadata. Source/profile,
catalog and companion hashes remain checked. The fixture retains all five
original font/shaping resources, HTML/CSS/JS and license:4539 font glyphs,
4686 catalog raster GIDs and no omitted assets. Asset bytes1547203, JFAPP1549404,
JFSG1550043. A1044480B slot refuses; the opt-in2093056B slot fits.

Candidate execution uses a disposable Runtime snapshot with an exact0.6.2/ABI9
source-hash lock and a matching fixture source manifest. Both are explicit
qualification-only edits. Signing and package inspection run normal strict
checks against that snapshot; no validator is patched to accept a different
version. Production0.9.0 lock, manifests, defaults, trust keys and all Runtime
implementation files remain unchanged. A mismatched candidate lock refuses.

The host store signature symbol is redirected only to its existing test-key
router, which invokes actual IDF5.3.1 mbedTLS software verification. Signing
requires the explicit public-test-signature flag and deliberately public P-256
scalar1 key. The production adapter is compiled separately and tests confirm
its empty allowlist does not trust that key. Never provision it in a product.

The C++ harness selects the actual runtime frame version and contextual value
capture APIs. Its source intentionally supports both old typed commands and
ABI9 flat display commands through the existing access helper. It rejects
scalar Text/PreparedText and, on ABI9, scalar GlyphRun display commands. Final
contextual packets are transported separately and painted through the owned
contextual Binding. A compile-time check prevents silently testing the wrong
route.

Standalone installed consumers now preserve package-first header order. The
parent harness originally saw bundled legacy Core headers before imported
headers; forwarding app_runtime's public include order fixes that harness
configuration. The imported Core target itself is scoped to the Runtime
subdirectory, so the parent does not access an unavailable target. Final
compile/link audits confirm public installed headers, exact imported archives,
active assertions and no Core C++ source fallback for full/cropped.

## Executed checks

Each17-case configuration covers successful transaction/install/execution,
reconstructed boot reauthentication, signature tampering with recomputed
transport CRC, pre-mutation capacity refusal, aggregate resource-budget refusal,
five C++ allocation-recovery cases, provider transport and six crypto cases.

Worker HTML/CSS/JS/font data come through the authenticated installed lease.
The real JS click handler changes source text/count and widths104/148. Initial
frame and three pointer-driven updates match independent Core DOM/layout
packets, insertion positions, baselines and every framebuffer pixel. The
reference is Core itself, not a browser shaping oracle. The rendered initial
and clicked images were inspected; this is a left-aligned diagnostic fixture,
not a product page or its acceptance.

After worker/session/resource/store release and simulated NOR erasure, the
initial accepted frame still paints identically. The new weak-owner check
then releases the last accepted frame and verifies that the Binding expires;
test-local references do not mask a retained-resource lifetime error.

| ABI9 operation | Cold allocations | Injected failures | Result |
|---|---:|---:|---|
| Binding selection |13|13|Refusal preserves selected owner; immediate retry succeeds|
| Worker initialization |430|426|Controlled terminal refusal; fresh worker recovers|
| Direct JS text/width mutation |245|245|Controlled refusal; old accepted pixels survive|
| Pointer input/mutation/publication |292|292|279 terminal,13 retryable publications; old pixels survive|

Full/cropped have the same counts. Four cold initialization indices are not
reached after process caches warm; they are not claimed as injected. Fatal
report allocation refusal/retry publishes exactly one40B record. These are
executable-local new/new[]/aligned/nothrow fault scans, not C malloc, mbedTLS
allocation, JerryScript arena, persistent exhaustion or target fragmentation.
Successful three-click execution follows each sweep. Legacy ABI4 counts remain
13 /426 /244 /291 cold allocations, with424 initialization indices injected.

The actual provider reconstructs all1550043 immutable bytes even after input
file replacement. Its192 JSONL events remain bounded. Contextual missing/partial
capabilities, wrong ABI, cross-route identity and insufficient capacity refuse
before INSTALL_BEGIN (six refusal combinations). Mock transport bypasses live
identity attestation; the separately tested production attestation remains
unchanged. Host inspection still reports signatureVerified:false.

Full JFSG SHA-256: `cd794103eb5ebd262793dc2923d830dc63f6823e579924ab8c69e0ae86e2f3bc`.
Cropped: `9658a49854a127df4b9d219ca08cd00f3c3cb632e2184b36dcd1349fff1d9651`.
Signatures are randomized; each exact frozen package has its own receipt.

## Earlier refusals and remaining gates

The first candidate generator attempt passed a0.6.2 manifest through production
0.9.0 signing policy and correctly refused. The final isolated lock/manifest
scheme fixes test inputs without relaxing policy. Initial harness builds also
exposed legacy command/capture API assumptions and installed include ordering;
their failure logs remain preserved. No Core or production Runtime fix was
needed for the final host suite.

This closes bounded ABI9 host signed execution and the measured C++ failure
scans. Separate direct publication/callback exhaustive scans, C malloc/VM
failure, retained/peak heap, product reserve and latency remain open. Wider
family/fallback, native/Desktop/default-route and candidate product-manifest
parity also remain open. Physical signed install/reboot/tamper/fallback/display
and touch require reviewed trust/profile/data migration. The6MiB storage profile
changes partition boundaries and must not be deployed as an app-only update.
No serial enumeration, flash, push or publication occurred. Full experimental
migration remains incomplete.
