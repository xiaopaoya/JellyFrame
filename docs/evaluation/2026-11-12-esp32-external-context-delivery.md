# ESP32 external Jerry context delivery

Last updated: 2026-11-12; Applies to: 0.6.0-dev

The ESP-IDF JerryScript component enables external contexts for engine, port and
consumers together. Context and the 256 KiB heap share an 8-byte-aligned allocation.
Allocation prefers PSRAM and falls back to 8-bit-capable memory. Size overflow
and allocation refusal enter the existing fatal task boundary before JerryScript
can clear or use an invalid context. Cleanup releases the allocation.

The scoped allocator commits 4621acd4/6b3722bf are integrated while preserving
the current WeakRef source patch and public engine definitions. The real port
allocation harness exercises preferred allocation, fallback, alignment, release,
both arithmetic overflow checks and complete allocation failure. CI now runs
that harness with Clang ASan in the scripting job.

Target qualification exposed two existing source compatibility errors. The BMP
adapter was compiled even when disabled, and gradient detection called the new
command type API directly. Source selection now follows the BMP feature switch,
and gradient detection reuses the Runtime command-access adapter. Enabled BMP
retains its canonical API dependency.

## Qualification

Evidence is under E:/Experiments/psram-delivery-20261112. Locked release Core
source fa5914ce.../111 is 0.8.0/ABI3. Supported script targets with forms/RTTI ON
and forms/RTTI OFF compile and link after the source compatibility corrections.
Core PR28 source 5af3a7f6.../148 also links with R3/forms ON and RTTI OFF.
The unsupported ABI3 forms-ON/RTTI-OFF combination is rejected by its existing
capability gate. Initial failures and subsequent successful build logs are kept.

The host allocation harness passes ASan against the real port translation unit.
Task suspension is intercepted by the harness. Physical fatal isolation and
recovery require target evidence. Four targets at Runtime 9acd79fd link and pass
artifact verification. Later changes affect the host test runner and metadata.

| Target | BIN bytes | Policy |
| --- | --- | --- |
| Locked Core 0.8 | 5,865,392 | Forms/RTTI ON, legacy Events, Node identity OFF |
| Locked Core 0.8 cropped | 5,829,904 | Forms/RTTI OFF, legacy Events, Node identity OFF |
| Core PR28 script app | 5,995,760 | R3/forms ON, RTTI/Node identity OFF |
| Core PR28 installed frame10 | 6,951,824 | R3/Node identity/CoreText/BMP/forms ON, RTTI OFF |

The combined image SHA256 is
`abf5b4e6f522d7836d16d8113d2073b8407781dca6370d4b8f568848eb89f57f`.
`artifact-evidence.json` records all hashes, source/profile identities and actual
policies. Compile commands verify external context and WeakRef definitions in
engine, patched GC, port and backend. ELF symbols confirm the port context
pointer and the absence of static Jerry context/heap symbols. The combined
8 MiB app partition has 17% free. Positive image-adapter admission is compiled
in that target, while the other three omit the disabled adapter translation unit.

The Windows CI allocator runner locates the shared ASan runtime through Clang's
runtime/resource directories. A local run with the preconfigured runtime path
removed passes. Initial CI DLL-loading and historical-document metadata failures
are preserved with their corrective commits. Required remote CI remains pending.

Physical PSRAM placement, fallback region, internal reserve, heap/largest-block
pressure, repeated initialization/cleanup, stack watermark, presentation and
interaction latency remain pending. Existing hardware observations retain their
recorded source/configuration scope.
