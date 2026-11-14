# Jerry external context on ESP32-S3

Last updated: 2026-11-11; Applies to: 0.6.0-dev

> Historical measurement, superseded for capacity interpretation by Runtime PR #98 (`92f6b62d`, merged 2026-11-14). The `16,383 / 16,384` value below is only the dedicated static-IRAM linker category. It does not mean total internal memory has one byte left: the same build's D/IRAM data window had about 270 KB available. Do not treat this measurement as an active memory blocker.

The component now publicly defines JERRY_EXTERNAL_CONTEXT=1 for the engine,
port and backend. The existing single-VM gate controls the global port context.
The existing preferred PSRAM allocation and 8-bit-capable fallback policy is
preserved. Context storage is rounded to 8 bytes before adding the 256 KiB heap,
matching the pinned JerryScript JMEM_ALIGNMENT_LOG=3 and jerry_init alignment.
Overflow and allocation failure enter the existing non-returning OOM fatal
boundary before jerry_init can clear a null pointer or underflow the heap size.

The real port C translation unit is compiled against host task/allocator stubs
by tests/run_context_allocation_test.py. Clang 22 debug and ASan runs passed
preferred allocation, fallback, alignment/size, exactly-once free, both failed
allocations, round-up overflow and heap-add overflow. The failure tests enter
the real fatal function and intercept its task-suspend boundary with longjmp.
No engine OOM recovery or fatal-task isolation is qualified by this harness.

Fresh ESP-IDF5.3.1 script-app acceptance compile/link passed in
E:\Builds\r3-jerry-psram-ws147-20261111 with six jobs and RTTI OFF, against
Core 15169c02b612d3a7548232c1bf58cc65a83742b5, source 98f89f0f/146, ABI 4.
The firmware was built before the scoped commit. Its Git app version identifies
ae6f508f with a dirty suffix. The two modified component source files are
identified by SHA256 in source-hashes.json and match the committed change. Source hashes and
artifact hashes are preserved in E:\Experiments\r3-jerry-psram-20261111.

IDF reports static D/IRAM usage of 71171 bytes, compared with 335579 for the
previous same script profile, a reduction of 264408 bytes. Its category
remainder is 270589 bytes. The independent linker-address audit includes
alignment and reports 71296 bytes of shared static occupancy, leaving 270464
bytes in the actual data window. This matches the same 264408 byte reduction
from baseline. Instruction occupancy is 68608 bytes, including 52224 bytes in
the shared instruction region. These address scopes differ from raw section
sums. The audit is preserved as linker-address-audit.json in the experiment.
BSS is 6992 bytes, data 12076, D/IRAM text 52103. Dedicated IRAM remains 16383 bytes
with 1 byte category remainder. ELF symbols remove jerry_global_heap and
jerry_global_context and retain the 4 byte context pointer, port context functions,
backend eval and worker eval. BIN is 0x5b5160 bytes. Flash text 1423148 and
rodata 4480540 bytes produce 5984506 bytes of map-derived image before padding.

The heap/context allocation is now dynamic. Actual PSRAM placement, fallback
reserve, dynamic heap pressure, stack and UI behavior require runtime evidence.
Internal-memory fallback can consume the available internal reserve. The
existing OOM fatal path suspends the script task and remains subject to the
separate fatal/isolation contract. No flash was performed. The legacy installed
frame 5 UI migration to canonical catalogs/providers remains separate work.
