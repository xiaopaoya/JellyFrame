# R3 joined backend and worker owner lifetime integration

## Source boundary

Fresh Runtime worktree: `E:/Workspaces/migration-followup/Runtime-r3-owner-joined-20261111`.
Runtime base `2850e819`; backend source commit
`ada09cdc3a6c333fc8775cba17e04af3fadb1a5d` cherry-picked as `1066dc37`;
worker source commit `7529fbfc` cherry-picked as `f1f80e44`.
The integration follow-up adds the initialization admission fix below and this
checkpoint. No Browser source, production package lock, canonical worktree or
frozen evidence was changed.

All builds use the in-tree Core source override
`E:/Workspaces/migration-followup/Core-c5-r3-joined-20261111` at
`c941f69a214eb38fafbabe95eba63e2f1faa17db` (Core 0.8.0 / ABI 4 experimental boundary).
This is source-override qualification, not a production installed package gate.

## Integration fix

Repeated worker initialization formerly replaced DomOwner before trying to
create another singleton Jerry backend; a mutation observer could also reenter
initialization while the original operation still used that document. Public
worker initialization now returns `Busy` during any active worker/backend
operation, and `AlreadyInitialized` for an idle initialized session, before
parsing, changing the supervisor, moving DOM ownership or touching the VM.
Stopped/Fatal precedence is preserved, and the supervisor overload's internal
State continuation remains admitted. Normal nested eval/supervisor operation
continues to work. The new enum values are appended so prior values remain stable.

A regression checks both initialize overloads repeatedly and from the mutation
observer, proves the original JS variable/document survives, and takes the
original mutated frame without a fatal result or frame loss. This retains the
explicit busy-refusal contract: no pending naked Node or second-document owner
is queued.

## Gates

The three CTest targets `jellyframe_render_core_tests`,
`jellyframe_script_tests` (backend + worker), and
`jellyframe_script_task_runtime_tests` all pass in each configuration:

- `E:/Builds/r3-owner-joined-debug-20261111`: MSVC 19.41 x64 Debug Runtime/Core,
  matching Debug Jerry libraries from
  `E:/Builds/r3-runtime-admission-jerry-debug-20261111/lib/Debug`.
- `E:/Builds/r3-owner-joined-release-20261111`: MSVC x64 Release Runtime/Core,
  release-CRT Jerry libraries from the existing MinSizeRel baseline at
  `E:/Workspaces/JellyFrame/third_party/jerryscript/build/lib/MinSizeRel`.
- `E:/Builds/r3-owner-joined-asan-20261111`: Clang 22.1.8 clang-cl/Ninja,
  RelWithDebInfo, `/fsanitize=address`, dynamic release CRT. Runtime, Core and
  app-runtime C++ are instrumented. The compatible baseline Jerry C libraries
  above are not instrumented. The ASan DLL directory is required on PATH:
  `C:/Program Files/LLVM/lib/clang/22/lib/windows`.

The Debug and Release desktop shell, script microbenchmark and pseudo browser
also compile/link against the new pure virtual ScriptRuntime stop methods.
These are compile gates only; no Browser UI interaction or host deletion gate
was performed. The only concrete ScriptRuntime implementations in this tree
are the Jerry facade and its backend State; both compile in all three gates.

Logs live under `E:/Builds/r3-owner-joined-{debug,release,asan}-ctest.log`,
corresponding `-configure.log` and `-build.log`, plus Debug/Release
`-consumers.log`. All three tests passed per configuration (nine passes total).
`git diff --check` passed. Core compiler warnings and unused-node/UNDEBUG
warnings do not change the passed test results.

## Lifetime sequence and limits

Worker public entries capture a bounded intrusive session pin, then forward to
State without later facade access. Backend public/native operation pins retain
the worker owner hook and backend shared State. DOM Event completion roots add
their own paired owner hook and backend State retention. On release, a record
frees/unlinks its Jerry root before dropping backend State, then releases the
worker pin; backend operation guards likewise release backend State before the
worker owner hook. Thus physical worker VM/DOM teardown waits for both active
calls and completion references. Stop revokes service requests, mutation
observer delivery and publication immediately, and retains a temporary worker
pin across backend request_stop to prevent a completion release from draining
under that stack. There is no persistent worker/backend owner cycle.

External owner callbacks must be paired, noexcept, and keep their context valid
through the final release. The worker supplies static State hooks; its optional
external mutation observer user context must independently survive its active
call, including owner reset. The observer is synchronous and checks logical stop.
The bounded counts are same-thread, not atomic cross-thread lifetime admission.

The earlier scoped checkpoints document exact owner-pin balances and worker
sizes. Combined regressions include real facade reset in native DOM listeners,
nested dispatch/supervisor scopes, timers/animation, service completions, and
explicit repeated stop, plus admission capacity refusal. State allocation failure
was not separately injected; State constructor allocation occurs before creating
VM/DOM and ordinary C++ allocation failure propagates. ASan establishes these C++
use-after-free paths only; it does not establish Jerry internal memory safety,
engine heap-OOM recovery, arbitrary borrowed-document ownership or BrowserApp,
HWND/audio host lifetime. Those external host lifetimes remain separately gated.
