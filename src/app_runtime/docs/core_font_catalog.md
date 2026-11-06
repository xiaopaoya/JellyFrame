# Core Font Catalog

> Last updated: 2026-11-06; Applies to: 0.8.0

`AppFontSet` can hold both the existing bitmap supplement set and a Core-owned
JFFONT2 catalog. The Core path is exposed through `set_core_system_font`,
`load_core_font`, `attach_core_font_view`, and `core_text_system`.

The system resource can be copied or attached as a borrowed read-only view and
remains bound as family ID 0. Borrowed bytes must remain unchanged and outlive
the font set. App resources belong to the active app instance. A named family is
mapped from Core's
normalized CSS family hash to a catalog-local family ID; an empty family name
registers another face in family 0. Each resource, including replacements for
the system face, receives a monotonically assigned identity. A candidate catalog
is published only after Core accepts the complete face table, so invalid
resources, missing default faces, alias conflicts and Core limits preserve the
previous service.

`load_core_font` copies resource bytes. `attach_core_font_view` borrows them;
the caller must keep the bytes unchanged until the app's font set is cleared.
The returned `TextSystem` pointer is owned by `AppFontSet` and remains valid
until the next successful catalog update or app teardown. Consumers must not
retain it across those operations. Its generation increments on each successful
rebind; layout caches must key on both the service pointer and generation. App
launch/teardown clears app faces and rebuilds the catalog from the system
resource, if one is configured.

Manifest package loading now distinguishes JFFONT2 by its resource magic and
registers it through the Core catalog API; JFFONT0/1 still use the legacy
bitmap-font loader. The packager validates both formats, including JFFONT2
coverage layout and CRC, and does not apply legacy integer-size restrictions to
JFFONT2. A named JFFONT2 family still requires a family-0 Core face in the
catalog. The Win32 browser's `--use-app-fonts` mode now binds a ready catalog to
DOM layout, Canvas2D, layer construction and software composition. Apps without
a Core catalog continue through the existing backend. The ESP32 image host now
binds a flash-resident family-0 Noto face. Its retained UI has an opt-in
`JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM` route through layout and software
composition; it remains disabled by default pending visual acceptance. The
installed script sessions now pass their host-owned `TextSystem` to the worker
layout/layer pipeline and UI frame renderer. Frame protocol v5 carries Core
`PreparedText` packets; v1-v4 retain their previous command set. The font
catalog remains host-owned until the worker and UI tasks finish teardown.
Desktop hosts still need a platform-provided default face.
