# Optional Host Services Contract

This document makes image/audio/lightweight-video, network data requests,
semantic device data and installable app bundles concrete enough for board ports. It complements
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
completions, releases old host handles on app switch, exit or crash recovery,
records a stable teardown reason and filters stale completions at frame
boundaries. It does not own DOM, a JS runtime, framebuffers or platform threads.

`src/app_runtime/app_host.h` / `src/app_runtime/app_host.cpp` provide the
higher-level `AppRuntimeHost`: a bounded state container that keeps the lifecycle
controller, request queue, completion queue and host handle table together. It
offers fixed entry points for submitting jobs from the current app, allocating
current-app handles and pumping frame completions. It also exposes
`terminate_current(reason)` / `crash_current()` so hosts can apply the same
resource-release rule after user kills, watchdog interrupts, app load failures
or runtime failures. It still does not perform network, file, decode or flash
I/O; real work belongs to desktop shells, RTOS workers or board ports.

`src/app_runtime/app_service_worker.h` / `src/app_runtime/app_service_worker.cpp`
provide a tiny platform-neutral worker pump. `pump_app_host_service_worker(...)`
does not create a thread. It pops requests for one `HostServiceJobKind`, calls a
host-owned `AppHostServiceWorker`, normalizes the returned completion identity
and pushes it back to the UI completion queue. This gives real Wi-Fi, flash,
codec or package-install workers a shared boundary while keeping DOM, JS, layout
and framebuffer ownership on the UI task.

`src/app_runtime/app_services.h` / `src/app_runtime/app_services.cpp` provide
the first platform-neutral mocks: `NetworkFetchMock`, `ImageDecodeMock`,
`AppPrivateKvStorageMock` and `AudioCommandMock`. They exist for desktop
validation and end-to-end contract tests. They still do not access real
networking, filesystems, codecs, audio devices or flash; product hosts should
replace their worker implementation while keeping the same request/completion/
handle semantics.

The same files also provide the first manifest/profile gate:
`AppServiceManifestCapabilities`, `AppServiceHostProfile` and
`app_service_policies_for_app(...)`. A manifest request such as
`network.fetch`, `storage.kv` or `media.audio.mp3` is only an app request. It
becomes an enabled runtime policy only when the selected host/profile also
allows that service and provides bounded budgets. This keeps policy decisions
out of JS bindings and worker implementations.

`src/app_runtime/app_device_services.h` /
`src/app_runtime/app_device_services.cpp` provide the first semantic-device
service mocks: `AppSensorSampleMock` and `AppLocationSnapshotMock`. They define
the request/completion/handle shape for accelerometer, gyroscope, heart-rate,
ambient-light and location snapshots. Product hosts still decide which sensor,
bus, RTOS task or companion-phone service produces the data. Apps request
documented capabilities; they never receive raw GPIO/I2C/SPI/BLE/GPS handles.

`src/app_runtime/app_capability_broker.h` provides a more general broker for
standard and product-specific capability names. It classifies requested
capabilities as `granted`, `granted-product-specific`, `unsupported-by-host` or
`unknown-capability`. Known names cover current services plus future semantic
device channels such as `sensor.accelerometer`, `sensor.gyroscope`,
`sensor.heart-rate`, `sensor.ambient-light`, `location.position`,
`connectivity.status`, `connectivity.companion`, `media.microphone`,
`media.camera` and `media.video.input`. Product-specific names may be declared
in the manifest, but they are granted only when the host/profile explicitly
lists the same name. They must still go through bounded host services or
host-provided JS bindings; raw GPIO/I2C/SPI/radio handles are not exposed to
apps.

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
- `pump_app_host_service_worker(...)`: optional helper for real host workers
  that process one service kind at a bounded cadence and post normalized
  completions back to the UI queue.
- `pump_app_host_service_workers(...)`: optional helper for a static table of
  service workers. It pumps each non-null slot with its own fixed request budget
  and stops before popping more work when the UI completion queue is full.
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
- `AppSensorSampleMock` / `AppLocationSnapshotMock`: desktop/test mocks for
  semantic device data. They keep only small sample/snapshot records and return
  them to the UI task through host handles.

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
    SensorSample,
    LocationSnapshot,
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
  the system shell. New code should prefer `terminate_current(AppTeardownReason::LoadFailure)`
  when it can preserve the precise reason.

Suspend/resume policy:

- `suspend_current()` is a state transition, not teardown. It is for screen-off,
  background and low-power states where the app may later resume.
- `AppFramePolicy` is the first coded policy entry point: foreground +
  screen-on consumes input, timers, rAF and presentation normally; low-power +
  screen-on keeps input/timers/presentation but disables animation; screen-off
  or suspended state pauses foreground input, timers, rAF and presentation.
- While suspended, the host should stop foreground input, script timers,
  `requestAnimationFrame` callbacks and nonessential CSS animation sampling by
  setting the relevant frame-loop/animation budgets to zero or by not pumping
  those queues.
- Slow host jobs may continue or pause by product policy. Completions must stay
  tagged with the original `app_instance_id`; a host may cache business delivery
  until resume, but stale completions must never mutate a later app instance.
- On resume, schedule a repaint before the first interactive frame and re-emit
  small state snapshots such as network and visibility when product policy needs
  deterministic app state.
- `exit_current()` / `terminate_current(reason)` / `crash_current()` remain
  teardown boundaries: cancel old requests, discard completions, release handles
  and clear app-local resources. Stable reason names include `normal-exit`,
  `app-switch`, `user-kill`, `runtime-error`, `script-watchdog`,
  `budget-exceeded`, `load-failure` and `system-policy`; the crash-like reasons
  are `runtime-error`, `script-watchdog`, `budget-exceeded` and `load-failure`.

## Worker Pump Helper

Hosts may run workers in a desktop thread, an RTOS task, a cooperative loop or a
test harness. JellyFrame only requires the request/completion boundary:

```cpp
class AppHostServiceWorker {
public:
    virtual HostServiceCompletion process(const HostServiceRequest& request) = 0;
};

AppHostServiceWorkerPumpResult result =
    pump_app_host_service_worker(host,
                                 AppHostServiceWorkerPumpOptions{
                                     HostServiceJobKind::NetworkFetch,
                                     1,
                                 },
                                 network_worker);
```

Rules:

- Pump one service kind per worker so image, network, storage and audio queues
  cannot consume each other's jobs.
- Keep `max_requests` small. On MCU targets this is normally `1` per worker
  tick, or a product-specific fixed budget.
- The helper overwrites completion identity with the original request's
  `job_id`, `kind` and `app_instance_id`; stale-instance protection therefore
  remains in the UI completion pump.
- If the UI completion queue is full, the helper returns
  `completion_queue_full` before popping a request. The host should retry later
  instead of spinning.
- The worker implementation must not call DOM, JS, style, layout, render or
  framebuffer APIs. It returns a small completion and host-owned handles only.

Recommended port structure:

```text
UI/main task:
  1. Process input/timers/system events.
  2. Call pump_frame_completions(...), dispatching only accepted completions to
     the current app.
  3. Use dirty flags to decide whether layout/render/present is needed.

Network worker:
  pump_app_host_service_worker(host, { NetworkFetch, 1 }, network_worker)

Storage worker:
  pump_app_host_service_worker(host, { StorageKv, 1 }, storage_worker)

Audio worker:
  pump_app_host_service_worker(host, { AudioCommand, 1 }, audio_worker)

Sensor worker:
  pump_app_host_service_worker(host, { SensorSample, 1 }, sensor_worker)

Location worker:
  pump_app_host_service_worker(host, { LocationSnapshot, 1 }, location_worker)
```

For a cooperative MCU loop, the same policy can be expressed without dynamic
allocation:

```cpp
AppHostServiceWorkerSlot workers[] = {
    { HostServiceJobKind::NetworkFetch, 1, &network_worker },
    { HostServiceJobKind::StorageKv, 1, &storage_worker },
    { HostServiceJobKind::ImageDecode, 1, &image_worker },
    { HostServiceJobKind::AudioCommand, 1, &audio_worker },
    { HostServiceJobKind::SensorSample, 1, &sensor_worker },
    { HostServiceJobKind::LocationSnapshot, 1, &location_worker },
};

AppHostServiceWorkerGroupPumpResult pumped =
    pump_app_host_service_workers(host, workers, 6);
```

The group helper is only a scheduler convenience. It does not create threads,
does not retry indefinitely and does not run service code when the completion
queue is already full.

MCU ports do not need three literal threads. These can be RTOS tasks, event-loop
branches or one cooperative background loop. The important parts are:

- Slow services must not run synchronously on the UI/main task.
- If the completion queue is full, stop posting completions and retry after the
  UI task has consumed a frame's completions.
- Request-queue full, completion-queue full, timeout and capability-denied
  states should be visible in port logs or desktop diagnostics.
- Regression coverage includes a bounded mixed-worker soak for network, storage,
  image, audio, sensors, location and system events. This is the reference
  shape for RTOS ports: workers post small completions, the UI frame boundary
  consumes them and stale or released handles are collected by service
  lifecycle hooks.
- The Win32 shell frame-capture output now reports `host_completion_*`,
  `system_event_*`, `frame_policy_*` and `service_activity` summary counters;
  use those fields as a reference for port log shape.

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
  changes. It can also evict LRU ready surfaces by surface-count and
  decoded-byte budgets while protecting current display-list references.
- `AppImageSurfaceCache::handle_completion(...)` rejects stale app-instance
  completions when called directly. The normal `AppRuntimeHost` completion pump
  already filters stale completions and releases returned handles, but this
  extra check keeps host/debug code from accidentally attaching an old surface
  to a new app.
- `evict_unreferenced_with_result(...)` reports both released surfaces and
  stale cache entries dropped during budget cleanup. Hosts should log non-zero
  stale drops because they usually mean a surface handle was released outside
  the cache lifecycle.
- `classify_app_image_failure(...)` / `app_image_failure_detail(...)` classify
  request rejections and completion failures into stable reasons such as
  `capability-denied`, `resource-not-found`, `decode-budget-exceeded`,
  `surface-budget-exceeded` and `pending-budget`. Desktop tools and future
  serial/package diagnostics should consume this classification instead of
  showing a vague `failed` status only.
- `AppImageSurfaceCache::diagnostic_detail_for_url(...)` exposes a stable
  debug string with `src`, `state`, `reason`, `submit`, and optional
  `host`/`error`/`job`/`handle`/`bytes` fields. Tooling should use it after
  request rejection or decode completion so asynchronous failures are tied back
  to the cache entry that caused them.
- Successful completions return `HostServiceHandleKind::Surface` handles;
  `AppDecodedSurfaceRecord` stores width, height, stride, pixel format and
  optional raw pixels.
- `release_surface(...)` must be called by the UI/main task when a surface is no
  longer referenced so the record and host handle are released.
- If app switch/crash teardown already released the surface handle through
  `AppRuntimeHost`, the mock can call `collect_released_surfaces(...)` to drop
  stale service-side records. Real services should do the same in lifecycle
  hooks.
- Render core provides `ImageHandleResolver`, image display commands and
  `ImagePainter`. A host can map `<img src>` to decoded surface handles during
  layer-tree construction and paint them through the painter.
- The Win32 browser debug shell wires `/debug/icon.raw` and `/debug/photo.raw`
  raw RGB565 fixtures: the first paint submits a decode, the completion returns
  to the UI/main task, marks `DomDirtyPaint` and repaints. App-authored pages
  should use package-local standard paths.

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
- A full cache may reclaim surfaces not referenced by the current display list;
  if every ready surface is still visible, the cache may temporarily exceed the
  budget until a later frame can release one.
- On failure, keep the placeholder box and report diagnostics.
- The Win32 debug shell can load uncompressed 24/32-bit BMP resources from
  `.jfapp`/source packages to validate the resource-to-surface-handle path.
- The Win32 debug shell reports image request rejections and completion
  failures with the triggering `src`, stable failure reason, submit/host status
  and host error code.
- Render core passes `object-fit` and simple `object-position` to the host
  painter. The Win32 debug painter supports `fill`, `contain`, `cover`, `none`,
  `scale-down` and one/two-value keyword/percentage positioning. Complex
  four-value and length-offset positioning is deferred.
- PNG/JPEG/WebP and production MCU codecs are not wired yet.

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
- `Close` releases the `AudioStream` handle.
- `SetVolume` clamps the stored volume to 0-100.
- Natural end posts `Completed`.
- Errors post `Failed` and a host error code.

Rules:

- Current platform-neutral code provides `AudioCommandMock` for request/
  completion/handle validation. It does not play audio.
- Like the network, image and storage mocks, it exposes `complete_request(...)`
  for host workers that have already popped an `AudioCommand` request through
  `pump_app_host_service_worker(...)`; `complete_next(...)` remains the compact
  helper for tests or cooperative loops that do not use the generic pump.
- The Win32 shell provides `--audio-smoke` for local files or `--app`
  in-package `/audio/...` resources to validate the desktop host adapter. This
  remains a host validation path; it does not change the core or imply an MCU
  codec.
- `HostMediaCapabilities::max_audio_streams` is usually 1 on watches.
- App switches or lock-screen policy may stop or pause app audio.
- App teardown releases host handles; service implementations should also drop
  stale stream records through their own lifecycle hook, as the mock does with
  `collect_released_streams(...)`.
- `classify_app_audio_failure(...)` / `app_audio_failure_detail(...)` classify
  request rejection and completion failure into stable diagnostics:
  `capability-denied`, `invalid-source`, `source-not-found`, `invalid-handle`,
  `stream-budget-exceeded`, `command-timeout`, `command-cancelled` and related
  reasons.
- Audio workers do not call JS; they post events for the UI task to dispatch.

## Semantic Device Data Services

Use cases:

- Fitness, health, watchface and weather apps reading accelerometer, gyroscope,
  heart-rate, ambient-light or position snapshots.
- Product-specific sensors exposed through semantic capability names rather than
  raw hardware buses.
- Background sampling governed by `AppFramePolicy` /
  `AppBackgroundServicePolicy`, so the host can slow down or stop sampling in
  low-power, screen-off or suspended states.

Current platform-neutral code provides:

- `AppSensorSampleMock`: requests one small sensor sample through
  `HostServiceJobKind::SensorSample`. `AppSensorKind` covers `accelerometer`,
  `gyroscope`, `heart-rate` and `ambient-light`.
- `AppLocationSnapshotMock`: requests one location snapshot through
  `HostServiceJobKind::LocationSnapshot`.
- `HostServiceHandleKind::SensorSample` / `LocationSnapshot`: results are
  referenced by short handles. App/JS bindings should copy the needed fields on
  the UI task after completion pumping, then release the handle.
- `JerryScriptRuntime` exposes `navigator.geolocation.getCurrentPosition(...)`
  when a location service is bound. The binding copies one completed snapshot
  into the Web-shaped callback object and immediately releases the host handle.
  Sensor JavaScript APIs are still deferred.
- `app_sensor_sample_policy_from_service_policies(...)` and
  `app_location_snapshot_policy_from_service_policies(...)`: convert the merged
  manifest + host/profile gate into concrete service policies.
- `classify_app_device_failure(...)`: classifies request rejection and
  completion failures into stable diagnostics such as `capability-denied`,
  `sample-unavailable`, `record-budget-exceeded`, `handle-budget-exceeded`,
  `request-timeout` and `request-cancelled`.

Rules:

- Apps must declare `sensor.accelerometer`, `sensor.gyroscope`,
  `sensor.heart-rate`, `sensor.ambient-light` or `location.position` in the
  manifest, and the host/profile must also allow the same service.
- A sample/snapshot is discrete data, not an unbounded high-frequency stream. A
  real host may downsample, debounce or cache continuous hardware samples into
  the latest snapshot before returning it through the request/completion
  boundary.
- Workers must not call DOM/JS/layout/framebuffer APIs. Sensor interrupts,
  GPS/BLE/Wi-Fi positioning and companion-phone data must be normalized into a
  small completion or host handle.
- `max_sensor_sample_records` and `max_location_snapshot_records` limit
  unreleased records; the host handle byte budget is the second guard.
- App switch, exit and crash recovery release host handles. Real services
  should also collect stale records, matching the mock
  `collect_released_samples(...)` / `collect_released_snapshots(...)` paths.
- Low-power and screen-off behavior is a product policy. The conservative
  default is to stop background sensor sampling. Products that need step count,
  heart-rate or location in the background should continue only when manifest
  intent, user permission and system power policy all allow it.

## Background Service Activity Policy

`backgroundServices` in `jellyframe.app.json` is an intent declaration, not a
permission grant:

```json
{
  "backgroundServices": {
    "network": { "whileSuspended": true, "whileScreenOff": false },
    "audio": { "whileSuspended": true, "whileScreenOff": true },
    "sensors": { "whileSuspended": false, "whileScreenOff": false, "inLowPower": false }
  }
}
```

The host combines that intent with product policy, user settings and system
state, then feeds the result into `AppBackgroundServicePolicy`. The platform-
neutral helper `app_service_activity_policy_for(...)` returns:

- `network_fetch`: whether network service workers may accept new app fetches;
- `audio_playback`: whether app audio may keep playing;
- `sensor_sampling`: whether sensor sampling may continue;
- `should_pause_audio`: whether the shell should pause/stop active streams;
- `should_throttle_sensors`: whether sensor cadence should be reduced or
  stopped.

Defaults are intentionally conservative: foreground apps may use approved
services, but suspended apps and screen-off state pause background work unless
the host explicitly allows it. Low-power mode throttles sensors unless the host
sets `sensors_in_low_power`. Completions must still carry the original
`app_instance_id`; background work may finish after an app is no longer active,
but stale completions must not mutate a newer app instance.

## Lightweight Video/MJPEG/H.264 Experimental Service

This is experimental and should be treated as a frame provider, not a promised
`<video>` implementation or a required part of normal page layout.

Recommended scope:

- low-resolution MJPEG;
- low-resolution H.264 baseline frame decode only when the target profile
  explicitly enables it;
- RGB565 output;
- fixed or low fps, such as 10-15fps;
- drop frames rather than block UI.

Recommended request:

```cpp
enum class HostVideoCodecKind {
    Mjpeg,
    H264Baseline,
};

struct HostVideoFrameRequest {
    uint32_t app_instance_id;
    HostVideoCodecKind codec;
    const char* resource_path;
    HostPixelFormat output_format; // usually Rgb565
    uint16_t max_width;
    uint16_t max_height;
    uint8_t max_fps;
    uint32_t max_frame_bytes;
    uint32_t timeout_ms;
};
```

Recommended result handle points to a latest-frame surface:

```cpp
struct HostVideoFrame {
    uint16_t width;
    uint16_t height;
    uint16_t stride_pixels;
    HostPixelFormat pixel_format;
    const void* pixels;
    uint32_t pts_ms;
    bool dropped_previous;
};
```

Rules:

- H.264 is not in the default ESP32-S3 profile. The 2026-06-20 retest proves it
  can run under QEMU + Octal PSRAM, but the 320x192 baseline sample remains
  below real-time, so it is only an experimental profile.
- Video frame buffers are host-owned; UI references the latest frame handle.
- If `supports_h264` is false, packaging/installation tools should reject apps
  declaring H.264 or emit an explicit degradation diagnostic.
- If decode is backlogged, drop old frames.
- If dirty repaint cannot keep up, pause video or lower fps.
- H.264 decode, YUV-to-RGB565 conversion, PSRAM/cache details and task
  scheduling belong to the host; core receives only frame handles/completions,
  never compressed streams or large pixel buffers.

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
- If app switch/crash teardown already released the response handle through
  `AppRuntimeHost`, the mock can call `collect_released_responses(...)` to drop
  stale response records. Real services should do the same in lifecycle hooks.
- `classify_app_network_failure(...)` / `app_network_failure_detail(...)`
  classify request rejection and completion failure into stable diagnostics:
  `capability-denied`, `invalid-url`, `resource-not-found`, `offline`,
  `response-budget-exceeded`, `response-handle-budget-exceeded`,
  `request-timeout`, `request-cancelled` and related reasons.
- XHR remains Web-near and small: app JavaScript sees `error`, `timeout` or
  `abort`; the detailed reason is for CLI, Win32 diagnostics, serial logs and
  host validation.

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
Like the network and image mocks, it exposes `complete_request(...)` for real
host workers that have already popped a `StorageKv` request through
`pump_app_host_service_worker(...)`; `complete_next(...)` remains the compact
single-call helper for tests or cooperative loops that do not use the generic
worker pump.
If app switch/crash teardown already released a `StorageValue` handle through
`AppRuntimeHost`, the mock can call `collect_released_values(...)` to drop stale
value records. Real storage workers should do the same in lifecycle hooks.

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

Storage diagnostics:

- `classify_app_storage_failure(...)` / `app_storage_failure_detail(...)`
  classify async storage rejection and completion failure into stable reasons:
  `capability-denied`, `invalid-key`, `value-budget`, `quota-exceeded`,
  `not-found`, `handle-budget-exceeded`, `operation-timeout` and
  `operation-cancelled`.
- `classify_app_local_storage_failure(...)` maps the in-memory
  `AppLocalStorageShadow` status values into the same reason family.
- Mock completions use small conventional error codes for diagnostics only:
  `404` for missing keys/resources, `413` for single payload/value budget and
  `507` for quota or handle budget exhaustion.

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

Lifecycle policy:

- `AppStorageLifecyclePolicy` and `app_storage_lifecycle_decision_for(...)`
  describe the platform-neutral policy boundary for pending writes and
  persistent app data.
- Default behavior is conservative: normal app exit flushes pending writes;
  crashes and memory pressure drop pending writes; uninstall drops pending work
  and deletes persistent data; update replacement flushes pending writes and
  keeps data.
- `drop_pending_writes` always wins over `flush_pending_writes`. A host should
  never try to flush data from a crashed JS/DOM instance if product policy says
  to drop it.
- `AppPrivateKvStorageMock::drop_pending_app_instance(...)` and
  `drop_pending_app(...)` provide the desktop validation path. Real ports should
  implement equivalent worker-queue cancellation or journal discard.
- `AppPrivateKvStorageMock::flush_pending(...)` and
  `apply_app_storage_lifecycle_decision(...)` provide the first
  platform-neutral reference path. Hosts can flush pending writes in bounded
  frame/event slices and receive flushed, dropped, deleted and remaining-work
  counters. Real ports may reuse the same decision shape, but flash/NVS/
  filesystem writes still belong in host workers.
- Flush work must be a bounded host job. Do not block the UI/main task on flash,
  NVS or filesystem writes.

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

Push status:

```cpp
enum class AppSystemEventPushStatus {
    Accepted,
    EmptyInstance,
    QueueFull,
};
```

Rules:

- The host pushes events for the current app instance with `push_current(...)`;
  diagnostic paths should call `try_push_current(...)`, which distinguishes
  `empty-instance` from `queue-full`.
- The UI/main task consumes events with `pump_current(...)` at frame boundaries.
- `max_events_per_frame` bounds event work per frame.
- Events for stale app instances are consumed and dropped.
- The queue does not call JS, mutate DOM, read RTC/network/battery hardware or
  trigger layout by itself. Bindings decide how an accepted event becomes an app
  callback later.
- The current JerryScript binding maps network state to `navigator.onLine` and
  the `window` `online`/`offline` event subset, and maps visibility state to
  `document.hidden`, `document.visibilityState` and `document`
  `visibilitychange`. Battery JavaScript APIs remain out of V0.
- The Win32 debug shell can inject fake events with `Ctrl+F6`/`Ctrl+F7`/`Ctrl+F8`
  and through frame scripts (`network-online/offline`, `screen-visible/hidden`,
  `low-power-on/off`) for app testing; failed injection reports
  `system-event-rejected` diagnostics. Hardware ports should use the same queue
  from their own host state provider.

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
   uncompressed 24/32-bit BMP resources from `.jfapp`/source packages. General
   cache eviction, the `object-fit` subset and stable failure-reason
   diagnostics are wired; next work is production image codecs.
4. Add ESP32-S3 RGB565 small-image/MJPEG decode with strict size/concurrency
   caps after the desktop surface consumer path is stable.
5. Add product host-owned MP3 playback backends, still returning only handles
   and ended/error events to the UI task. The mock and tiny `Audio()` JS status
   event subset are already available for desktop validation.
6. Expose further user-facing JS APIs only after the lifetime boundary is
   stable. The asynchronous `XMLHttpRequest` GET V0 subset, tiny `Audio()`
   subset and `navigator.geolocation.getCurrentPosition(...)` subset are now
   exposed behind host bindings; `fetch()` and sensor JS APIs wait for bounded
   Promise/microtask support and stricter cadence policy. Let manifest/profile
   checks reject unsupported targets.

The point is to get lifetime and scheduling right first, then attach real
hardware capabilities gradually.
