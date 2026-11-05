# WS147 Panel Scroll Synchronization Fix and Port Acceptance

> Last updated: 2026-11-05; Applies to: 0.6.0-dev
> Status: deferred by owner on 2026-11-05; board-level TE input is not present in the published WS147 schematic; experimental path remains disabled.

This task is limited to the WS147 experimental physical-GRAM panel-scroll path. It does not change Render Core scrolling and does not treat desktop auto-scroll timing as touch-latency evidence.

The owner has paused further panel-scroll synchronization work after sufficient independent attempts. Do not investigate TE, `DISPOFF`, or alternative synchronization paths unless the owner restarts this work. Keep physical-GRAM panel-scroll disabled and retain the full-frame/fallback path. The qualification criteria below are historical requirements if the work is resumed.

The path has a measured auto-scroll throughput benefit, but the latest device retest showed human-visible lower-edge row displacement. Keep `JELLYFRAME_WS147_PANEL_SCROLL_VISUAL_ACCEPTED` disabled until this document's exit criteria pass.

The interaction policy is explicit: physical-GRAM panel-scroll is ineligible during direct touch dragging and its inertia phase. Even when the experimental configuration is enabled, a frame with `scroll_gesture.dragging()` or `scroll_gesture.has_inertia()` must leave panel-scroll, rebuild the framebuffer, and use the normal framebuffer scroll-blit/full-present path. Experimental panel-scroll is reserved for unattended autorun benchmarks; it is not a touch-following implementation or evidence of touch latency.

## Finding

The current ring mapping is internally consistent for positive and negative deltas and for a split at the GRAM end. The current port sequence writes the exposed strip with `RAMWR`/DMA before publishing `VSCSAD`; initialization sends `TEOFF`. The two operations still lack a shared scan boundary, so the panel can scan across the address change and expose stale rows at the lower edge during upward scroll.

On 2026-11-05, the published [Waveshare WS147 schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.47/ESP32-S3-Touch-LCD-1.47-Schematic.pdf) was inspected. LCD connector J3 lists 17 pins and has no TE signal routed to the ESP32-S3. The archived PDF used for inspection had SHA-256 `DB1F05C28BBD078E76B430FF2A1332FE7E566731104756A73C84F08C38333AE5`. This establishes that the documented board connection does not provide a TE GPIO; it does not establish whether the JD9853 silicon or a different module revision supports TE internally. Do not add a guessed TE GPIO or enable the path based on another panel's behavior.

## Synchronization options and required confirmation

For the documented WS147 board, assume TE input is unavailable unless a board revision-specific schematic or direct electrical inspection proves otherwise. The safe default is full-frame/fallback. A display-off transaction is a possible alternative only after verifying JD9853 command support and measuring the visible blank interval; it may be too noticeable for interactive scrolling. A fixed delay is not an acceptable synchronization fix. If a different board revision exposes TE, record its pin, polarity, edge and multiplexing, then confirm JD9853 TE mode and pulse meaning against that module's documentation and a logic-analyzer capture.

## Implementation and evidence

Keep panel-scroll default-off. Do not add a TE GPIO configuration for the documented board. Any future synchronization implementation must keep the LCD transaction under one lock and preserve the existing ring mapping. Record the selected synchronization mode, VSCSAD updates, strip DMA completion, direction and fallback/re-entry counters. Test small positive/negative moves, wrap crossings, 120 tracked move samples, synchronization/DMA faults, ordinary redraw interleaving and a 30-minute mixed run with at least 100 wraps.

Pass requires no human-visible row displacement, tearing, repeated/jumped rows or stale content; event correlation through DMA completion; no panic/watchdog/reset/DMA/SPI/panel/present errors; successful full-present recovery and re-entry. Claim input-to-present only with TE/latch or optical evidence. The current mainline additionally rejects panel-scroll candidates while `scroll_gesture.dragging()` or `scroll_gesture.has_inertia()` is true; this protection is only effective on hardware after flashing the current mainline firmware. Do not enable the visual acceptance Kconfig or publish the path before a versioned report and firmware hash satisfy all criteria.
