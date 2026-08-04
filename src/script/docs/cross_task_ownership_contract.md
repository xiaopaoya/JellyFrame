# Script-App Cross-Task Ownership Contract

> Last updated: 2026-08-04; Applies to: 0.5.0-dev; Status: 0.6 prerequisite with foundation landed

This contract defines how an RTOS host runs a real JerryScript App without moving DOM,
JerryScript or renderer objects across tasks. The direct, same-thread desktop
`JerryScriptRuntime::bind_document(Node&)` path does not satisfy it by itself.

The system UI task owns launcher, sampled input, presentation rendering, framebuffer and panel
output. The script worker owns exactly one realm, private app DOM, wrappers, listeners and timers.
The supervisor owns session generation, worker lifetime, service scope, bounded mailboxes,
native-lease registry and recovery. A session is the nonzero tuple
`app_instance_id`, monotonic `generation` and `worker_epoch`; all packets validate the full tuple.

No packet, queue, timer, callback, handle payload or fatal record may carry raw DOM/layout/layer/
display pointers, JerryScript values or wrappers, framebuffer/panel/DMA/GPIO/NVS/file handles, or
task-local container/arena addresses. Cross-task data is a bounded value copy or a supervisor-owned
opaque lease ID checked against the full session.

A worker publishes a sealed, immutable replacement `AppFrame`: version/session/sequence/viewport,
POD paint commands, bounded text bytes, validated resource lease IDs and hit regions with numeric
`target_key` values. The UI task hit-tests the accepted frame and sends normalized input values back;
the worker resolves `target_key` in its private DOM, dispatches JS, mutates its DOM and publishes a
new frame. Neither task reaches into the other's DOM or renderer state. A sealed frame is immutable;
the supervisor owns its bounded lease lifecycle and records coalescing or queue-full drops.

The supervisor is also the only owner of `AppRuntimeHost`, host queues and handle table. Service
requests and completions are value packets. Cancellation installs a tombstone before queue cleanup;
late completions only release their handle and update counters. Native wrappers retain session-scoped
opaque tokens only; their finalizers emit idempotent release intents, never dereference UI or service
objects.

Fatal recovery occurs at a task-local C-safe boundary, not across C++ destructors. On fatal,
watchdog, budget failure or exit, the supervisor first closes input/services, invalidates the session,
cancels requests and freezes leases, then waits for worker exit, releases queued frames/handles and
returns the UI to the trusted launcher. No heap, slot or session identity is reused before every owner
has acknowledged release.

Before a port claims real script-App support, it must prove touch-to-frame redraw, completion/cancel/
late-completion handling, frame lease replacement, fatal plus native-wrapper teardown, and 30 real
launch/fail/recover cycles without cross-App state, dangling access or system reset.

## Current platform-neutral foundation

`src/app_runtime/script_task_contract.*` now implements and tests session generation/epoch validation,
fixed-slot value mailboxes, session-scoped sealed frame leases, cancellation tombstones with late-
completion classification, deduplicated native-release intents, and a two-stage `ScriptTaskSupervisor`
teardown that does not create a task or VM. It has no JerryScript, RTOS, DOM or renderer dependency;
it does not start a worker or paint an `AppFrame`.

The next slice is an `AppRuntimeHost` service bridge mapping request/completion/handle lifetimes to
these value tokens, plus a worker-side serializable AppFrame encoder and input target-key resolver.
Ports must not fill that gap with raw pointers.
