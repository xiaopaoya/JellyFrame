# App Runtime Benchmarks

Microbenchmarks in this directory measure app-runtime helpers that affect the
UI/main task: request queues, completion queues, host handle tables and
platform-neutral service mocks such as network, image, audio, storage and
system events.

Executable: `jellyframe_app_runtime_microbench`.

`app_runtime_stale_pending_cleanup` measures the lifecycle hook that drops
worker-popped pending network/image/audio/sensor/location requests after an app
switch or crash. It is intentionally outside the normal frame hot path; hosts
call it only when tearing down or switching app instances.
