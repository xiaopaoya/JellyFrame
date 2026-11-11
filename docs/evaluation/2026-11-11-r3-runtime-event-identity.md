# R3 Runtime shared Event identity checkpoint

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Historical experimental checkpoint; current delivery policy and qualification are tracked separately in the migration execution board.

This checkpoint records an experimental Runtime integration of the Core R3
dispatch-completion API. It is not a production capability claim.

## Verification identity

- Runtime branch: `codex/r3-shared-event-identity`, based on
  `161adce8b5746f3681cb18ded98f7175d4819eb6`.
- Core source override: `E:\Workspaces\migration-followup\Core-r3-integrated`,
  commit `83e3381b9ad4ac594588b883de4051ec3456802a`, including the protected
  Node lifetime observer, dispatch completion and dispatch-failure APIs.
- Runtime-generated Core source manifest: SHA-256
  `4ace6b2b02da38dcab3d14bd804bf8bc42798ca6b9f8a16cdcdc2689dcd12b77`,
  145 source inputs, package `0.9.0-dev`, ABI 4, `source-override`,
  `lockEnforced=false`.
- Runtime's production package lock remains `0.8.0`, ABI 3, and was not
  edited. JerryScript is `b7069350c2e52e7dc721dfb75f067147bd79b39b`.
- Release build: `E:\Builds\r3-runtime-admission-release-20261111`, Visual
  Studio 2022 / MSVC 19.41, scripting and script-task Runtime enabled. Built
  `jellyframe_script_tests`, `jellyframe_desktop_shell` and
  `jellyframe_script_microbench`. The combined script and worker suite passes;
  the microbenchmark executes successfully. The desktop shell was compiled,
  not interactively exercised.
- Debug build: `E:\Builds\r3-runtime-admission-debug-20261111`, same source
  override and scripting/script-task profile. The combined script and worker
  suite passes with matching Debug JerryScript libraries freshly built in
  `E:\Builds\r3-runtime-admission-jerry-debug-20261111`. An earlier Debug run
  linked the available MinSizeRel engine and emitted CRT warning LNK4098; that
  run is superseded by the separate matched Debug build.
- Both final generated Core provenance files identify the hash/file count
  above; `git diff --check` passes. This checkpoint does not repeat or extend
  the Core-only qualification recorded in the Core repository.

## Implemented experiment

DOM `addEventListener` and property-handler bridges use one TLS-linked record
for the active Event/runtime pair. The record owns one JerryScript reference
and enrolls with Core completion. Later listeners receive copies of that same
object; Core completion snapshots `defaultPrevented`, clears the Event pointer,
marks the binding inactive and releases the root. Native getters expose the
live `currentTarget`, `eventPhase` and `defaultPrevented`. The current callback's
`this` wrapper is reused by the currentTarget getter during that callback.

Focused JS coverage checks identity and expandos across ancestor capture,
target capture, target listeners, property handlers and bubbling; transient and
final event state; sequential and nested distinct Events; cancellation;
throwing once-listener cleanup; stop-immediate cleanup; and retained Event
snapshots. Existing target-destruction tests remain in the Runtime script suite.

`ScriptRuntime::bind_document()` now returns an admission result. During script
execution, eval result/exception conversion, another bind operation or active
DOM Event completion records it returns false before clearing anything. It
retains no pending `Node*`. The host retains both document owners and retries
at an idle boundary; normal idle binding still succeeds. Worker startup,
desktop-shell startup and the script benchmark check refusal. This changes the
experimental Runtime C++ interface; external implementations must return bool.

`ScriptRuntimeOptions::max_active_event_dispatches` defaults to 32 and bounds
simultaneously enrolled shared DOM Event records. Reusing the same Event record
for another listener does not consume capacity. Cap refusal and recoverable
native `std::bad_alloc` during record/binding/wrapper construction record
`ScriptCallbackFailureStatus::ResourceLimit` without allocating a diagnostic
string. The worker consumes that status as its existing `BudgetExceeded`
terminal outcome and refuses later frame publication. Enrollment/property
construction errors use `Exception`. Core's `fail_event_dispatch()` stops the
remaining listeners and returns false even for noncancelable Events, while
`defaultPrevented` remains the user's cancellation state. There is no rollback
of already executed callbacks or DOM mutations. Script hosts must consume the
callback-failure status; successful surrounding eval alone is insufficient.
The desktop shell build checks interface compatibility only: its existing
callback-status recovery paths have not been extended by this increment.

Event fields and target/currentTarget wrapper staging use checked own-property
definitions, bypassing inherited setters during publication. No listener is
called with `undefined` or an incomplete Event. Listener callback retention now
starts before staging, and Node-binding allocation/ledger preparation precedes
publication so native allocation refusal cannot leave an untracked binding or
destroy observer. This candidate explicitly requires the Core completion and
failure feature macros; the unchanged production ABI3 lock is not a compatible
candidate build provider.

New regression coverage includes rebind attempts from eval, eval `toString()`,
DOM callbacks and a native listener after JS returned but before completion;
idle retry; cap zero with listener/property handlers and both cancelability
values; later native listener suppression; cap one with nested refusal, shared
outer identity and sequential reuse; three injected native allocation failures
followed by successful dispatch; inherited Event and Node-wrapper setters; and
worker budget-fatal publication without a later frame. Injection covers C++
record/Event-binding/Node-binding creation allocations, not JerryScript heap
exhaustion. The engine's real heap exhaustion follows its fatal OOM path.

Independent review found that the lazy `currentTarget` getter, reached from a
native listener evaluating a retained Event between JS callbacks, bypassed the
checked wrapper construction used for listener `this`. Both now share the same
checked/catching staging helper: own properties bypass inherited setters,
recoverable native allocation refusal records `ResourceLimit` and fails the
dispatch, and ordinary live/currently completed getters preserve wrapper/null
semantics. A focused regression covers removing and throwing prototype setters,
native binding-allocation refusal, later-listener suppression and null snapshots
after completion. The Debug injector skips MSVC STL iterator-proxy allocations
in noexcept default string constructors before getter entry; it still faults
the fallible native binding allocation. The combined script/worker suite passes
in the existing matched Debug and Release build trees after this correction.

## Reproduction

Release configuration (from this Runtime worktree after the development
entry script):

```powershell
cmake -S . -B E:/Builds/r3-runtime-admission-release-20261111 -G "Visual Studio 17 2022" -DJELLYFRAME_BUILD_SCRIPTING=ON -DJELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON -DJELLYFRAME_BUILD_EXAMPLES=ON -DJELLYFRAME_BUILD_BENCHMARKS=ON -DJELLYFRAME_RENDER_CORE_SOURCE_DIR=E:/Workspaces/migration-followup/Core-r3-integrated -DJERRYSCRIPT_ROOT=E:/Workspaces/JellyFrame/third_party/jerryscript
cmake --build E:/Builds/r3-runtime-admission-release-20261111 --config Release --target jellyframe_script_tests jellyframe_desktop_shell jellyframe_script_microbench --parallel 4
ctest --test-dir E:/Builds/r3-runtime-admission-release-20261111 -C Release -R '^jellyframe_script_tests$' --output-on-failure
```

For matched Debug, configure a separate JerryScript build with
`-DJERRY_CMDLINE=OFF -DJERRY_VM_HALT=ON '-DJERRY_PROFILE=es.next'`, then build
`jerry-core jerry-ext jerry-port --config Debug`. Configure the separate Runtime
Debug tree with scripting/script-task enabled, the same Core override, and
`JERRYSCRIPT_LIBRARIES` explicitly listing all three `lib/Debug/*.lib` files.
Build `jellyframe_script_tests --config Debug` and run the same CTest selector
with `-C Debug`. The final Debug build cache contains those exact library paths.

## Remaining ownership gates

Physical Runtime destruction remains synchronous and has no owner pin. Factory,
worker and browser use unique ownership; a caller resetting an owner or
destroying a stack Runtime from inside a callback still invalidates active C++
frames and Jerry roots. No deferred flag in the Runtime can repair that.
The concrete next design and required owner-reset tests are in
[the owner lifetime design](2026-11-11-r3-runtime-owner-lifetime-design.md).
There is no pending bind/stop API or physical teardown qualification in this
increment. General exceptions/allocations during arbitrary JS/native host work
remain distinct from the specifically tested Event-construction boundary.

Cross-callback Node identity remains out of scope, as do Window, XHR, audio and
service-event routes. This experiment does not qualify the full R3 Event/Node
identity contract.

No scripted device build, flash or physical runtime test is part of this host
checkpoint. The separately linked WS147 non-script profiles in
`E:\Experiments\r3-paired-link-20261111` do not qualify this scripted increment.
