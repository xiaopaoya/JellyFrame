# App Runtime

> Last updated: 2026-08-04; Applies to: 0.5.0-dev

`app_runtime` contains hardware-neutral helpers for installable JellyFrame apps.

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

It may depend on `render_core` for shared host capability and budget types.
It must not depend on JerryScript directly, filesystem/network implementations,
RTOS APIs or platform drivers.

When both `JELLYFRAME_BUILD_SCRIPTING=ON` and
`JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME=ON`, the separate
`jellyframe_script_task_runtime` target provides value-only script-task
sessions, bounded mailboxes, sealed frame leases, cancellation tombstones,
native-release intents and the `AppRuntimeHost` service bridge. Keeping this
target off leaves its code and static storage out of an ordinary app-runtime
build. See `../script/docs/cross_task_ownership_contract.md`.

The same module's `script_task_frame_codec.*` serializes bounded `DisplayList`
snapshots and paint-ordered opaque input target keys. The session and frame
sequence remain in the surrounding sealed-frame lease packet.

`script_task_input_codec.*` defines the bounded, versioned worker-inbox values
for pointer, wheel, key and text input. The worker validates and dispatches
those values against its own DOM and layer tree.
`script_task_input_dispatch.*` is the worker-only adapter to its private
`InputController`; it never exposes the event target outside that task.

`script_task_service_request_codec.*` defines a separate, fixed 20-byte
worker-to-supervisor request packet. The supervisor alone takes that mailbox
and calls `ScriptTaskServiceBridge::submit_packet()`; frame consumers never
compete with service consumers for outbound worker traffic.

With that target enabled, `script_task_service_bridge.*` is the exclusive completion consumer while that
script session is active. A port must use its ordered teardown: invalidate the
supervisor session, cancel bridge requests, terminate the host app, then retire
bridge records and complete supervisor teardown. In-flight host work is kept
tracked until either a late completion releases its handle or host teardown
makes that completion stale.

The target name is `jellyframe_app_runtime`.
