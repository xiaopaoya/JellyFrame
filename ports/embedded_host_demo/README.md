# Embedded Host Demo

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This directory contains a platform-neutral host bring-up demo. It is kept under
`ports/` because it exercises board-facing integration points rather than
desktop inspection:

- static in-memory HTML and CSS resources;
- tight embedded-style `HostBudgets`;
- bitmap font measurement and painting;
- focus/input activation without Win32;
- RGBA rendering converted to RGB565 through the embedded framebuffer sink.

The executable name remains `jellyframe_embedded_host_demo`.

This is not a real board port. Use it as the smallest reference shape before
connecting a board-specific display, input queue, clock and text backend.

Audience: Render Core or port contributors validating the neutral HAL boundary.
Start here when a board port has not yet been selected; move to a real port only
after this smoke path is understood.
