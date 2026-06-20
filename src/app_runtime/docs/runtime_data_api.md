# Runtime Data API Plan

This document defines how optional runtime data services are exposed to
JavaScript. User-facing syntax should stay a documented subset of Web platform
APIs whenever practical, so app authors do not need to learn JellyFrame-only
data APIs.

## Principles

- Prefer standard names, object shapes and event names.
- If a standard API cannot be honored predictably, do not expose it yet.
- JellyFrame-specific C++ helpers may exist internally, but app JavaScript
  should not depend on custom `JellyFrame.*` data APIs for common web concepts.
- No service performs slow work on the UI/main task.
- Every operation is tied to the active `app_instance_id`.
- Manifest capability and host/profile policy must both allow the service.
- Results are dispatched only after the UI/main task pumps accepted completions
  or system events.
- Large data stays host-owned behind handles. JS receives bounded copied
  strings, status codes and short error names.
- Stale-instance completions release handles and never call app callbacks.

## Network

Preferred V0 user-facing API: an asynchronous `XMLHttpRequest` subset.

`fetch()` should wait until JellyFrame has a bounded Promise/microtask story.
A custom callback helper would be easier to implement, but it would create
non-standard authoring habits and is therefore rejected.

Current implementation: `AppXmlHttpRequest` provides the V0 XHR state machine
over `NetworkFetchMock`/host completions. `JerryScriptRuntime` exposes an
`XMLHttpRequest` constructor and calls JavaScript callbacks only after the
UI/main task pumps accepted completions. The Win32 browser shell binds a debug
network mock in scripting builds so desktop validation can exercise the same
host-completion path.

Current XHR subset:

```js
var xhr = new XMLHttpRequest();
xhr.open("GET", "https://api.example.com/weather", true);
xhr.onload = function () {
  document.getElementById("status").textContent = xhr.status + ":" + xhr.responseText;
};
xhr.onerror = function () {};
xhr.ontimeout = function () {};
xhr.send();
```

Supported V0 surface should be limited to:

- `new XMLHttpRequest()`
- `open(method, url, async)` with async `GET` only
- `send()`
- `abort()`
- `readyState`
- `status`
- `responseText`
- `responseURL`
- `onreadystatechange`
- `onload`
- `onerror`
- `ontimeout`
- `onabort`
- `onloadend`

Rules:

- `network.fetch` remains the manifest capability name for the host service,
  but the JS authoring API should be XHR first.
- Remote HTML/CSS/script/image resources remain forbidden as page resources.
- URL length, response bytes, timeout and in-flight request count come from the
  merged `NetworkFetchPolicy`.
- Non-2xx HTTP status is not automatically a transport error.
- `timeout` property, `getResponseHeader()`, POST, custom headers,
  credentials, redirects, streaming, binary response types and upload progress
  are deferred.

## App-Private Storage

Preferred V0 user-facing API: a tiny `localStorage` subset, but only if the host
can provide an app-private RAM shadow or otherwise guarantee that getters and
setters do not block on flash/filesystem I/O.

If that guarantee is unavailable for a target profile, storage should remain
unexposed rather than adding a custom async API.

Planned subset:

```js
localStorage.setItem("theme", "dark");
var theme = localStorage.getItem("theme");
localStorage.removeItem("theme");
localStorage.clear();
```

Supported V0 surface should be limited to:

- `localStorage.getItem(key)`
- `localStorage.setItem(key, value)`
- `localStorage.removeItem(key)`
- `localStorage.clear()`
- `localStorage.length`
- `localStorage.key(index)`

Rules:

- Values are strings, matching the Web Storage model.
- Storage is app-private; apps cannot access another app's namespace.
- Key length, value bytes, item count and total bytes come from the merged
  `AppPrivateKvPolicy`.
- Synchronous calls must hit a small in-memory shadow. Host flash/NVS/filesystem
  writes are scheduled through the async service path and reconciled by host
  policy.
- Quota failures should throw a small `QuotaExceededError`-like exception when
  possible; otherwise return a documented diagnostics warning during early
  bring-up.
- `sessionStorage`, storage events, IndexedDB, cookies and Cache API are not in
  V0.

## System State

System state should use existing web-adjacent concepts where they fit:

- `navigator.onLine`
- `window` `online` / `offline` events
- `document.hidden`
- `document.visibilityState`
- `document` `visibilitychange` event
- `pagehide` / `pageshow` for lifecycle-like transitions, if needed later

Battery and low-power state do not have a broadly safe modern baseline. The
Battery Status API exists historically but is privacy-sensitive and not a good
default. For V0, keep battery/charging/low-power snapshots in the C++ host event
queue and do not expose them to app JavaScript until a product profile explicitly
chooses a compatible surface.

The platform-neutral source remains `AppSystemEventQueue`. JS bindings should
map accepted events to the standard subset above when possible.

## Error Names

Internal host status can still map to stable small strings for diagnostics:

| Host status | Diagnostic code |
| --- | --- |
| `Unsupported` | `unsupported` |
| `BudgetExceeded` | `budget-exceeded` |
| `Timeout` | `timeout` |
| `Cancelled` | `cancelled` |
| `Failed` | `failed` |

Detailed platform error codes can remain in diagnostics or optional host debug
fields. They should not become required app-author syntax.
