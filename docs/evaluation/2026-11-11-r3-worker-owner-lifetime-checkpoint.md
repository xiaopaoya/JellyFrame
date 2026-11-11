# R3 worker session owner lifetime checkpoint

The public worker is an eight-byte facade on x64. Its sole heap session State
contains the VM facade, DomOwner, complete pipeline, telemetry and supervisor
gateway context. Public operations capture an allocation-free intrusive pin
before calling State and never access the facade after forwarding. Native
backend operations and DOM Event completion records use the paired
ScriptRuntimeOptions owner hooks to pin the same State. No Node gains shared
ownership, and no pending raw document bind is retained.

Stop revokes initialization, new operation admission, mutation observer delivery,
service submission/cancellation and frame/fatal publication immediately. It asks
the backend to stop while retaining a temporary pin, so backend completion-pin
releases cannot drain the worker under request_stop. Pipeline and DOM destruction
follow VM destruction only after the final operation/completion owner pin leaves.
An explicit stop retains the bounded scalar State for existing telemetry/getters;
facade destruction releases it after its final pin. Repeated stop is harmless.
ActiveSupervisorScope and service/image resolver callback contexts reference State.

The optional mutation observer forwards the existing engine-neutral tooling hook.
It runs synchronously on the worker thread; its external user context must survive
the active call, including if the callback resets the worker's unique owner. A
State trampoline refuses further observer delivery after stop.

x64 Debug measurements (MSVC 19.41): State 976 bytes, facade 8 bytes,
operation pin 8 bytes. State includes the previous worker payload. The session
counter is size_t plus one owner-alive bool; its configured cap defaults to 64.
Worker options add a size_t cap and two observer pointers; backend owner hooks
add three pointers inside script options. Admission uses a comparison/increment,
no allocation. State allocation failure propagates before any VM/DOM is created;
no allocation-failure injection was added for that ordinary constructor failure.

Regression coverage exercises actual unique-owner reset and explicit repeated
stop during DOM listeners, timers, nested supervisor scopes and copied service
completion mutation. Stopped callbacks cannot publish old frames, and further
operations/gateways are refused. Zero capacity and backend pin exhaustion at
capacity one are refused. Existing frame-value, canonical-v10 resource, fatal and
inbox regressions remain in the combined script test executable.

This checkpoint does not establish BrowserApp/HWND/audio host lifetime safety.
Browser host ownership is external to the worker boundary. True JerryScript heap
exhaustion still requires the separate fatal/isolation contract.

## Local verification

Combined `jellyframe_script_tests` passed in both Debug and Release with the
joined Core source override at `E:/Workspaces/migration-followup/Core-c5-r3-joined-20261111`.
Build directories are `E:/Builds/r3-worker-owner-debug-20261111` and
`E:/Builds/r3-worker-owner-release-20261111`. Debug links the separately built
Debug Jerry libraries; Release links the baseline MinSizeRel release-CRT Jerry
libraries. Verification used interim backend owner-lifetime source files copied
from the parallel backend worktree; those files are excluded from this worker
commit and final joined verification must use the merged backend implementation.
No memory-safety instrumentation run was completed in this worker worktree.
