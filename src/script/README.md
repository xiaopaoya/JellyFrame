# Script

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Optional JerryScript integration.

## Read By Task

- App-author JavaScript subset: `docs/scripting_scope.md`.
- Cross-task ownership and teardown: `docs/cross_task_ownership_contract.md`.
- Port integration sequence: `docs/script_task_port_integration_guide_zh.md`.
- Worker-local runtime tests and protocol codecs: `tests/README.md` and the
  sibling `tests/` directory.

This layer binds the documented JellyFrame DOM/event/timer/form subset to
JerryScript, including bounded inline style mutation, frame-snapshot geometry
and opt-in Canvas 2D V0.4. It is disabled unless
`JELLYFRAME_BUILD_SCRIPTING=ON` is set. RTOS script-App task isolation is a
separate optional module and additionally requires
`JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON`.
Product builds should use a JerryScript library built with `JERRY_VM_HALT=ON`
when script execution budgets are required.

`JerryScriptRuntime` is a same-thread binding: it directly owns JS wrappers for
its bound `Node` tree. An RTOS script worker must not pass that DOM, JerryScript
values, renderer objects or native pointers to the UI task. Real multi-task App
support uses the value-only session/frame/input/service/fatal protocol in
`docs/cross_task_ownership_contract.md` (Chinese:
`docs/cross_task_ownership_contract_zh.md`). The first platform-neutral worker
slice is `ScriptTaskWorkerRuntime`: it owns a private parsed document,
`JerryScriptRuntime`, render/layout/layer data and `InputController`, and
publishes only sealed `ScriptTaskAppFrame` values. Worker-local timer and
animation callbacks use the same private realm and publish mutated DOM through
the same sealed-frame path. The constrained
`services.request(kind, callback, options)` gateway accepts only scalar request
metadata. Its optional `inputHandle` identifies a supervisor-owned request
input; a completion's returned resource is never exposed as a host handle and
is copied into a bounded byte array before the JS callback runs.
`services.cancel(requestId)`
 posts a separate value-only cancellation packet; the supervisor bridge resolves
 queued versus in-flight cancellation and keeps late completion cleanup outside
 the worker realm. Provider policy, fatal worker boundaries, and the RTOS task
adapter remain separate follow-up work.

Worker-local callback exceptions and execution-budget interruptions are now
captured as bounded value status by `JerryScriptRuntime`; the worker converts
them to `ScriptTaskWorkerRuntimeFatalRecord` and stops publishing frames.
This is the platform-neutral fatal-detection slice, not the RTOS task exit or
launcher recovery boundary.

`ScriptTaskWorkerRuntime::stop()` is the explicit, idempotent worker-local
boundary. It destroys only the worker-owned realm, document and render state;
the port must still run supervisor session invalidation and lease/service
teardown around it.

`ScriptTaskWorkerRuntime::publish_fatal()` serializes the retained fatal record
into the supervisor's separate fixed-value fatal mailbox. Publication is
idempotent and can be retried after mailbox backpressure.

Keep this directory separate from `src/render_core` and `src/app_runtime` so
embedded builds can ship without JavaScript support.
