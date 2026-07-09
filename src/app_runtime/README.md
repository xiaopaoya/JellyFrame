# App Runtime

> Last updated: 2026-07-07; Applies to: 0.5.0-dev

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

The target name is `jellyframe_app_runtime`.
