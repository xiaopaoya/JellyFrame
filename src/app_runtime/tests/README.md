# App Runtime Tests

> Last updated: 2026-08-04; Applies to: 0.5.0-dev

These tests belong to `jellyframe_app_runtime`.

They cover platform-neutral app-runtime helpers such as bounded async queues,
completion events and host handle lifetimes. They should not depend on DOM,
layout, software rendering, JerryScript or real I/O.

When both scripting and the script-task runtime module are enabled,
`script_task_contract_tests.cpp` covers the value-only RTOS scripting boundary:
session generations, mailbox stale rejection, sealed frame leases, service
cancellation tombstones and idempotent native-release intents.

`script_task_service_bridge_tests.cpp` verifies host-job/token mapping,
fixed-width completion encoding, pending and late cancellation, opaque handle
release, worker-inbox backpressure and dedicated worker-to-supervisor service
request submission, supervisor rejection counters and terminal host-rejection
completion delivery.

`script_task_contract_tests.cpp` additionally verifies independent service
payload lease budgets and teardown release, before a service gateway binds any
host-specific response adapter.

`script_task_frame_codec_tests.cpp` verifies bounded AppFrame value encoding,
malformed-packet rejection, sealed frame leases and paint-ordered target-key
resolution.

`script_task_input_codec_tests.cpp` verifies normalized input encoding, input
budget enforcement and worker-inbox delivery.

`script_task_input_dispatch_tests.cpp` verifies worker-local pointer dispatch
and rejects malformed packets or undefined key values without DOM access.

`script_task_service_request_codec_tests.cpp` verifies fixed-width service
request values, malformed/budget rejection and isolation from frame traffic.

`script_task_worker_inbox_tests.cpp` verifies that the worker receives only
decoded input/completion values and rejects malformed completion packets.

`script_task_value_flow_tests.cpp` verifies the complete platform-neutral
service request -> completion -> worker -> sealed frame -> UI value flow.

CTest target: `jellyframe_app_runtime_tests`; the optional module uses
`jellyframe_script_task_runtime_tests`.
