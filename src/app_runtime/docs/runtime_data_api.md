# Runtime Data API Plan

This document defines the intended JavaScript-facing shape for optional runtime
data services. The APIs are not exposed yet. It exists so the C++ host-service
contract, Win32 shell and future JerryScript binding converge on one small,
bounded model.

## Principles

- No service performs work on the UI/main task.
- Every operation is tied to the active `app_instance_id`.
- Manifest capability and host/profile policy must both allow the service.
- Results are dispatched only after the UI/main task pumps accepted completions
  or system events.
- No Promise/microtask dependency in V0. Callback APIs are easier to bound on
  JerryScript and tiny RTOS hosts.
- Large data stays host-owned behind handles. JS receives small copied strings,
  status codes and short error names.
- Stale-instance completions release handles and never call app callbacks.

## Proposed Global Object

The binding should expose one small namespace:

```js
JellyFrame.fetchText(url, callback)
JellyFrame.storage.get(key, callback)
JellyFrame.storage.set(key, value, callback)
JellyFrame.storage.remove(key, callback)
JellyFrame.storage.clear(callback)
JellyFrame.system.on(type, callback)
JellyFrame.system.off(type, callback)
JellyFrame.system.snapshot()
```

`window.fetch`, synchronous `localStorage`, IndexedDB and browser storage events
remain out of scope.

## Network

`JellyFrame.fetchText(url, callback)` submits `HostServiceJobKind::NetworkFetch`.

Callback:

```js
function callback(error, response) {
  // error is null or { code, message }
  // response is { status, contentType, text }
}
```

Rules:

- Only GET is planned for V0.
- Remote HTML/CSS/script/image resources are still forbidden as page resources.
- URL length, response bytes, timeout and in-flight request count come from the
  merged `NetworkFetchPolicy`.
- Non-2xx HTTP status is not automatically a transport error; the host reports
  transport/TLS/DNS/timeout failures through `error`.

## App-Private KV Storage

Storage remains asynchronous and app-private:

```js
JellyFrame.storage.get("theme", function (error, value) {})
JellyFrame.storage.set("theme", "dark", function (error) {})
JellyFrame.storage.remove("theme", function (error) {})
JellyFrame.storage.clear(function (error) {})
```

Rules:

- Values are V0 strings or UTF-8 bytes converted to strings by the binding.
- Key length, single value bytes, item count and total bytes come from the
  merged `AppPrivateKvPolicy`.
- Missing keys should call back with `null` value and no fatal exception. The
  C++ completion may still use `Failed`; the binding maps that to a documented
  miss result.
- No synchronous `localStorage`; flash/NVS/filesystem writes must never block
  the UI task.

## System Events

System state is host-injected through `AppSystemEventQueue`.

```js
JellyFrame.system.on("battery", function (snapshot) {})
JellyFrame.system.on("network", function (snapshot) {})
JellyFrame.system.on("time", function (snapshot) {})
var snapshot = JellyFrame.system.snapshot()
```

Snapshot fields:

```js
{
  unixTimeMs,
  timezoneOffsetMinutes,
  batteryPercent,
  charging,
  networkOnline,
  screenOn,
  lowPowerMode
}
```

Rules:

- `snapshot()` returns the latest host-approved snapshot copied into the runtime
  binding, not a live hardware object.
- Event callbacks are budgeted per frame.
- App code cannot directly read RTC, Wi-Fi, battery gauge or power-management
  drivers.

## Error Names

The binding should map host status to stable small strings:

| Host status | JS error code |
| --- | --- |
| `Unsupported` | `unsupported` |
| `BudgetExceeded` | `budget-exceeded` |
| `Timeout` | `timeout` |
| `Cancelled` | `cancelled` |
| `Failed` | `failed` |

This mapping is intentionally small. Detailed platform error codes can remain
in diagnostics or optional `hostCode` fields during desktop debugging.
