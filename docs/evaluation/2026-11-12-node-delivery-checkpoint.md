# Canonical Node delivery candidate

Last updated: 2026-11-12; Applies to: 0.6.0-dev

This candidate integrates the complete weak table, routing, GC retirement and
native allocation-refusal corrections into the dual-mode R3 Runtime. One CMake
module and capability header configure both policies. Node identity defaults
OFF and independently supports either Event mode. Enabled cache capacity is
1 through1024. OFF avoids table allocation and WeakRef capture.

Enabled lookups reuse live wrappers and expandos. Finalizers mark native
metadata, then ordinary VM entry releases weak roots and reuses slots. Helpers
have a separate binding budget. The engine publication repair comes from master.

Document binding checks publication. Early busy/readonly-global refusal preserves
the old document. A late construction/publication failure stops the session and
invalidates bindings with an observable failure latch. A fresh Runtime recovers.
Own-property updates preserve attributes, including native function prototypes
and writable nonconfigurable document globals. Strict construction initially
rejected prototypes during integration and was corrected by these updates.

Four final Clang ASan+UBSan complete script/worker groups pass1/1 each, covering
locked ABI3 legacy with identity OFF/ON and new Core R3 with identity OFF/ON.
ABI3 source is fa5914ce.../111 and new Core source f7c17009.../145. Runtime and
Jerry core/ext are instrumented. ABI3 uses a rebuilt package with compatible
annotations and new Core is instrumented from source. Platform port remains
uninstrumented and leak detection is disabled.

Cache-ON suites execute generation/epoch, real GC turnover, retained identity,
limits and persistent native-allocation regressions. Both policies execute
readonly-global refusal, writable nonconfigurable rebind, late binding failure,
stopped-state failure access and fresh Runtime recovery.

Evidence is E:/Experiments/node-delivery-20261112 with final-ctest logs for all
four routes and reproduction runners. Native full/Debug, installed profiles,
target compile/resources, remote CI and device acceptance remain pending.
