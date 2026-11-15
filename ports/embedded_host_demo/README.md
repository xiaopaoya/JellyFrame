# Embedded Host Demo

> Last updated: 2026-11-15; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

This directory contains a platform-neutral host bring-up demo. It is kept under
`ports/` because it exercises board-facing integration points rather than
desktop inspection:

- static in-memory HTML and CSS resources;
- tight embedded-style `HostBudgets`;
- Core-owned `TextSystem` measurement, glyph preparation and painting;
- focus/input activation without Win32;
- RGBA rendering converted to RGB565 through the embedded framebuffer sink.

The executable name remains `jellyframe_embedded_host_demo`.

This is not a real board port. Use it as the smallest reference shape before
connecting a board-specific display, input queue, clock and font-resource delivery.

Audience: Render Core or port contributors validating the neutral HAL boundary.
Start here when a board port has not yet been selected; move to a real port only
after this smoke path is understood.

The demo borrows a complete 318-byte JFFONT2 resource containing space and
hand-authored A/B coverage strikes. Its bytes and face registration remain alive
through layout, layer construction and composition. The same `TextSystem` is
bound to all three stages; no text measurement or paint callbacks are installed.
This uses the supported TextSystem contract in both locked Core 0.8.0 / ABI3 and
Core PR #60 / ABI4, without an ABI-specific callback fallback.

Build and run `jellyframe_embedded_host_demo` with the normal examples profile.
Success requires at least 20 green heading-ink pixels (controls cannot satisfy
this check), button activation, checkbox toggling, profile-correct select activation, a
128x96 RGB565 buffer and one full-viewport flush. The process returns nonzero
if any proof fails, including in Release builds. The font is deliberately tiny;
it is a bring-up fixture, not a general UI font catalog.

The heading uses left alignment to keep this bring-up fixture within the shared
ABI3/ABI4 prepared-text path; centered-text parity is outside this smoke gate.
The frame is composed before input activation, and the input proof checks the
resulting live form state independently.

With advanced forms enabled, select activation opens the popup and preserves
`A`; with that family cropped out, the base control cycles to `B`. Both outcomes
are checked explicitly through the exported feature flag.
