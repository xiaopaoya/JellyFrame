# ESP32 external Jerry context delivery

Last updated: 2026-11-12; Applies to: 0.6.0-dev

The ESP-IDF JerryScript component enables external contexts for engine, port and
consumers together. Context and the256 KiB heap share an8-byte-aligned allocation.
Allocation prefers PSRAM and falls back to8-bit-capable memory. Size overflow
and allocation refusal enter the existing fatal task boundary before JerryScript
can clear or use an invalid context. Cleanup releases the allocation.

The scoped allocator commits4621acd4/6b3722bf are integrated while preserving
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
sourcefa5914ce.../111 is0.8.0/ABI3. Supported script targets with forms/RTTI ON
and forms/RTTI OFF compile and link after the source compatibility corrections.
Core PR28 source5af3a7f6.../148 also links with R3/forms ON and RTTI OFF.
The unsupported ABI3 forms-ON/RTTI-OFF combination is rejected by its existing
capability gate. Initial failures and subsequent successful build logs are kept.

The host allocation harness passes ASan against the real port translation unit.
Task suspension is intercepted by the harness. Physical fatal isolation and
recovery require target evidence. Exact final-head manifests and the combined
frame10/Node/CoreText/BMP target are being qualified.

Physical PSRAM placement, fallback region, internal reserve, heap/largest-block
pressure, repeated initialization/cleanup, stack watermark, presentation and
interaction latency remain pending. Existing hardware observations retain their
recorded source/configuration scope.
