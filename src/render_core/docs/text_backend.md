# Text Backend Migration

> Last updated: 2026-11-15; Applies to: 0.6.0-dev

Core `TextSystem` is the canonical interface. Ports register immutable offline
JFFONT2 resources and lifetime; Core owns family/weight selection, original
metrics, Q1000 scaling, glyph preparation and software painting. Do not add
host measurement, font-metric or painting callbacks. AppFontSet's legacy
serialized loader and callback interfaces are removed.

Existing `TextMeasureProvider` / `TextPainter` and range/family/additive hooks
serve remaining generated BitmapFont/GDI default consumers and fixtures only.
Remove them together after production consumers, generated/widget text and tests
migrate. They are not the target API for new ports. HostTextAdapter has already
been removed; its old integration examples are no longer valid.

See [Core Font Catalog](../../app_runtime/docs/core_font_catalog.md) for resource
ownership and the [consumer retirement inventory](../../../docs/render_core_text_consumer_retirement.md)
for remaining work. Platform font APIs or LVGL text must not replace Core text
semantics.
