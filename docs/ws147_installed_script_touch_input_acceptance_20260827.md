# WS147 Installed Script App Touch-Input Acceptance

> Last updated: 2026-08-27; Applies to: 0.6.0-dev

## Purpose

Verify that physical touch events from an installed classic-script App enter the
worker as value-only input packets, cause JavaScript/DOM mutation, publish a new
value frame, and complete panel presentation. This complements the accepted v4
timer-driven render-output route; it is neither a JFDP lifecycle rerun nor a
Canvas or frame-rate benchmark.

## Preconditions

- The WS147 Developer Image, provider and manifest come from one build. Save
  `discover`, `info`, and wire identity. Mark the run `blocked` for any image
  ID, profile, version, Render Core version/ABI, or source-revision mismatch.
- Enable the installed script-task runtime on `rect-172x320`. Record firmware
  SHA-256, provider version, COM endpoint, ESP-IDF version and SDKCONFIG.
- The provider exclusively owns the endpoint; do not run a raw serial monitor.

## Fixture

Install a minimal classic-script `.jfapp` through the provider with:

1. A button whose `click` handler increments visible text.
2. An `input[type=range]` whose `input` handler updates visible text and a
   width, colour, or progress indicator while it is dragged.
3. Static title and background content that makes residual pixels and clear
   flashes observable.

Archive fixture HTML/CSS/JS, `jellyframe.app.json`, the `.jfapp`, and its
package hash. Timers must not fabricate the button or range changes.

## Procedure

1. Install and launch the fixture, wait for worker-ready and first present, and
   save initial provider logs.
2. Tap the button at least three times. The counter must increase exactly once
   per tap, without a whole-frame flash or stale text.
3. Drag the range from low to high, then high to low. Inspect the visible value
   and indicator at the start, midpoint and end; updates must occur during the
   drag, not only on release.
4. Collect app-scoped `script-input` and `script` logs from the same run. Keep
   `posted`, `worker_seq`, `mutation_seq`, `published_seq`, `accepted_seq`, and
   `presents_failed` in one evidence chain.
5. Stop and relaunch, then repeat one button tap and one range drag. Old-session
   input must not affect the new session.
6. Optionally perform at least 20 rapid taps or drag movements. This checks
   bounded queue recovery only and makes no FPS claim.

## Pass Criteria

- Button taps and range drags produce their intended JavaScript-visible changes.
  Range moves must reach the worker with primary `button/buttons` state so they
  update while dragging, rather than merely processing down/up.
- `posted` and `worker_seq` advance after interaction. State-changing input also
  advances `mutation_seq`, `published_seq`, and `accepted_seq`; short fixed-slot
  backpressure may occur but must not permanently stall.
- During ordinary interaction, `rejected=0`, `unsupported=0`,
  `queue_dropped=0`, and `presents_failed=0`. A non-zero counter requires the
  event kind, operation and recovery result in the report.
- There is no watchdog, panic, reset, brownout, DMA/SPI/panel error, failed
  flush, or app-to-app input leakage. Stop/relaunch returns to normal
  interaction without consuming stale packets.

## Archive

Archive `report.md`, `summary.json`, provider JSON/JSONL, complete app-scoped
logs, manifest, fixture source, and `.jfapp` hash. The summary records at least
identity match, button clicks, range visual result, every input counter,
worker/mutation/published/accepted sequences, failed presents, relaunch result,
and errors. Desktop-only or timer-only evidence is `blocked` or `partial`; it
cannot reuse the v4 render-output result as a pass.
