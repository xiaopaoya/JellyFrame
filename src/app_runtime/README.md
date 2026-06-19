# App Runtime

`app_runtime` contains hardware-neutral helpers for installable JellyFrame apps.

It owns contracts and small bounded data structures for:

- App-instance-scoped async requests and completions.
- Host-owned resource handles with generation checks.
- Future app lifecycle, package install/update/delete, network fetch, private
  storage, media and system-event plumbing.

It may depend on `render_core` for shared host capability and budget types.
It must not depend on JerryScript directly, filesystem/network implementations,
RTOS APIs or platform drivers.

The target name is `jellyframe_app_runtime`.
