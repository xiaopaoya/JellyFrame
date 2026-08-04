# App Runtime Tests

> Last updated: 2026-08-04; Applies to: 0.5.0-dev

These tests belong to `jellyframe_app_runtime`.

They cover platform-neutral app-runtime helpers such as bounded async queues,
completion events and host handle lifetimes. They should not depend on DOM,
layout, software rendering, JerryScript or real I/O.

`script_task_contract_tests.cpp` covers the value-only RTOS scripting boundary:
session generations, mailbox stale rejection, sealed frame leases, service
cancellation tombstones and idempotent native-release intents.

CTest target: `jellyframe_app_runtime_tests`.
