# R3 Browser audio callback queue checkpoint

Last updated: 2026-11-11; Applies to: 0.6.0-dev

This is the original experimental checkpoint. The standalone mainline-based
delivery's fresh validation is recorded at the end of this document.

An audio ended JS listener can call Audio.play(), which appends to BrowserApp's
pending audio vector through ScriptAudioHost. The previous range-for held an
event reference and vector iteration state across that JS dispatch, then wrote
the audio ID through the reference. Callback appends could reallocate the vector.

BrowserApp now calls the small production pump in
`src/script/script_audio_event_queue.h`. It snapshots the initial vector size,
copies each due event value and marks that indexed entry consumed before JS.
It retains no vector iterator or element reference across dispatch. Appended
entries remain for the next host pump, and one final compaction preserves order.
The dispatch count is bounded by the initial queue length. The change adds zero
persistent state bytes and no allocation in the pump. It scans the initial batch
and compacts the resulting vector once.

The helper's caller keeps its vector/session alive and defers clear/rebuild
until the pump returns. Browser route review found ended callbacks can enqueue
audio or invoke existing trace/scroll hooks. Current rebuild, shutdown and shell
app transitions execute outside this synchronous audio dispatch. The helper
therefore follows that existing sequencing without adding another epoch or
owner control object. BrowserApp/HWND owner deletion from arbitrary external
callbacks retains its separate acceptance boundary.

## Regression and verification

The existing combined script test suite now exercises this exact production
helper using the real Jerry Audio binding and a ScriptAudioHost callback that
queues value events. An ended JS listener calls tone.play() 128 times. The queue
starts with capacity one so the callback grows its vector. The first pump must
deliver one event, already consumed before author JS, and retain all 128 appended
events. The next pump delivers those events exactly once. A due-time check also
keeps a future event while processing a due event. The host fixture handles
playback values in memory, so this regression needs no window, file or driver.

Temporarily restoring the original range-for/post-dispatch-write algorithm in
the extracted production pump reproduces an ASan heap-use-after-free, WRITE of
four bytes after the ended handler reallocates the vector. The reproducer exits
1. Its log is `E:/Builds/r3-browser-audio-asan-old-repro.log`. The fixed source was
restored before final builds and commit.

The complete combined backend/worker script suite passes with the fix in all
three fresh build trees:

- `E:/Builds/r3-browser-audio-debug-20261111`, MSVC x64 Debug with matching Debug
  Jerry libraries from `r3-runtime-admission-jerry-debug-20261111/lib/Debug`.
- `E:/Builds/r3-browser-audio-release-20261111`, MSVC x64 Release with existing
  MinSizeRel release-CRT Jerry libraries.
- `E:/Builds/r3-browser-audio-asan-20261111`, Clang 22.1.8 clang-cl/Ninja
  RelWithDebInfo, dynamic release CRT, instrumented Runtime/Core/app-runtime C++.
  The baseline Jerry C libraries are uninstrumented.

The desktop Browser shell compiles and links in Debug and Release. Logs are
`E:/Builds/r3-browser-audio-{debug,release,asan}-test.log` and their corresponding
configure/build logs. `git diff --check` passes.

The isolated Runtime worktree is
`E:/Workspaces/migration-followup/Runtime-r3-browser-audio-20261111`, based on
`28c4c5b2425970a5ac07ef89cf7ca42b04ef15eb`. All gates use Core
`15169c02b612d3a7548232c1bf58cc65a83742b5` at
`E:/Workspaces/migration-followup/Core-r3-submit-kind-20261111`, version
0.9.0-dev / ABI 4, source identity
`98f89f0fd300e631e701b301023e1510026145efc50eb3c76845843eb1c34086` / 146.

## Installed consumer staging correction

The helper lives under `src/script` and both Browser and script tests include
`script/script_audio_event_queue.h`. This keeps it available in the installed
v10 consumer's existing script-source staging. The earlier tools/native header
location made that real consumer fail to compile because its staged source tree
contains script and app-runtime inputs. One production helper now supplies both
callers through the existing script include path.

After the move, the affected combined script/worker suites pass again in Debug,
Release and ASan, and the desktop shell rebuilds in Debug and Release. Fresh
Release installed-package consumer builds also pass both value-codec and actual
Jerry worker tests in each profile (four tests total):

- `E:/Builds/r3-browser-audio-installed-full-20261111` uses profile
  `render-core-default` from `E:/Builds/r3-submit-kind-install-full-20261111`.
- `E:/Builds/r3-browser-audio-installed-cropped-20261111` uses profile
  `render-core-minimal-no-forms-advanced` from
  `E:/Builds/r3-submit-kind-install-cropped-20261111`.

Both installed packages retain Core 0.9.0-dev / ABI 4 and source identity
`98f89f0fd300e631e701b301023e1510026145efc50eb3c76845843eb1c34086` / 146.
The worker target compiles the staged script test including the real audio
queue regression. Consumer logs are
`E:/Builds/r3-browser-audio-installed-{full,cropped}-test.log`, with corresponding
configure/build logs. The header move changes its include location and leaves
the tested queue algorithm intact.

## Standalone delivery on current Runtime master

The queue helper and real Audio regression were replayed independently onto
Runtime `161adce8`, then integrated without conflict with `27f0503b` (the
separately merged JerryScript dependency repair). No R3 Event/owner or Node
identity implementation is included. The regression uses the current void
`bind_document` API rather than requiring the experimental boolean signature.

Fresh qualification uses the actual released Core 0.8.0 package, ABI3, source
`fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795` / 111,
with `lockEnforced=true`. MSVC Release desktop shell, combined script and
task-runtime test targets build; script, task-runtime, source-boundary and
documentation checks pass 4/4. This is a focused gate, not all desktop CTest.

A separate Clang ASan+UBSan build uses the same release Core source rebuilt
with matching sanitizer/standard-library annotations. Runtime/Core C++ and
JerryScript core/ext are instrumented; the platform port remains uninstrumented
and leak detection is disabled. The complete combined script/worker group
passes 1/1. This new source's temporary control restoring the old reference-
holding loop produces an actual ASan heap-use-after-free and exit1; restoring
the production helper rebuilds and passes. The control restores the original
header in a finally block, and the committed helper is the fixed version.

Builds: `E:/Builds/runtime-audio-delivery-{native,asan}-20261111`.
Reproduction runner and raw negative/restored logs:
`E:/Experiments/runtime-audio-delivery-20261111`. Fresh delivery checks do not
claim BrowserApp deletion safety or physical audio-driver behavior. Remote
full/cropped installed/staged workers remain covered by the PR's CI gate.
