# Optional Host Services Contract

This document makes image/audio/lightweight-video, network data requests and
installable app bundles concrete enough for board ports. It complements
`host_abstraction.md` and `embedded_hal_api.md`: those documents define
ownership and checklists; this one defines the V0 service shape.

All services are optional. `jellyframe_render_core` must not depend on ESP-IDF,
GMF, ADF, LVGL, sockets, TLS, filesystems or vendor SDK types. Real
implementations belong to desktop shells, RTOS hosts, system shells or board
ports.

The first platform-neutral helpers live in `src/app_runtime/host_services.h` /
`src/app_runtime/host_services.cpp`. They provide bounded request queues,
completion queues, a host handle table and basic status types. They do not
create threads, perform I/O or own platform resources.

`src/app_runtime/app_lifecycle.h` / `src/app_runtime/app_lifecycle.cpp` provide
the first app-instance lifecycle helper. It only assigns `app_instance_id`,
tracks foreground/suspended state, cancels old requests, discards old
completions, releases old host handles on app switch, exit or crash recovery, and filters stale
completions at frame boundaries. It does not own DOM, a JS runtime, framebuffers
or platform threads.

`src/app_runtime/app_host.h` / `src/app_runtime/app_host.cpp` provide the
higher-level `AppRuntimeHost`: a bounded state container that keeps the lifecycle
controller, request queue, completion queue and host handle table together. It
offers fixed entry points for submitting jobs from the current app, allocating
current-app handles and pumping frame completions. It also exposes
`crash_current()` so hosts can apply the same resource-release rule after app
load or runtime failures. It still does not perform network, file, decode or
flash I/O; real work belongs to desktop shells, RTOS workers or board ports.

`src/app_runtime/app_services.h` / `src/app_runtime/app_services.cpp` provide
the first platform-neutral mocks: `NetworkFetchMock` and
`AppPrivateKvStorageMock`. They exist for desktop validation and end-to-end
contract tests. They still do not access real networking, filesystems or flash;
product hosts should replace their worker implementation while keeping the same
request/completion/handle semantics.

The same files also provide the first manifest/profile gate:
`AppServiceManifestCapabilities`, `AppServiceHostProfile` and
`app_service_policies_for_app(...)`. A manifest request such as
`network.fetch` or `storage.kv` is only an app request. It becomes an enabled
runtime policy only when the selected host/profile also allows that service and
provides bounded budgets. This keeps policy decisions out of JS bindings and
worker implementations.

## Overall Model

JellyFrame has one UI owner:

- The UI/main task owns DOM, JerryScript, style, layout, layers, display lists
  and framebuffers.
- Worker tasks own decode, network, bundle installation and file I/O.
- Workers must not mutate DOM, run JavaScript, call layout/render or write the
  framebuffer.
- Workers post small completion events back to the UI queue.
- The UI/main task consumes completion events at frame boundaries within a
  configured budget, then dispatches DOM/JS events or marks dirty state.

Recommended port-side queues:

```text
request queue    UI/main -> worker
completion queue worker  -> UI/main
resource cache   host-owned surfaces/buffers/audio handles/bundles
```

`HostAsyncCapabilities::max_in_flight_jobs` caps pending jobs.
`HostAsyncCapabilities::max_completion_events_per_frame` caps completions
consumed per UI frame.

Current core helpers:

- `AppLifecycleController`: active app instance management, explicit
  suspend/resume and request/completion/handle teardown during launch/exit/crash.
- `AppRuntimeHost`: combined lifecycle, request/completion queue and handle
  table state for desktop shells and MCU hosts wiring optional services.
- `HostServiceRequestQueue`: bounded request queue with priority selection,
  pending-job cancellation and bulk cancellation by `app_instance_id`.
  Workers that own only one service kind should use kind-filtered popping so
  network, storage, image and media jobs cannot consume each other's requests.
- `HostServiceCompletionQueue`: bounded completion queue with per-frame pop
  limits and stale `app_instance_id` discarding.
- `HostHandleTable`: bounded host handle table with generation checks to reject
  stale handles after release, plus active-count and used-byte accounting.
- `AppSystemEventQueue`: bounded host-injected system status events, tagged with
  the active `app_instance_id` and consumed at frame boundaries.

## Generic Job Shape

Use monotonically increasing 32-bit job ids. They only need to be unique within
one boot session.

```cpp
enum class HostServiceJobKind {
    ImageDecode,
    AudioCommand,
    VideoFrameDecode,
    NetworkFetch,
    StorageKv,
    BundleInstall,
    BundleRemove,
};

enum class HostServiceStatus {
    Completed,
    Failed,
    Cancelled,
    Unsupported,
    BudgetExceeded,
    Timeout,
};

struct HostServiceCompletion {
    uint32_t job_id;
    HostServiceJobKind kind;
    HostServiceStatus status;
    uint32_t app_instance_id;
    uint32_t handle;
    uint32_t error_code;
    uint32_t byte_count;
};
```

`app_instance_id` isolates old jobs after app switches, document teardown or
sleep. If a completion belongs to an inactive app instance, release the host
handle and skip DOM/JS callbacks.
`AppLifecycleController::pump_completions()` is the reference filtering policy:
current-instance completions enter the business handling list; stale completions
are consumed and their handles released so old network responses, image surfaces
or audio states cannot call into the new app.

`handle` is a small host-resource handle, not a raw pointer. It may identify a
decoded surface, audio stream, network response buffer or bundle staging record.

The Win32 reference shell follows the A4 rule set:

- package loading, JerryScript runtime ownership, timer pumping, input dispatch
  and completion pumping are bound to the active `app_instance_id`;
- stale or non-foreground instances do not receive input or script timer pumps;
- app rebuild/load failures call `AppRuntimeHost::crash_current()` and return to
  the system shell.

## Image Decode Service

Use cases:

- future `<img>`;
- future local CSS `background-image`;
- app icons for previews and app managers.

Input should come from local packages, installed bundles or system resources.
Remote images still do not enter the page loader.

Current V0 helper:

- `ImageDecodePolicy` gates the service and carries URL, width, height, decoded
  byte and pending-decode budgets.
- `ImageDecodeMock` provides desktop/test raw-surface fixtures through
  `HostServiceJobKind::ImageDecode` requests and completions.
- `AppImageSurfaceCache` turns URLs into bounded decode requests, records ready
  `Surface` handles after completions and releases surfaces when a page/instance
  changes.
- Successful completions return `HostServiceHandleKind::Surface` handles;
  `AppDecodedSurfaceRecord` stores width, height, stride, pixel format and
  optional raw pixels.
- `release_surface(...)` must be called by the UI/main task when a surface is no
  longer referenced so the record and host handle are released.
- Render core provides `ImageHandleResolver`, image display commands and
  `ImagePainter`. A host can map `<img src>` to decoded surface handles during
  layer-tree construction and paint them through the painter.
- The Win32 browser debug shell wires `app://icon` / `app://photo` raw RGB565
  fixtures: the first paint submits a decode, the completion returns to the
  UI/main task, marks `DomDirtyPaint` and repaints.

Future host requests can map to:

```cpp
struct HostImageDecodeRequest {
    uint32_t app_instance_id;
    const char* resource_path;
    HostPixelFormat output_format; // usually Rgb565 or Rgba8888
    uint16_t max_width;
    uint16_t max_height;
    uint32_t max_decoded_bytes;
};
```

The result handle points to:

```cpp
struct HostDecodedSurface {
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    HostPixelFormat pixel_format;
    const void* pixels;
};
```

Rules:

- Resize or reject large images during packaging; do not gamble at runtime.
- Prefer the format needed by the screen or compositor. ESP32-S3 should prefer
  RGB565.
- Decoded surfaces are host-cache owned; UI only references handles.
- A full cache may reclaim surfaces not referenced by the current display list.
- On failure, keep the placeholder box and report diagnostics.
- The Win32 debug shell can load uncompressed 24/32-bit BMP resources from
  `.jfapp`/source packages to validate the resource-to-surface-handle path.
- PNG/JPEG/WebP, general cache eviction, `object-fit` and production MCU codecs
  are not wired yet.

## Audio Playback Service

Use cases: tones, alarms, button feedback and short voice prompts.

Core should not receive PCM. The host owns codecs, I2S, GMF/ADF pipelines and
audio tasks.

Recommended commands:

```cpp
enum class HostAudioCommandKind {
    Open,
    Play,
    Pause,
    Stop,
    Close,
    SetVolume,
};

struct HostAudioCommandRequest {
    uint32_t app_instance_id;
    HostAudioCommandKind command;
    uint32_t audio_handle;
    const char* resource_path;
    uint8_t volume;
};
```

Completion behavior:

- `Open` returns an `audio_handle`.
- `Play`, `Pause` and `Stop` return state.
- Natural end posts `Completed`.
- Errors post `Failed` and a host error code.

Rules:

- `HostMediaCapabilities::max_audio_streams` is usually 1 on watches.
- App switches or lock-screen policy may stop or pause app audio.
- Audio workers do not call JS; they post events for the UI task to dispatch.

## Lightweight Video/MJPEG Service

This is experimental and should be treated as a frame provider, not a promised
`<video>` implementation.

Recommended scope:

- low-resolution MJPEG;
- RGB565 output;
- fixed or low fps, such as 10-15fps;
- drop frames rather than block UI.

Rules:

- H.264 is not in the default ESP32-S3 profile.
- Video frame buffers are host-owned; UI references the latest frame handle.
- If decode is backlogged, drop old frames.
- If dirty repaint cannot keep up, pause video or lower fps.

## Network Data Service

Network is for runtime data APIs only, not page resource loading.

Current V0 mock: `NetworkFetchMock` simulates `network.fetch` from fixed
fixtures. It submits `HostServiceJobKind::NetworkFetch` requests and returns
`FetchResponse` handles in completions. It checks capability policy, URL length,
response byte budget and request-queue capacity. The response body is owned by
the mock; the UI/main task can only inspect it through a handle and explicitly
release it.

`AppXmlHttpRequest` maps this service into a platform-neutral asynchronous XHR
V0 state machine: `open("GET", url, true)`, `send()`, `abort()`,
`readyState`, `status`, `responseText`, `responseURL`, content type and the
standard event sequence used by the JerryScript binding.

Policy gate:

```cpp
AppServiceHostProfile profile =
    app_service_host_profile_from_capabilities(device_capabilities, storage_policy);
AppServicePolicies policies =
    app_service_policies_for_app(manifest_capabilities, profile);
NetworkFetchMock network(policies.network);
```

`policies.network.enabled` is true only when both the app manifest requested
`network.fetch` and the host profile allows bounded fetch jobs.

Recommended request:

```cpp
enum class HostFetchMethod {
    Get,
    Post,
};

struct HostFetchRequest {
    uint32_t app_instance_id;
    HostFetchMethod method;
    const char* url;
    const uint8_t* body;
    uint32_t body_size;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
};
```

Recommended result handle points to a bounded response:

```cpp
struct HostFetchResponse {
    uint16_t status_code;
    const char* content_type;
    const uint8_t* bytes;
    uint32_t byte_count;
};
```

Rules:

- Default `network.allows_remote_page_resources = false`.
- No remote HTML/CSS/script/image loading through the page loader.
- Target profiles may restrict domains, schemes, concurrency, response bytes
  and timeouts.
- TLS, DNS, retry and cache policy belong to the host.
- Response buffers should not become long-lived large JavaScript objects; JS
  bindings should copy small data or expose bounded reads.

## App Private KV Storage Service

Storage is for small app-private data only. It is not browser-style persistent
synchronous `localStorage`, cookies, IndexedDB, Cache API or a general
filesystem. The goal is to support settings, tokens, small JSON state and
offline-cache indexes for embedded apps.

Current V0 mock: `AppPrivateKvStorageMock` isolates namespaces by app id and
completes `get/set/remove/clear` asynchronously through
`HostServiceJobKind::StorageKv`. Successful `get` operations return
`StorageValue` handles; `set`, `remove` and `clear` return status only. The mock
checks key length, single-value size, per-app item count and total byte budget.

Policy gate: `policies.storage.enabled` is true only when the app manifest
requested `storage.kv` and the host profile supplies an enabled
`AppPrivateKvPolicy`. The resulting key/value/item/byte budgets are copied into
the concrete storage policy used by the mock or product worker.

`AppLocalStorageShadow` is a small in-memory helper for the standard
`localStorage` V0 subset. It uses the same `AppPrivateKvPolicy` limits, stores
string keys/values in a compact sequential table and performs no host I/O. The
current JerryScript binding exposes `localStorage` only when the host binds this
non-blocking shadow; persistence, recovery and flush/drop policy remain
host-owned async storage work.

Recommended namespace:

```text
app_id -> key -> bounded bytes
```

Recommended commands:

```cpp
enum class HostStorageCommandKind {
    Get,
    Set,
    Remove,
    Clear,
    Keys,
};

struct HostStorageRequest {
    uint32_t app_instance_id;
    HostStorageCommandKind command;
    const char* app_id;
    const char* key;
    const uint8_t* value;
    uint32_t value_size;
    uint32_t max_response_bytes;
    uint32_t timeout_ms;
};
```

Recommended result handle points to a short-lived host-owned response:

```cpp
struct HostStorageResponse {
    const uint8_t* bytes;
    uint32_t byte_count;
};
```

Rules:

- All operations complete asynchronously through UI/main-task completion events.
- Each app has an isolated namespace and cannot enumerate or read another app's
  data.
- Key length, single-value size, total byte quota, item count and completions
  per frame must be bounded.
- `Set` should use host-side atomic write or journaling when possible so power
  loss does not corrupt existing keys.
- On app deletion, system policy decides whether private storage is erased or
  retained as user data; the desktop mock should provide explicit cleanup.
- Finish quotas, error codes, crash recovery and tests before exposing JS.
  User-facing storage should be a tiny `localStorage` subset only when the host
  can keep calls non-blocking through an app-private RAM shadow; otherwise keep
  storage unexposed.

## Bundle Installation Service

Installable third-party apps are managed by the system shell/app manager.

Recommended install flow:

1. Download or receive `.jfapp` into staging.
2. Validate header, manifest summary, target device, version, budgets and
   hash/signature.
3. Validate sorted resource index, offsets, sizes, normalized paths and total
   size.
4. Write the bundle store.
5. Atomically commit the installed-app registry.
6. Post a completion event so the launcher refreshes its app list.

Recommended registry fields:

```text
app_id
version_name / version_code
display_name
icon_resource_path
permissions / capabilities
bundle_offset
bundle_size
validation_state
```

Rules:

- Active app JavaScript cannot directly install and mount a new bundle.
- Failed installs discard staging and must not corrupt the committed registry.
- Updates write the new bundle fully before atomically switching the registry.
- Deleting the active app should first switch back to the system shell.

## System Data Events

System data events provide small host-approved status snapshots to apps without
placing hardware APIs in the core. The first platform-neutral helper is
`AppSystemEventQueue` in `src/app_runtime/system_events.h`.

Supported event kinds:

```cpp
enum class AppSystemEventKind {
    TimeChanged,
    TimezoneChanged,
    NetworkStatusChanged,
    BatteryChanged,
    ScreenStateChanged,
    LowPowerModeChanged,
};
```

The snapshot is deliberately tiny and copyable:

```cpp
struct AppSystemStateSnapshot {
    uint64_t unix_time_ms;
    int16_t timezone_offset_minutes;
    uint8_t battery_percent;
    bool charging;
    bool network_online;
    bool screen_on;
    bool low_power_mode;
};
```

Rules:

- The host pushes events for the current app instance with `push_current(...)`.
- The UI/main task consumes events with `pump_current(...)` at frame boundaries.
- `max_events_per_frame` bounds event work per frame.
- Events for stale app instances are consumed and dropped.
- The queue does not call JS, mutate DOM, read RTC/network/battery hardware or
  trigger layout by itself. Bindings decide how an accepted event becomes an app
  callback later.

## Implementation Order

Recommended order:

1. Implement generic request/completion queues and `app_instance_id` isolation.
   The first core helper implementation is complete.
2. The desktop bundle staging/registry mock is implemented. Use
   `jellyframe_cli.py registry` to install, list, resolve and remove `.jfapp`
   bundles with an atomically committed installed-app registry JSON.
3. The first image-decode mock/raw-surface fixture, `AppImageSurfaceCache` and
   render-core image display command passes are implemented. The Win32 debug
   shell can automatically submit mock decodes and repaint, and can load
   uncompressed 24/32-bit BMP resources from `.jfapp`/source packages. Next,
   wire general cache eviction, `object-fit` and production image codecs.
4. Add ESP32-S3 RGB565 small-image/MJPEG decode with strict size/concurrency
   caps after the desktop surface consumer path is stable.
5. Add host-owned MP3 playback, returning only handles and ended/error events.
6. Expose user-facing JS APIs only after the lifetime boundary is stable. The
   asynchronous `XMLHttpRequest` GET V0 subset is now exposed; `fetch()` waits
   for bounded Promise/microtask support. Let manifest/profile checks reject
   unsupported targets.

The point is to get lifetime and scheduling right first, then attach real
hardware capabilities gradually.
