# Band System Shell

> Last updated: 2026-07-16; Applies to: 0.5.0-dev

An icon-first, 172x320 wearable system-shell visual acceptance sample. It
separates host-owned watch face, launcher, quick settings and notifications
from the application concepts it presents. It intentionally uses only the
documented HTML/CSS subset: no SVG, filters, runtime HTML parsing or JavaScript.

The ESP32-S3 port carries an equivalent static resource and a narrow native
event adapter for board bring-up. This package remains the source-package
acceptance form; it does not grant third-party apps system capabilities.

The current visual baseline uses bounded radial/conic gradients, short text
shadows, outline-offset focus rings and pressed transform feedback. These are
all declaration-driven; the board shell deliberately avoids a permanent route
animation loop.
