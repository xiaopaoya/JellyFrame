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
POD paint commands, bounded text bytes, validated resource lease IDs and optional hit regions with numeric
`target_key` values. Raw normalized input is the authoritative first-version path and is hit-tested by the
worker against its private DOM/layer tree; a UI task may use accepted target regions only as an optimization.
The worker dispatches JS, mutates its DOM and publishes a new frame. Neither task reaches into the other's
DOM or renderer state. A sealed frame is immutable;
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

When both `JELLYFRAME_BUILD_SCRIPTING=ON` and `JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME=ON`, the separate
`jellyframe_script_task_runtime` target compiles `src/app_runtime/script_task_contract.*` and implements
and tests session generation/epoch validation,
fixed-slot value mailboxes, session-scoped sealed frame leases, cancellation tombstones with late-
completion classification, deduplicated native-release intents, and a two-stage `ScriptTaskSupervisor`
teardown that does not create a task or VM. `script_task_service_bridge.*` maps those tokens to
`AppRuntimeHost` jobs and serializes completion values into a fixed 24-byte packet. The optional target has no
JerryScript, RTOS, DOM or renderer dependency; it does not start a worker or paint an `AppFrame`.
`script_task_frame_codec.*` now encodes a bounded `DisplayList`, viewport and paint-ordered opaque input
target keys into a versioned value frame; session and sequence remain in the surrounding frame lease.
`make_script_task_app_frame()` flattens the worker-private `LayerNode` before copying this value frame.
`script_task_input_codec.*` provides versioned pointer, wheel, key and bounded text values for the worker inbox.

The bridge is the sole `AppRuntimeHost` completion consumer during a script session. Its required
shutdown order is: `ScriptTaskSupervisor::begin_teardown`, bridge pending-job cancellation, host App
termination, bridge record retirement, then `ScriptTaskSupervisor::complete_teardown`. This preserves
late-completion handle release without sending stale data to the worker.

The next slice is a worker-side DOM/display-list producer and a UI-task frame consumer that uses this codec,
then the port-specific RTOS adapter. Ports must not fill those gaps with raw pointers.
