# App Runtime Tests

These tests belong to `jellyframe_app_runtime`.

They cover platform-neutral app-runtime helpers such as bounded async queues,
completion events and host handle lifetimes. They should not depend on DOM,
layout, software rendering, JerryScript or real I/O.

CTest target: `jellyframe_app_runtime_tests`.
