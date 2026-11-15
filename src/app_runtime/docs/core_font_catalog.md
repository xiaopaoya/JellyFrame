# Core Font Catalog

> Last updated: 2026-11-15; Applies to: 0.6.0-dev

`AppFontSet` owns or borrows canonical JFFONT2 resources and exposes one Core
`TextSystem`. Legacy serialized JFFONT0/1 and AppFontSet measurement/painting
callbacks are removed. Generated C++ BitmapFont resources in older port-default
paths are a separate remaining migration; this change does not remove them.

Register immutable default-family0 resources with `set_core_system_font_catalog`
(copying) or `attach_core_system_font_catalog_view` (borrowing). The singular
system-font methods wrap a one-entry catalog. Core owns family/weight/strike
selection, Q1000 metrics, preparation and painting. Requested sizes scale from
original source metrics; there is no legacy integer-only scale or synthetic bold.

`load_core_font` copies app resources; `attach_core_font_view` borrows them.
Borrowed bytes must stay unchanged through app teardown and every use of the
catalog. Named families use Core's normalized CSS family hash. An empty family
registers another default-family0 face; a named-only catalog is invalid without
a real default face. No OS or bitmap fallback is silently installed.

Resource IDs increase monotonically and must not be reused for different bytes.
Admission validates the complete candidate before publishing it; invalid bytes,
missing defaults, catalog conflicts or capacity refusal preserve the prior app
and service. `clear_app_instance` and `clear` remove app faces, preserving the
system catalog. The font set cannot be copied or moved because its service
borrows its own face table. A returned service is for the current catalog only;
do not retain prepared frames/resources across teardown without separate owned
snapshot lifetime. Layout caches key both service identity and generation.

Manifest `fonts[].source` and imports use `.jffont2` only. Renaming JFFONT0/1
does not convert it. Win32 `--use-app-fonts` binds the declared system/app
catalog to layout, layers, Canvas2D and paint; unavailable catalogs refuse
startup. GDI remains the desktop default outside that explicit mode.

Prepare licensed static OTF/TTF assets offline with the pinned Core tool and its
`font_resource_requirements.txt` environment. It retains hhea/hmtx metrics,
actual source weight and source identity; neither the device nor the port
reinterprets them. Current scalar coverage/shaping limits remain explicit.

```powershell
python <pinned-core>/tools/prepare_font_resource.py `
  --font licensed-static-font.otf --characters used_chars.txt `
  --size 16 --bits 4 --output ui.jffont2 --report ui.font.json
```

Alternatively use Runtime's `font --font-source ... --core-font-tool ...`
command with explicit `--characters UTF8_COVERAGE` in that environment. The command's `--font-output` is `.jffont2`;
`--font-size` selects the raster strike. First generate the resource, then place
it at the manifest source or import it with
`--import-font-resource /fonts/ui.jffont2=PATH`. Packaging does not generate
serialized fonts from BDF. `font --bdf --output-header` only generates the
remaining static C++ port product. See [app_packaging.md](app_packaging.md).
