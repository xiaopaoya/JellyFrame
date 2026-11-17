# ABI9 requested C++ memory, host phase timing and VM fatal boundary

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Result and scope

Installed ABI9 full/cropped and ABI4 compatibility each pass20/20 without
skips, extending the previous17 cases with a ledger self-check,32-update
resource observation and an isolated actual worker VM-exhaustion process.
The existing C++ OOM sweeps still pass with unchanged allocation counts.
The real ESP external-context allocator also passes its existing Clang ASan
stub test. No production implementation, Core, ABI layout, lock, defaults,
keys, device contents or publication state changed in this slice.

Runtime parent: f1d8b628c05a7550e46d306c319cc6123d4122ea. Core stays at
f35f1f778119f360d508b69b0c7a0eaeafae6cb4; source identity
997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61.
Evidence: E:/Experiments/migration-contextual-resource-cost-20261117,
including complete-result.json, source/package/executable hashes, verbose
tests, original serial samples and the ASan port log. Previous frozen signed
execution evidence is unchanged. New binaries are frozen separately.

## Instrumentation and measurements

The executable-local C++ replacement allocator now keeps an allocation-free
prefix recording requested size and raw malloc pointer. It preserves ordinary
and over-alignment, zero-size allocation, overflow rejection and all delete
forms. Atomics record live/peak/cumulative requested bytes and allocation count.
Counts include startup allocations and frees after a phase ends. Self-checks
cover alignment256, sized delete, refusal, overflow, zero size and full release.
The executable is single-threaded at measured boundaries; resetting peak is
not offered as a concurrent production API.

Measurements exclude the ledger prefix/alignment waste, malloc metadata,
C malloc, mbedTLS C allocations and JerryScript arena. They add instrumentation
overhead. Process totals include6MiB simulated NOR, signed package storage,
test-only scalar resource/catalog copies and Core reference/framebuffers.
These totals are not a device working set. The font helper also retains a
2MiB-capacity vector after resizing to716274 bytes, solely in this harness.

Each profile has five independent serial processes,32 real pointer-triggered
JS updates per process, and normal packet/pixel comparisons after every update.
Comparison and image writing are outside timed operations. Input queuing is
outside process_one timing; paint includes framebuffer creation, atomic staging
and the existing nonempty-pixel assertion. Every loop update is compared to
independent Core DOM/layout, not a browser oracle.

Representative ABI9 full host measurements (full-samples/result.json):

| Phase | Median host time | Requested bytes per phase | Maximum extra live bytes |
|---|---:|---:|---:|
| Binding selection |27.115ms|3085328|3085328|
| Worker initialization |15.401ms|13089376|905865|
| Initial application script |14.215ms|13064984|895121|
| First pointer-up update + publication |15.326ms|13939882|895057|
| Later pointer-up update + publication |14.580ms|13072006 median|895001|
| Initial explicit publication |0.033ms|5309|5033|
| Later acceptance/take |0.009ms|5668.5 median|5480|
| Later paint including framebuffer |0.195ms|440418|440414|

Later pointer-up p95 is15.508ms across155 observations. Repeated samples are
correlated within each process, so this is a descriptive percentile, not a
statistical proof of improvement. Five-process cold-phase samples are small.
Do not extrapolate these x64 Release instrumented timings to ESP32 or compare
them with previous uninstrumented timings. Cropped/ABI4 serial observations
are controls in their result files, not an optimization admission claim.
Earlier concurrently sampled controls are preserved but superseded by serial
control samples. No controlled regression threshold was applied in this slice.

Binding retains1543092 requested bytes while briefly holding two resource
copies. An ordinary update cumulatively requests about13.07MB, but increases
simultaneously live C++ bytes by at most about0.895MB; cumulative traffic is
not peak RAM. At the same update branch/count-length after warmup (click12
versus32), retained bytes differ by0 in all five processes for all three
profiles. Final accepted-frame release expires the weak Binding owner. This
is bounded retention evidence, not full leak/fragmentation/reserve qualification.

## VM exhaustion is a distinct fatal boundary

The new subprocess initializes the actual authenticated installed worker, then
runs a finite allocation-heavy JavaScript program retaining nested arrays.
The pinned host Jerry port exits10 (JERRY_FATAL_OUT_OF_MEMORY), after an explicit
initialized-worker marker. The Python parent checks both and enforces a30s
timeout; CTest has40s timeout. A returned JS failure, setup failure or timeout
does not pass. This demonstrates actual fatal VM behavior, not recovery.

The linked host Jerry build uses an internal512KiB arena, MEM_STATS OFF and
SYSTEM_ALLOCATOR OFF. The real ESP component uses external context with a256KiB
arena. Its fatal handler logs and calls vTaskSuspend(NULL), followed by an
infinite delay guard. C++ bad_alloc handling cannot catch this VM fatal path;
task/session cleanup, terminal reporting and restart are not established by
this suite. Unsafe longjmp across Runtime C++ owners is not a recovery option.

The real ESP port's existing ASan test compiles jerry_port_esp32s3.c against C
stubs and verifies8-byte context alignment,256KiB arena addition, PSRAM-first
and fallback allocation, release, both allocations failing and size overflow.
Its C test intercepts vTaskSuspend with setjmp only to observe the fatal call.
This is not a live FreeRTOS/VM heap test and does not prove target recovery.

## Next gates

Keep resource qualification explicit: target-equivalent VM budget and product
reserve/latency, C malloc/crypto failure and safe terminal task supervision remain
open. The13MB temporary allocation traffic is a measured profiling lead, not
yet attributed to an exact function or solved by a speculative cache.

The next migration slice should return to candidate app manifests, desktop/
sample integration and wider family/fallback/native/default route parity.
The current route is still bounded static blocks; no image/effect/general-inline
parity follows from this test. Physical signed install/reboot/tamper/fallback
and display/touch require reviewed trust, matching target and partition/data
migration; released ABI4 acceptances stay closed. Full migration is incomplete.
