# Script

> Last updated: 2026-08-04; Applies to: 0.5.0-dev

Optional JerryScript integration.

This layer binds the documented JellyFrame DOM/event/timer/form subset to
JerryScript, including bounded inline style mutation, frame-snapshot geometry
and opt-in Canvas 2D V0.4. It is disabled unless
`JELLYFRAME_BUILD_SCRIPTING=ON` is set.
Product builds should use a JerryScript library built with `JERRY_VM_HALT=ON`
when script execution budgets are required.

`JerryScriptRuntime` is a same-thread binding: it directly owns JS wrappers for
its bound `Node` tree. An RTOS script worker must not pass that DOM, JerryScript
values, renderer objects or native pointers to the UI task. Real multi-task App
support requires the value-only session/frame/input/service/fatal protocol in
`docs/cross_task_ownership_contract.md` (Chinese:
`docs/cross_task_ownership_contract_zh.md`).

Keep this directory separate from `src/render_core` and `src/app_runtime` so
embedded builds can ship without JavaScript support.
