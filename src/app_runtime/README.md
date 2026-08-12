# App Runtime

> Last updated: 2026-08-12; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

`app_runtime` contains hardware-neutral helpers for installable JellyFrame apps.

## Read By Task

- App lifecycle and install/update/delete behavior: `docs/app_lifecycle.md` and
  `docs/app_packaging.md`.
- Host service policy and data boundaries: `docs/host_optional_services.md` and
  `docs/runtime_data_api.md`.
- Authorized file access: `docs/authorized_file_broker.md`.
- Script-task sessions, leases and teardown: `../script/docs/cross_task_ownership_contract.md`.

This module defines contracts and bounded state. Real filesystem, network,
panel and RTOS work belongs to a host or port adapter.

It owns contracts and small bounded data structures for:

- App lifecycle state, active app instances and teardown cleanup.
- A small runtime-host state container that ties lifecycle, request/completion
  queues and host handles together without performing platform I/O.
- App-instance-scoped async requests and completions.
- Host-owned resource handles with generation checks.
- App lifecycle, package install/update/delete, bounded host compute jobs, network fetch, private storage,
  image/audio host-service mocks and system-event plumbing.
- Fixed-size host data snapshots for battery, weather, activity, location and
  sensor summaries, filtered by explicit access policy before any app-visible
  surface.
- Advisory app-load telemetry for host DVFS, shallow sleep, service backlog and
  animation frame-drop decisions.
- The `jellyframe_device_runtime_contracts` dependency for bounded app staging
  and `JFDP/1` framing. Its source files temporarily remain in this directory
  while the future Device OS ownership boundary is prepared. The target does
  not perform flash, transport, signature or registry I/O; ports and desktop
  hosts inject those adapters.

It may depend on `render_core` for shared host capability and budget types and
on `jellyframe_device_runtime_contracts` for D0 device contracts. It must not
depend on JerryScript directly, filesystem/network implementations, RTOS APIs
or platform drivers.

When both `JELLYFRAME_BUILD_SCRIPTING=ON` and
`JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME=ON`, the separate
`jellyframe_script_task_runtime` target provides value-only script-task
sessions, bounded mailboxes, sealed frame leases, cancellation tombstones,
native-release intents and the `AppRuntimeHost` service bridge. Keeping this
target off leaves its code and static storage out of an ordinary app-runtime
build. See `../script/docs/cross_task_ownership_contract.md`.

The optional supervisor serializes session/generation transitions, frame
sequence assignment and mailbox admission in short critical sections. It
returns session values by copy so a worker or UI task cannot retain a mutable
reference across teardown; no such lock exists in ordinary builds.

The same module's `script_task_frame_codec.*` serializes bounded `DisplayList`
snapshots and paint-ordered opaque input target keys. v1 remains the compact
legacy format; v2 additionally carries bounded hierarchical clip records and
parallel command clip indices. The session and frame sequence remain in the
surrounding sealed-frame lease packet.

`script_task_frame_renderer.*` is the host-side value-frame consumer. It maps
decoded v1/v2 values to the platform-neutral `SoftwareRasterizer`, preserving
rounded clip chains and dirty-region boundaries while rejecting malformed
indices or parent cycles. It does not rebuild DOM or `LayerNode` state and can
therefore be reused by the desktop shell and a port adapter independently of
the worker's private runtime.

For bounded v2 profiling, a host may supply `SoftwareRasterizerStatistics`
through `ScriptTaskFrameRendererOptions`. It records rounded clip-run command
counts, per-`DisplayCommandType` commands replayed into rounded temporary
surfaces and their overlapping candidate replay areas, masked temporary-surface
pixels, rectangular dirty fast paths, opaque
inner pixels copied directly from the temporary surface, source-over blended
pixels, full-coverage versus antialiased-corner pixel counts, and explicit
budget/allocation rejections without introducing a platform timer or allocator
dependency. A host may separately provide a monotonic microsecond callback in
`SoftwareRasterizerOptions::timing`; only then are temporary-surface
prepare/clear, per-command replay and rounded-coverage composition duration
counters populated. Ordinary builds neither call a clock nor retain timer state.
`ScriptTaskFrameRendererOptions::rasterizer_timing` forwards that value-only
callback to the renderer, so a port need not duplicate the v2 command loop.
The same statistics also distinguish full-coverage output rows, rows that are
wholly opaque, opaque span count, and antialiased-coverage rows. Those counters
need no timing callback and help a host attribute rounded composite work before
adding another paint fast path.

`script_task_input_codec.*` defines the bounded, versioned worker-inbox values
for pointer, wheel, key and text input. The worker validates and dispatches
those values against its own DOM and layer tree.
`script_task_input_dispatch.*` is the worker-only adapter to its private
`InputController`; it never exposes the event target outside that task.

`script_task_service_request_codec.*` defines a separate, fixed 20-byte
worker-to-supervisor request packet. The supervisor alone takes that mailbox
through `ScriptTaskServiceBridge::pump_service_requests()`; frame consumers
never compete with service consumers for outbound worker traffic. The result
has per-rejection counters suitable for supervisor diagnostics.
Host rejection is also queued as a terminal value completion, with normal
worker-inbox backpressure handling, so worker code does not silently wait for
a request the host refused.

The supervisor also owns a separate sealed service-payload lease registry.
Its session and byte limits are independent from AppFrame leases. It is the
only allowed destination for a future supervisor-side service gateway to copy
host result bytes; an opaque host handle is not worker-readable payload data.

`script_task_worker_inbox.*` is the worker-local receiver for normalized input
and decoded service completions. Its completion sink can bind a private realm,
but it never receives a host, supervisor, DOM or renderer pointer.

With that target enabled, `script_task_service_bridge.*` is the exclusive completion consumer while that
script session is active. A port must use its ordered teardown: invalidate the
supervisor session, cancel bridge requests, terminate the host app, then retire
bridge records and complete supervisor teardown. In-flight host work is kept
tracked until either a late completion releases its handle or host teardown
makes that completion stale.

The target name is `jellyframe_app_runtime`.
