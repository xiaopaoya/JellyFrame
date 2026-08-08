# Jelly Canvas Gauges

> Last updated: 2026-07-22; Applies to: 0.5.0

Canvas 2D V0.4 sample for wearable gauges and compact data graphics. DOM/CSS still
own layout, text and controls; Canvas is used only for bounded rings and tiny
charts/labels that are awkward to express with boxes.

This package declares `graphics.canvas2d`. Targets must opt in with
`hostServices.canvas2d`. Its responsive CSS and target gates cover `round-300`,
`rect-320x240` and `rect-172x320`; run `jellyframe_cli.py doctor --sample
jelly_canvas_gauges --strict` to recheck that contract.
