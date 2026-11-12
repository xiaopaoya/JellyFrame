# Browser callback failure delivery

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Based on Runtime PR56/master26e94a95, this slice extracts the host callback
failure boundaries from32229998. Input, host completion, system event, timer,
audio and animation operations consume the failure before using callback-borrowed
nodes. Resource refusal maps to BudgetExceeded, execution interruption to
ScriptWatchdog and exceptions to RuntimeError. Installed app failures terminate
the app and return to the system shell. Direct-page failures stop the script.

Review found another boundary in input-controller reconstruction. Its constructor
can dispatch autofocus. Modal-root changes and restored focus also run callbacks.
These operations now consume failure and abort their callers. Before correction,
an autofocus exception produced no failure diagnostic and the next timer pump
cleared it. The corrected run reports autofocus/RuntimeError immediately.

Recovery can replace the document. A monotonic recovery generation prevents
scroll rendering, trace attribution, wheel descriptions, drag and inertia callers
from resuming with nodes borrowed from the terminated app. Recovery clears
scroll gesture targets before rebuilding the shell.

The dedicated CTest exercises nested click resource exhaustion, initial autofocus
exceptions and wheel-triggered autofocus failure with trace output. Full R3 CI
builds and runs the shell regression for Node identity OFF and ON. The owner
host configure now explicitly receives that matrix policy too.

## Local evidence

E:/Experiments/browser-callback-delivery-20261112 retains configure/build/test
logs and preserved controls. Current Core source is the PR28 candidate3e7a76b5,
source5af3a7f6.../148. MSVC Debug, Release and Clang ASan+UBSan targeted shell
and combined script/worker tests pass. Sanitizer scope instruments Runtime/Core
and Jerry core/ext, retains an uninstrumented platform port and disables leak
detection. Wider optional sample/benchmark tools are excluded from this gate.

An installed app fixture arms its focus exception from a wheel listener. The
sanitized shell terminates instance1 and presents launcher instance2 while
capturing three frames with tracing enabled. The reproduction script explicitly
sets the fixture packager's active Core version to0.9.0-dev for the isolated
source-override shell. Production package policy and dependency lock are retained.

Locked Core0.8/ABI3 legacy Release shell compiles and consumes the autofocus
exception with RuntimeError. Its combined script/worker CTest passes1/1.
Required remote CI is pending.
This host change has no firmware flash or hardware acceptance result.
