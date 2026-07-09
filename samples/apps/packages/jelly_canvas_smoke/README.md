# Jelly Canvas Smoke

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

Small Canvas 2D V0.4 sample for trend lines and simple charts. It draws the chart once into a compact source canvas, uses a bounded two-stop concentric `createRadialGradient()` background, and uses canvas-to-canvas `drawImage()` scaling for the visible graph. The page still uses
ordinary DOM/CSS for structure and text; Canvas is only used for the bounded
data graphic.

This sample declares `graphics.canvas2d`. Product target profiles must opt in
before package checks treat Canvas as supported.
