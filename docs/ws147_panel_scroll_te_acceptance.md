# WS147 Panel Scroll Synchronization Fix and Port Acceptance

> Status: pending hardware confirmation and device implementation; board: Waveshare ESP32-S3-Touch-LCD-1.47; panel: JD9853; viewport: 172x320

This task is limited to the WS147 experimental physical-GRAM panel-scroll path. It does not change Render Core scrolling and does not treat desktop auto-scroll timing as touch-latency evidence.

The path has a measured auto-scroll throughput benefit, but the latest device retest showed human-visible lower-edge row displacement. Keep `JELLYFRAME_WS147_PANEL_SCROLL_VISUAL_ACCEPTED` disabled until this document's exit criteria pass.

The interaction policy is explicit: physical-GRAM panel-scroll is ineligible during direct touch dragging and its inertia phase. Even when the experimental configuration is enabled, a frame with `scroll_gesture.dragging()` or `scroll_gesture.has_inertia()` must leave panel-scroll, rebuild the framebuffer, and use the normal framebuffer scroll-blit/full-present path. Experimental panel-scroll is reserved for unattended autorun benchmarks; it is not a touch-following implementation or evidence of touch latency.

## Finding

The current ring mapping is internally consistent for positive and negative deltas and for a split at the GRAM end. The race is in the port sequence: it sends `VSCSAD` and only then writes the exposed strip with `RAMWR`/DMA. Initialization also sends `TEOFF`. Without a shared TE/vblank boundary, the panel can scan the new address before the strip has been written, exposing stale rows at the lower edge during upward scroll.

## Required hardware confirmation

Confirm the actual JD9853 module's TE pin, GPIO, polarity, edge, and multiplexing; confirm the JD9853 TE mode and the meaning of its pulse from module documentation and a logic-analyzer capture. If TE is unavailable, keep full-frame/fallback or use a documented display-off transaction. A fixed delay is not an acceptable synchronization fix.

## Implementation and evidence

Add a default-off TE configuration, bounded wait and timeout fallback. Keep the LCD transaction under one lock and preserve the existing ring mapping. Record TE wait/timeouts, VSCSAD updates, strip DMA completion, direction and fallback/re-entry counters. Test small positive/negative moves, wrap crossings, 120 tracked move samples, TE/DMA faults, ordinary redraw interleaving and a 30-minute mixed run with at least 100 wraps.

Pass requires no human-visible row displacement, tearing, repeated/jumped rows or stale content; event correlation through DMA completion; no panic/watchdog/reset/DMA/SPI/panel/present errors; successful full-present recovery and re-entry. Claim input-to-present only with TE/latch or optical evidence. The current mainline additionally rejects panel-scroll candidates while `scroll_gesture.dragging()` or `scroll_gesture.has_inertia()` is true; this protection is only effective on hardware after flashing the current mainline firmware. Do not enable the visual acceptance Kconfig or publish the path before a versioned report and firmware hash satisfy all criteria.
