# Script-App Cross-Task Ownership Contract

> Last updated: 2026-08-15; Applies to: 0.6.0-dev; Status: 0.6 foundation landed

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
The supervisor serializes session transitions with mailbox admission and returns session snapshots by
value, so teardown cannot race a worker against mutable generation storage.
Every worker-side `take_worker_packet(...)` operation must pass its own
`ScriptAppSession`; there is deliberately no "take the current worker input"
convenience API. This keeps a delayed worker from consuming a newer lifetime's
packet.

A worker publishes a sealed, immutable replacement `AppFrame`: version/session/sequence/viewport,
POD paint commands, bounded text bytes, validated resource lease IDs and optional hit regions with numeric
`target_key` values. Raw normalized input is the authoritative first-version path and is hit-tested by the
worker against its private DOM/layer tree; a UI task may use accepted target regions only as an optimization.
The worker dispatches JS, mutates its DOM and publishes a new frame. Neither task reaches into the other's
DOM or renderer state. A sealed frame is immutable;
the supervisor owns its bounded lease lifecycle and records coalescing or queue-full drops.

The supervisor is also the only owner of `AppRuntimeHost`, host queues and handle table. Service
requests use a dedicated worker-to-supervisor mailbox and completions use the worker input mailbox;
frame traffic never shares either service consumer. Both directions are bounded value packets.
Cancellation installs a tombstone before queue cleanup;
late completions only release their handle and update counters. Native wrappers retain session-scoped
opaque tokens only; their finalizers emit idempotent release intents, never dereference UI or service
objects.

Fatal recovery occurs at a task-local C-safe boundary, not across C++ destructors. On fatal,
watchdog, budget failure or exit, the supervisor first closes input/services, invalidates the session,
cancels requests and freezes leases, then waits for worker exit, releases queued frames/handles and
returns the UI to the trusted launcher. No heap, slot or session identity is reused before every owner
has acknowledged release.
While a session is retiring, the supervisor rejects a new session. Only after
`complete_teardown()` has released the retiring session's leases, tombstones
and queued release intents may `begin()` admit another worker.

Before a port claims real script-App support, it must prove touch-to-frame redraw, completion/cancel/
late-completion handling, frame lease replacement, fatal plus native-wrapper teardown, and 30 real
launch/fail/recover cycles without cross-App state, dangling access or system reset.

## Current platform-neutral foundation

When both `JELLYFRAME_BUILD_SCRIPTING=ON` and `JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON`, the separate
`jellyframe_script_task_runtime` target compiles `src/app_runtime/script_task_contract.*` and implements
and tests session generation/epoch validation,
fixed-slot value mailboxes, session-scoped sealed frame leases, cancellation tombstones with late-
completion classification, deduplicated native-release intents, and a two-stage `ScriptTaskSupervisor`
teardown that does not create a task or VM. `script_task_service_bridge.*` maps those tokens to
`AppRuntimeHost` jobs and serializes completion values into a fixed 24-byte packet. The optional target has no
JerryScript, RTOS, DOM or renderer dependency; it does not start a worker or paint an `AppFrame`.
`script_task_frame_codec.*` now encodes a bounded `DisplayList`, viewport and paint-ordered opaque input
target keys into a versioned value frame; session and sequence remain in the surrounding frame lease.
Frame v3 additionally carries a bounded 1/1024 fixed-point affine matrix per command, so worker-local
`rotate()` and `scale()` do not disappear when a frame crosses into the UI task. v1/v2 reject transformed
commands explicitly instead of silently painting an untransformed result. `make_script_task_app_frame()`
flattens the worker-private `LayerNode` before copying this value frame.
`script_task_input_codec.*` provides versioned pointer, wheel, key and bounded text values for the worker inbox.
`script_task_input_dispatch.*` consumes those values only through the worker-private `InputController`.
`script_task_service_request_codec.*` encodes a fixed 20-byte typed request and
a fixed 12-byte cancellation identity;
the supervisor decodes it before `ScriptTaskServiceBridge::submit_packet()` touches the host.
`ScriptTaskServiceBridge::pump_service_requests()` is the only mailbox drain;
it routes cancellation packets through `cancel_packet()` and reports per-rejection
and cancellation counters without consuming frames or worker inbox data.
An accepted wire request that the host rejects is returned through the normal
bounded completion path as a terminal value, rather than silently disappearing.
The supervisor has a separate session-scoped sealed service-payload lease
registry. `ScriptTaskServiceBridge` copies a bounded result representation
through `ScriptTaskServicePayloadWriter`, publishes it to that registry, and
places only the resulting 64-bit lease ID in completion packet version 3. A port
supplies supervisor-only copy and provider-release callbacks: the latter must
release the provider record and host-table entry exactly once. Opaque host
handles are never worker-readable data. The worker copies then releases a
lease with `take_script_task_service_payload()`.
`script_task_worker_inbox.*` is the worker-local receiver for input and
completion values; a private-realm sink never receives a host or UI pointer.

The bridge is the sole `AppRuntimeHost` completion consumer during a script session. Its required
shutdown order is: `ScriptTaskSupervisor::begin_teardown`, bridge pending-job cancellation, host App
termination, bridge record retirement, then `ScriptTaskSupervisor::complete_teardown`. This preserves
late-completion handle release without sending stale data to the worker.

`src/script/script_task_worker_runtime.*` now provides the first worker-side
DOM/display-list producer slice. It owns the parsed document, same-thread
JerryScript binding, render/layout/layer trees and `InputController` inside one
worker object, and publishes only sealed value frames after value input or a
worker-local timer/animation callback. Node destruction observers are
composable, interaction state is rebound to replacement layer trees, and event
dispatch reports target destruction before default actions can use the old node.
The first worker-local JS gateway now exposes constrained
`services.request(kind, callback, options)` and `services.cancel(requestId)`:
request metadata and cancellation identity are scalar-only, and completion
payload bytes are copied from a sealed lease before the callback runs. A
request option named `inputHandle` identifies an optional supervisor-owned
input resource; it is not a completion resource. Completion results remain
behind a supervisor lease and are copied as bytes, never exposed to JS as a
host handle. A
successful cancel removes the worker-local callback only after its cancel packet
is accepted; the supervisor bridge then cancels queued work or retains an
in-flight tombstone for late cleanup. Provider policy, real RTOS task adapter
and fatal worker boundary remain separate follow-up work. Ports must not fill
those gaps with raw pointers.

`JerryScriptRuntime` also records the first exception or execution-budget
failure raised by a budgeted callback as a value-only `ScriptCallbackFailure`.
`ScriptTaskWorkerRuntime` consumes it after input, timer/animation, or service
completion dispatch and emits a bounded fatal record instead of publishing a
new frame. This is only the worker-local fatal-detection slice; RTOS task exit,
supervisor recovery and launcher handoff remain port acceptance work.
The worker-local object exposes an idempotent `ScriptTaskWorkerRuntime::stop()`
for its own exit boundary. It cannot invalidate the session or release
supervisor-owned frame/service leases; those actions remain in the documented
supervisor teardown order.
Fatal publication uses a separate fixed 40-byte `FatalRecord` value packet and
mailbox. It is intentionally independent from frame/input/service queues, and
the worker may retry `publish_fatal()` after bounded mailbox backpressure; a
successful publication cannot be duplicated.
