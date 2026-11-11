# R3 backend facade lifetime checkpoint

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Historical experimental checkpoint; current delivery policy and qualification are tracked separately in the migration execution board.

This experimental backend increment separates `JerryScriptRuntime` (the public
facade) from `JerryScriptRuntimeState` (the surviving backend). It is scoped to
physical backend safety; borrowed documents and host resources still require
an enclosing owner/session lifetime contract.

The facade stores one shared State reference. Every forwarding entry copies
that reference into a bounded same-thread operation guard before reaching the
backend and never accesses the facade after forwarding begins. Native DOM
listener entry has the same guard; each Core completion record separately
retains State until its native Jerry root is freed and the record is unlinked.
Deleting a unique or stack facade requests logical stop and drops its reference.
The final State reference performs the existing cleanup once, after scopes and
completion roots have drained. Stop refuses new public entries, native JS
callback execution, service submissions and mutation notifications. Already
admitted JS may finish. Busy document bind still refuses without retaining a
raw pending Node pointer.

`ScriptRuntime` now requires `request_stop() noexcept` and
`stop_requested() const noexcept`. External alternate backend implementations
must implement these methods. Optional paired `ScriptRuntimeOptions` owner
hooks retain the enclosing session for admitted operations and completion
records. The backend releases its pin before calling `release_owner`; hosts
must supply matching callbacks and keep the borrowed context valid until the
last release. These hooks alone do not pin a BrowserApp, HWND or arbitrary
external document owner.

Resource costs are one make_shared State allocation/control block per runtime,
one facade shared reference, and stack-only operation guards. There is no
shared ownership per Node. `max_active_operations` defaults to 64 and bounds
simultaneous facade/native operation admissions. Completion roots retain the
existing independent `max_active_event_dispatches` bound (32 by default), with
one State shared reference and two owner-hook pointers added per record. The
State uses one stop flag and operation counter; logical drain is the lifetime
between stop and the final shared-reference release. Operation refusal returns
the existing empty/false result without admitting another VM frame.

Validation on Core source override
`E:\Workspaces\migration-followup\Core-c5-r3-joined-20261111`:

- Debug complete script suite passed in
  `E:\Builds\r3-backend-owner-debug-20261111`, MSVC 19.41 with matching Debug
  JerryScript libraries from `r3-runtime-admission-jerry-debug-20261111`.
- Release complete script suite passed in
  `E:\Builds\r3-backend-owner-release-20261111`, MSVC with existing MinSizeRel
  release JerryScript libraries.
- Clang 22.1.8 AddressSanitizer complete script suite passed in
  `E:\Builds\r3-backend-owner-asan-ninja-20261111`, RelWithDebInfo, dynamic release
  CRT. Runtime, Core and app-runtime C++ are instrumented. Compatible existing
  release JerryScript libraries are not instrumented; this gate verifies C++
  facade/State use-after-free paths, not engine-internal memory safety.
- New regressions reset a real `unique_ptr<ScriptRuntime>` from mutation hooks
  during eval, native DOM callbacks, timers, animation frames, service completion
  and nested dispatch. They check exact paired owner-pin release, stop admission,
  and fresh engine creation after the final completion/operation drain.
- `git diff --check` passed. ASan's DLL directory must be on PATH when running
  the instrumented executable (`C:\Program Files\LLVM\lib\clang\22\lib\windows`).

Worker/browser owner sessions and their native contexts require separate
integration and tests. This backend checkpoint does not qualify their teardown.
JerryScript heap exhaustion remains the engine fatal path; no engine OOM
recovery claim is made. No production package lock or frozen evidence changed.
