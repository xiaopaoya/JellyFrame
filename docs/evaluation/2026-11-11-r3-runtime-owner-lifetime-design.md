# R3 owner lifetime design: required next boundary

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Historical experimental checkpoint; current delivery policy and qualification are tracked separately in the migration execution board.

This design accompanies the experimental shared DOM Event implementation. It
does not claim that active Runtime destruction is safe. The implemented bind
admission and dispatch failure rules are independent prerequisites.

## Actual ownership and call paths

`create_script_runtime()` returns `std::unique_ptr<ScriptRuntime>` and concrete
`JerryScriptRuntime` objects may also live on the C++ stack. Its destructor
clears bindings and calls `jerry_cleanup()` synchronously. A DOM listener owns
only a raw `JerryScriptRuntime*`; an Event completion record retains a native
JerryScript root, not a C++ owner. `ScriptDomMutationScope` tracks execution,
but its destructor also touches the Runtime and cannot prolong its lifetime.

`ScriptTaskWorkerRuntime` owns its Runtime, `DomOwner`, input controller and
pipeline. `stop()` resets the Runtime before clearing the DOM/pipeline. Its
destructor calls `stop()`. `ActiveSupervisorScope` restores fields on the
worker after a call, so retaining only the VM would leave that scope dangling.
Worker gateway callbacks use the worker's raw `this` pointer. The browser
similarly owns `script_runtime_` and resets it during rebuild, recovery and
destruction; DOM mutation/audio host hooks use the browser's `this` pointer.

These paths require an owner operation contract. A Runtime-local pending-stop
flag cannot keep an enclosing `unique_ptr` or stack object's C++ storage alive.
No destructor should attempt to wait on its own callback stack or return while
continuing to use already-destroyed members.

## Bounded session state and operation pins

Introduce one heap-allocated session `State`, owned initially by the host
facade. It contains the Runtime, document and pipeline and the callback gateway
state that presently points to the worker. Do not add shared ownership to each
Node or Event wrapper. Add an intrusive count for active owner operations and
one host-owner reference to this one State; guard acquisition performs no heap
allocation. Check a configured maximum before incrementing the operation count.

Every public session entry (`initialize`, `eval`, input, timer/frame pumping,
service completion and rendering) acquires an operation pin before reaching
the VM and retains it until all return values and native roots are released.
Native DOM listener entry and Core dispatch completion require a session pin
too: Core can invoke these independently of a worker public entry. Root-record
enrollment retains a pin until completion snapshots and frees its Jerry value.
Pins reference State, not the host facade. Refactor native gateway contexts to
State so destroying the facade cannot invalidate an executing callback's user
pointer. External code may no longer retain a naked session Runtime pointer
across an operation boundary.

Represent lifecycle with `Running`, `StopRequested`, `Draining`, and `Stopped`.
`request_stop()` first revokes admission to new outer operations and service
requests. It clears the facade's owner reference and records a pending action;
it never physically resets State inside an operation. A pin can survive loss
of the facade reference. At the last pin release, after the completion-root
count and VM execution depth are zero, an out-of-callback drain clears native
listeners, service callbacks and Jerry roots, invokes `jerry_cleanup()` exactly
once, destroys pipeline/DOM, and finally releases State. Deferred release must
occur after the guard's last State access, not from a callback still using it.
For a DOM record pin released by Core completion, finish releasing the Jerry
root and unlinking the record before any drain; if an outer operation still
exists, its pin supplies the later drain point.

Pending binds need owned input. Either continue the implemented explicit busy
refusal and let the host retain/retry its `DomOwner`, or introduce a separate
owner-level move of a complete new document/session. Do not queue `Node*` or
`Node&`. Permit at most one pending bind; choose an explicit refusal/replacement
rule. Stop wins over bind. Existing wrapper invalidation and document epoch
change happen once at an admitted idle drain, with old and new document owners
alive through that transition. An old callback cannot publish a frame for the
new epoch. Admission distinguishes continuation of an already pinned operation
from a new outer operation, so ordinary synchronous dispatch remains usable.

The browser needs the same per-session boundary plus host lifetime admission
for UI callbacks. Retaining a session does not retain `BrowserApp`, its HWND or
audio/mutation callback context. Either pin that owner across the entire native
UI entry, or detach hooks through a separately surviving host control object
whose revocation is checked before access. A per-session VM pin alone is
insufficient for browser teardown.

## Required tests before implementation is claimed safe

Use real owner reset/stop requests from DOM listeners, mutation hooks, timers,
service callbacks and nested dispatch. Destroy the outer facade, not merely a
test flag. Verify no return scope uses it afterward; State remains alive until
the final completion root and operation pin drain; shutdown/cleanup occur once;
new operations are refused after logical stop; old callbacks and snapshots
cannot operate on a new document epoch. Test stop followed by bind and bind
followed by stop, pending capacity, document destruction before retry, nested
failure while draining, and allocation refusal during pending-state creation.
Use Debug plus an appropriate memory-safety tool. Audit all `runtime_.reset()`
and `script_runtime_.reset()` callers and native callback user contexts.

True JerryScript heap exhaustion follows the engine fatal path
(`jmem_heap_gc_and_alloc_block`, `JERRY_FATAL_OUT_OF_MEMORY`), rather than a
recoverable C++ exception. It requires the separate worker fatal/isolation
contract; the native C++ allocation tests do not qualify engine-OOM recovery.

## Scope of this increment

The current change returns false from `bind_document()` during execution,
result conversion or active DOM Event roots and retains no pending document.
The host keeps ownership and may retry after the current dispatch returns.
It bounds active DOM Event records, reports `ResourceLimit`, and uses Core's
dispatch-failure API so failed construction delivers no incomplete Event and
stops the remaining listeners even for noncancelable Events. This is atomic
publication/failure of the refused Event, not rollback of earlier listeners or
DOM effects. Physical owner teardown, Node identity across callbacks, Window,
XHR, audio and service Event identity remain separate unimplemented gates.
