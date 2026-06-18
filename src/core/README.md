# Core

The platform-neutral JellyFrame engine core.

This directory owns the embedded-safe pipeline:

1. HTML/CSS parsing.
2. DOM and CSSOM construction.
3. Style resolution.
4. Render/layout/layer tree construction.
5. Display command generation.
6. Software framebuffer rendering.
7. Hit testing, events, form controls and dirty-region planning.

Core code should prefer bounded data structures, predictable allocation and
small-device failure behavior over browser-completeness.
