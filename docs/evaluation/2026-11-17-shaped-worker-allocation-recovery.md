# Shaped worker C++ allocation failure recovery

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

The full-resource signed workload exposed uncaught C++ allocation exceptions in
worker initialization, direct script evaluation and pointer processing. Each
failed at injected allocation0 before the fix. These exceptions could escape
the ESP worker task instead of returning a controlled failure. The local fix
handles `std::bad_alloc` and `std::length_error` at the public initialization,
evaluation, frame publication, input and callback boundaries. It records
BudgetExceeded without allocating an error string and preserves already
accepted immutable frames. Fatal-record publication can refuse allocation and
be retried. No public ABI/type layout or successful-path semantics change.

The ESP worker task also catches construction-time allocation/length exceptions,
which occur before a worker can report its own failure. It sets allocation-free
session failure/stop state and reaches the existing worker-done handoff, allowing
the supervisor's ordinary protected teardown. This task entry change is target
compiled; its physical execution has not been fault-injected.

## Executed evidence

`ports/esp32s3-idf/tests/signed_shaped` now passes17/17 with the original complete
1550043-byte signed package and actual store/Binding/JerryScript/v11 painter.
The executable-local allocation shim fails one C++ new/new[] operation at a time,
including aligned and nothrow variants. Injection is disabled during fixture
setup, cleanup and verification. It never enters target firmware.

| Operation | Baseline allocations | Actual injected failures | Outcome |
| --- | ---: | ---: | --- |
| Authenticated Binding selection | 13 | 13 | Refuses; prior Binding valid; immediate retry succeeds |
| Worker initialization | 426 cold | 424 | Controlled refusal; destruction succeeds; fresh worker executes |
| Direct JS text/width mutation | 244 | 244 | Controlled fatal/refusal; accepted frame pixels retained |
| Real pointer click/mutation/publication | 291 | 291 | 278 terminal failures,13 retryable publications; retained pixels unchanged |

Two cold initialization allocation indices are not reached after process-wide
caches warm; the test records that difference rather than claiming injection at
unreached points. Direct/input scans exercise the normal author JS mutation and
shaped DOM rebuild. After each candidate is destroyed, an accepted old frame
still paints pixel-identically. Input/frame mailboxes are drained between
independent candidate workers so a failed pop does not contaminate the next
injection. The ordinary three-click sequence and exact independent Core packet/
baseline/pixel comparison run successfully after each complete sweep.

The fatal-report test creates one worker at a time (JerryScript's host realm is
not a concurrent-worker fixture), forces evaluation failure, fails fatal-report
allocation, retries successfully and proves exactly one40-byte fatal packet is
published. Initial concurrent-worker test setup was invalid and its crash log is
retained separately; the corrected sequential fixture passes.

Matched shaping-ON and shaping-OFF script suites each pass4/4, covering worker
behavior, reentry/ownership and event policy boundaries. The initial build used
an incorrect individual test target name; subsequent builds use the actual
`jellyframe_script_tests` target. These are focused affected checks, not a new
claim that every earlier full-suite gate was rerun.

Actual IDF5.3.1 shaping/CoreText/installed-worker rebuild passes. BIN4515744B,
SHA-256 `6c458fdf0e0f7d5ead9db12941b2b282337f7fe10e613a3be319ba7e30021354`;
ELF SHA-256 `312cf5274830e790dc082423465a2a0d4ff185fb9aca560c083385f4b08f265d`.
Compared with the previous same-profile4515040B build, image size increases704B.
This does not measure runtime instruction or heap cost. The6MiB storage profile
and effective2093056-byte slot are unchanged. A matching provider0.1.2-dev local
candidate is rebuilt from exact app/table/recovery bytes; independent schema,
seven real route-symbol and source-snapshot checks accompany the result.

Evidence: `E:/Experiments/migration-shaped-oom-20261117/complete-result.json`,
host verbose log, ON/OFF affected-test logs, target build/release logs and source
snapshot. Earlier failing injection logs remain under
`E:/Experiments/migration-real-shaped-store-20261117/*oom*.log`.

## Limits and next gates

This is C++ exception/refusal evidence on the host. It does not fail C malloc,
JerryScript's arena or mbedTLS allocations, emulate persistent exhaustion, detect
leaks, measure peak/fragmented embedded heap, or establish a product reserve.
The shim replaces allocations only in the test executable. Worker construction
continues to have a throwing allocation contract; the ESP task now handles it.
Init/mutation/input are exhaustively scanned over the measured host allocation
indices; the callback and direct frame-publication guards share the same policy
but do not yet have separate exhaustive fault scans. VM C callback unwind safety
under arbitrary failures remains a distinct backend question.

No device/serial endpoint was accessed; production defaults, keys and dependency
lock are unchanged. Physical signed install/recovery, actual startup/interaction
heap peaks, OOM recovery and latency remain open. ABI9 resource identity and its
contextual layout/paint route, reviewable PRs, published matching packages and
hosted CI remain required for full migration.
