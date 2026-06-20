# App Templates

Reference app templates for common wearable workflows. These are intentionally
small, modern and brand-neutral watch-style apps that stay inside the documented
JellyFrame HTML, CSS and scripting subset instead of depending on full browser
layout behavior or copying commercial watch interfaces.

- `calculator/`: compact quick-math keypad, event delegation, `dataset` and local state.
- `clock/`: timer-driven dayline display updates and compact health metrics.
- `timer/`: control buttons, state changes and time formatting.
- `weather/`: data-shaped UI intended for future host network APIs.

These directories are app-author starting points. They intentionally mirror the
source-package structure but should not accumulate every edge case; targeted
fixtures belong under `../../../samples/apps/loose`, `../../../src/render_core/samples/pages/modern`
and `../../../src/script/samples/classic`.
