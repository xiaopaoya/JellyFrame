# App Lifecycle

> Last updated: 2026-08-04; Applies to: 0.5.0

This document is the app-author view of JellyFrame lifecycle behavior. It
summarizes what an app can rely on when it is installed, launched, suspended,
resumed, terminated or uninstalled. Host and board-port implementation details
live in `host_optional_services.md`.

## Model

JellyFrame runs one active app instance at a time in the current V0 runtime. A
launcher, watch face or settings page can also be a JellyFrame app, but product
hosts should treat those as trusted system roles. Third-party apps do not get
raw filesystem, network stack, GPIO, display, audio codec or sensor handles.

Each launch creates a new `app_instance_id`. Host requests, service completions,
system events, handles and app font resources are tied to that instance. When an
app exits, crashes, is killed by policy or is replaced by another app, stale
completions are discarded and handles are released before the next app can be
mutated.

## Installation And Removal

Installable apps are `.jfapp` bundles or source packages installed through the
desktop CLI/system shell path.

- `jellyframe_cli.py check` validates the manifest, resources, capability
  intent, fonts and render pipeline before installation.
- `jellyframe_cli.py install --root app_dir --store store_dir` stages the app,
  validates it, writes the registry atomically and lets the launcher refresh on
  a later frame.
- Failed installation must discard staging bytes and keep the previous registry.
- Uninstall deletes app-private data by default. Product shells may expose a
  user choice to keep data.
- Active-app deletion should first return to a trusted launcher/system shell.

JavaScript running inside a normal app cannot install, mount, delete or update
another app directly. Future system components can expose those flows through
trusted roles and host-owned brokers, not through a general browser API.

Trusted launcher/system-shell apps are regular JellyFrame packages with a system
role, for example `role: "launcher"` plus capabilities such as
`system.launcher` and `system.appManager`. Those capabilities are interpreted by
the host. They do not create a web API that ordinary third-party apps can call.
The Win32 sample launcher currently uses package HTML plus host-injected app
list markup; its `data-action` buttons are consumed only while the shell is in
system-shell mode. A normal installed app can use `data-*` attributes for its
own UI, but those attributes cannot launch, enable, disable, roll back or delete
other apps.

## Launch And Frame Loop

At launch, the host loads package resources, builds DOM/style/layout/layers, and
presents the first frame through the same render pipeline used by the Win32
debug shell. The app can then receive input, timers, animation frames and host
service completions according to the active frame policy.

This describes a same-thread host. A port that runs JerryScript in a distinct
RTOS task must not move its DOM, JavaScript wrappers, render objects or native
service pointers into the UI task. It must use the value-only session, frame,
input, service and teardown protocol in
`../../script/docs/cross_task_ownership_contract.md`; that protocol remains a
prerequisite for a real multi-task script App.

The host should pump each frame in this order:

1. Accept host completions and system events for the active instance.
2. Run bounded JavaScript callbacks and timers.
3. Apply DOM/style changes and recompute dirty regions.
4. Layout/paint only what is needed.
5. Present dirty rectangles or a full frame to the display backend.

Slow work such as network fetch, image decode, audio playback, file access and
bundle installation must run as host jobs outside the UI task. The completion
returns to the active instance on a later frame.

## Suspend, Resume And Visibility

Suspend is a reversible state change. It is used for app backgrounding,
screen-off and low-power policies. It is not the same as teardown.

JellyFrame maps the current V0 system state to a small web-like JavaScript
surface:

- `document.hidden`
- `document.visibilityState`
- `document.addEventListener("visibilitychange", ...)`
- `navigator.onLine`
- `window.addEventListener("online", ...)`
- `window.addEventListener("offline", ...)`

When suspended or screen-off, hosts should stop foreground input, timers,
`requestAnimationFrame` and presentation unless a product profile explicitly
keeps a service active. On resume, the host should schedule a repaint before the
first interactive frame and may inject fresh network/visibility snapshots.

Detailed low-power state remains a host policy input. An app may declare
`system.battery`, `system.weather` or `system.activity` and read a filtered,
one-shot value snapshot through `navigator.jellyframe.getSnapshot()` when the
host binds it. This is not the Battery Status API: no listener, polling,
subscription, raw device handle or write-back path is exposed.

## Runtime Services

Apps request services through manifest capabilities and the selected target
profile must also allow them.

- `compute.jobs` declares bounded named host work. It is a port/system contract only in V0: no JavaScript Worker, arbitrary background callback or message-port API is exposed.
- `network.fetch` enables the asynchronous `XMLHttpRequest` GET V0 subset.
- `storage.kv` enables the tiny `localStorage` subset only when the host binds a
  non-blocking app-private shadow.
- `media.audio.playback` enables the host-optional `Audio()` V0 subset.
- `location.position` enables `navigator.geolocation.getCurrentPosition(...)`
  when a host location service is bound.
- `system.battery`, `system.weather` and `system.activity` enable only their
  corresponding fields in `navigator.jellyframe.getSnapshot()` when the host
  binds approved data. Returned objects are detached values; changing them never
  changes host or system state.
- Sensor capability names express intent, but sensor JavaScript APIs are still
  deferred in V0.

Background service intent is declared in `backgroundServices`. It does not grant
permission by itself. The host combines manifest intent, user permission, target
profile and power state to decide whether network, audio, sensor or location
work may continue while suspended or screen-off.

## Storage Lifecycle

`localStorage.setItem(...)` updates the app-private RAM shadow. It does not mean
flash/NVS/filesystem persistence has completed. The host owns persistence and
uses the storage lifecycle policy:

- Normal exit/update should flush pending writes when possible.
- Crash, budget recovery and memory pressure may drop pending writes.
- Uninstall drops pending work and deletes persistent data by default.
- Hosts should prefer predictable recovery over blocking the UI task on flash.

Apps must be written as if persistence can fail. Use package diagnostics and
Win32 shell output to inspect quota and lifecycle failures.

## Failure And Recovery

The runtime can terminate the active app with stable reasons such as:

- `user-kill`
- `script-watchdog`
- `budget-exceeded`
- `load-failure`
- `system-policy`

When the Win32 system shell terminates an installed app for `script-watchdog`
or `budget-exceeded`, it records the app as failed before returning to the
launcher. The registry detail is capped, so a bad app cannot turn an exception
message into unbounded shell metadata. Users can inspect, re-enable, roll back
or remove the package through the app-manager flow.

Termination cancels current requests, discards stale completions, releases host
handles and app font resources, and returns to a trusted launcher/system shell.
The app may fail; the runtime, launcher and other apps must keep running. Any
operation that does not modify firmware should have a fallback that does not
require reflashing the device.

## Win32 Debug Flow

For app authors, the Win32 shell is the preferred interactive debug host:

```powershell
python tools\jellyframe_cli.py doctor --build-dir build\Release
.\build\Release\jellyframe_win32_browser.exe --app samples\apps\packages\watch_weather
```

Useful lifecycle checks:

- `Ctrl+F6`: toggle network online/offline.
- `Ctrl+F7`: toggle screen visibility.
- `Ctrl+F8`: toggle low-power visibility.
- `--system-survival-smoke N`: repeatedly launch a bad app and verify launcher
  recovery.
- `--registry-store build\installed_apps`: use the installed-app registry and
  sample launcher path.

Frame scripts can inject the same network/visibility/low-power events for
deterministic CI captures.
