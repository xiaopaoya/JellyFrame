# Jelly Canvas Smoke

> Last updated: 2026-09-04; Applies to: 0.6.0-dev; Render Core baseline: 0.6.2

Small Canvas 2D V0.4 sample for trend lines and simple charts. It draws the chart once into a compact source canvas, uses a bounded two-stop concentric `createRadialGradient()` background, and uses canvas-to-canvas `drawImage()` scaling for the visible graph. The page still uses
ordinary DOM/CSS for structure and text; Canvas is only used for the bounded
data graphic.

This sample declares `graphics.canvas2d`. Product target profiles must opt in
before package checks treat Canvas as supported.

This is an acceptance package, not an app-author starter or gallery example.
The current Win32 gallery shell does not claim a product Canvas host binding;
use it to validate the bounded API and target declaration only where the host
has explicitly enabled Canvas.
