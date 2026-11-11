# Jerry external context on ESP32-S3

The component now publicly defines JERRY_EXTERNAL_CONTEXT=1 for the engine,
port and backend. The existing single-VM gate controls the global port context.
The existing preferred PSRAM allocation and 8-bit-capable fallback policy is
preserved. Context storage is rounded to8bytes before adding the256KiB heap,
matching the pinned JerryScript JMEM_ALIGNMENT_LOG=3 and jerry_init alignment.
Overflow and allocation failure enter the existing non-returning OOM fatal
boundary before jerry_init can clear a null pointer or underflow the heap size.

The real port C translation unit is compiled against host task/allocator stubs
by tests/run_context_allocation_test.py. Clang22 debug and ASan runs passed
preferred allocation, fallback, alignment/size, exactly-once free, both failed
allocations, round-up overflow and heap-add overflow. The failure tests enter
the real fatal function and intercept its task-suspend boundary with longjmp.
No engine OOM recovery or fatal-task isolation is qualified by this harness.

Fresh ESP-IDF5.3.1 script-app acceptance compile/link passed in
E:\Builds\r3-jerry-psram-ws147-20261111 with six jobs and RTTI OFF, against
Core15169c02b612d3a7548232c1bf58cc65a83742b5, source98f89f0f/146, ABI4.
The build used this branch's scoped source diff on ae6f508f. Source hashes and
artifact hashes are preserved in E:\Experiments\r3-jerry-psram-20261111.

Static D/IRAM is71171bytes, compared with335579 for the previous same script
profile, a264408byte reduction. D/IRAM category remainder is270589bytes.
BSS is6992bytes, data12076, D/IRAM text52103. Dedicated IRAM remains16383bytes
with1byte category remainder. ELF symbols remove jerry_global_heap and
jerry_global_context and retain the4byte context pointer, port context functions,
backend eval and worker eval. BIN is0x5b5160 bytes. Flash text1423148 and
rodata4480540 bytes produce5984506bytes of map-derived image before padding.

The heap/context allocation is now dynamic. Actual PSRAM placement, fallback
reserve, dynamic heap pressure, stack and UI behavior require runtime evidence.
Internal-memory fallback can consume the available internal reserve. The
existing OOM fatal path suspends the script task and remains subject to the
separate fatal/isolation contract. No flash was performed. The legacy installed
frame5 UI migration to canonical catalogs/providers remains separate work.
