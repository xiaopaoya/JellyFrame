# App Templates

> Last updated: 2026-08-25; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Reference app templates for common wearable workflows. These are intentionally
small, modern and brand-neutral watch-style apps that stay inside the documented
JellyFrame HTML, CSS and scripting subset instead of depending on full browser
layout behavior or copying commercial watch interfaces.

- `calculator/`: compact quick-math keypad, event delegation, `dataset` and local state.
- `blank/`: the smallest `Hello world` package with empty CSS and JavaScript entry points.
- `clock/`: timer-driven dayline display updates and compact health metrics.
- `timer/`: control buttons, state changes and time formatting.
- `weather/`: data-shaped UI intended for future host network APIs.

## Choose A Template

| Template | Best first use | Main ideas |
| --- | --- | --- |
| `blank/` | A clean start | Minimal manifest, `Hello world`, empty CSS/JS entry points |
| `weather/` | Data cards and status panels | Local images and state-shaped content |
| `clock/` | Time/status dashboard | Compact typography and host-time updates |
| `timer/` | Interactive control loop | Buttons, local state and deterministic time |
| `calculator/` | Dense keypad layout | Grid-like rows, event delegation and bounded state |

Copy one with `python tools/jellyframe_cli.py new --template NAME`, then run
`check` before adding features. These are starter apps, not exhaustive feature
fixtures or proof of target-panel performance.

These directories are app-author starting points. They intentionally mirror the
source-package structure but should not accumulate every edge case; targeted
fixtures belong under `../../../samples/apps/packages`,
`../../../src/render_core/samples/pages/modern` and
`../../../src/script/samples/classic`. Template source conventions are in
[CONVENTIONS.md](CONVENTIONS.md).
