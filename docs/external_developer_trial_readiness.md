# External Developer Trial Readiness

> Last updated: 2026-08-09; Applies to: 0.6.0-dev

This is the short admission checklist for a limited external developer trial.
It is a release gate, not a promise of full browser compatibility or flawless
hardware behavior. A gate is open only when the stated command and evidence
are reproducible from a clean build.

## Author Workflow

1. Read [HOW_TO_START.md](../HOW_TO_START.md),
   [app_author_guide.md](app_author_guide.md), the quick capability table and
   the searchable HTML/CSS support tables before choosing syntax.
2. Configure and build a desktop Release shell. Run `ctest` before editing an
   app so a toolchain failure is not confused with an app failure.
3. Create a source package with `jellyframe_cli.py new` and edit only local
   HTML, CSS, classic or package-time static-module JavaScript and declared
   resources.
4. Run `jellyframe_cli.py check --targets round-300,rect-320x240,rect-172x320`.
   Fix `error` diagnostics and inspect every `warning`, `developerAdvice[]`,
   `performanceAdvice[]`, font and target-gate result.
5. Run `preview` for each intended target, then use the Win32 shell and a
   deterministic frame script for input, scroll and animation behavior.
6. Run `package` to produce the `.jfapp`; validate the generated report and
   install it into the desktop registry. Use `doctor` for a repository-wide
   sample pass and `trial` for the strict official evidence flow.
7. Only after the desktop report is clean, ask the port owner to run the target
   profile. Hardware reports must separate core render time, conversion,
   present/DMA, memory watermarks, input delivery and visual inspection.

## Admission Gates

| Gate | Required evidence | Blocking failure |
| --- | --- | --- |
| Build | Clean Release and Debug configure/build | Missing dependency, stale generated profile or failed link |
| Regression | Relevant CTest plus `doctor --trial` | Any unexpected test failure or unclassified warning |
| Authoring | `new -> edit -> check` from a clean directory | Command/path mismatch or report that cannot identify the fix |
| Rendering | Three target previews and Win32 capture | Text overflow, clipping, broken fallback, input/scroll failure |
| Packaging | `.jfapp` package report, install and launch | Resource escape, manifest drift, non-atomic install/update |
| Recovery | Failed-app, rollback and remove workflows | Host shell crash, stale registry state or data loss outside confirmation |
| Device | Port-owned profile, frame and memory evidence | Reset, watchdog, failed flush, unexplained visual or input defect |

## Explicit Scope

The supported contract is the capability matrix, not browser intuition. Current
high-value authoring pieces include bounded block/inline, flex/grid, forms,
local routes, classic scripts, package-time static modules, transitions/keyframes,
rounded geometry, gradients, shadows, text overflow controls, package-local
images and opt-in Canvas 2D. Many APIs remain partial, host-dependent or
deferred, including full Workers, Shadow DOM, iframe, browser storage, remote
loading, full media, SVG and browser-grade text shaping.

`minJellyFrame: 0.5.0` is the package compatibility baseline. It must not be
changed to `0.6.0-dev` merely because the repository is on the 0.6 development
line.

## Evidence Rules

- A desktop preview proves desktop pipeline behavior only.
- A port benchmark must include workload, target, build/profile, frame/present
  p50/p95, dirty area, conversion/DMA timing, memory low-water and failure
  counters. Do not infer missing p95 from average or extrema.
- A capability changes status only after implementation, behavior regression,
  documentation and the relevant target gate agree.
- Keep raw port logs and reports outside source commits. Curate local release
  evidence under `project_docs/` when needed; public documents should cite
  only the conclusion and its evidence scope.

## Trial Stop Conditions

Pause external recruitment when a clean sample fails to package or launch, a
diagnostic gives no actionable location, a documented supported feature silently
disappears, a recovery path leaves registry/data state ambiguous, or a target
shows reset/watchdog/present errors. Record the smallest reproducer and classify
the issue as core, tool, documentation, host or port before changing scope.
