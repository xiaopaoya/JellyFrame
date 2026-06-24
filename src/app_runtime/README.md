# App Runtime

`app_runtime` contains hardware-neutral helpers for installable JellyFrame apps.

It owns contracts and small bounded data structures for:

- App lifecycle state, active app instances and teardown cleanup.
- A small runtime-host state container that ties lifecycle, request/completion
  queues and host handles together without performing platform I/O.
- App-instance-scoped async requests and completions.
- Host-owned resource handles with generation checks.
- App lifecycle, package install/update/delete, network fetch, private storage,
  image/audio host-service mocks and system-event plumbing.
- Advisory app-load telemetry for host DVFS, shallow sleep, service backlog and
  animation frame-drop decisions.

It may depend on `render_core` for shared host capability and budget types.
It must not depend on JerryScript directly, filesystem/network implementations,
RTOS APIs or platform drivers.

The target name is `jellyframe_app_runtime`.
