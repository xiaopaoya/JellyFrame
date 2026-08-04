# App Runtime

> Last updated: 2026-08-04; Applies to: 0.5.0-dev

`app_runtime` contains hardware-neutral helpers for installable JellyFrame apps.

It owns contracts and small bounded data structures for:

- App lifecycle state, active app instances and teardown cleanup.
- A small runtime-host state container that ties lifecycle, request/completion
  queues and host handles together without performing platform I/O.
- App-instance-scoped async requests and completions.
- Value-only script-task sessions, bounded mailboxes, sealed frame leases,
  cancellation tombstones, native-release intents and ordered supervisor
  teardown for future RTOS scripting.
- A supervisor-only `AppRuntimeHost` service bridge that maps script request
  tokens to host jobs and sends fixed-width, pointer-free completion packets.
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

`script_task_contract.*` does not execute JavaScript or render a frame. It is
the platform-neutral ownership boundary that a port must complete before it can
run a real script App outside the UI task. See
`../script/docs/cross_task_ownership_contract.md`.

`script_task_service_bridge.*` is the exclusive completion consumer while that
script session is active. A port must use its ordered teardown: invalidate the
supervisor session, cancel bridge requests, terminate the host app, then retire
bridge records and complete supervisor teardown. In-flight host work is kept
tracked until either a late completion releases its handle or host teardown
makes that completion stale.

The target name is `jellyframe_app_runtime`.
