# Canonical app font sample

> Last updated: 2026-11-15; Applies to: 0.6.0-dev

The licensed Noto Sans SC 2.002 Regular subsets in `fonts/*.jffont2` are generated
by Core `tools/prepare_font_resource.py`, using Pillow 12.3.0 and fontTools 4.60.1.
The tracked OTF is `ports/esp32s3-idf/font/source/noto-sans-sc-2.002/NotoSansSC-Regular.otf`.
Coverage text and resource reports are in development-only `fonts/.source/`; `fonts/LICENSE.txt`
contains the SIL Open Font License. Reports retain original font metrics, glyph IDs
and source SHA-256. The 16px raster strike supports Core scalar scaling, including
12px: the historical integer-only bitmap size warning is retired.

Prepare fonts before packaging. Legacy `.jffont`/JFFONT0-v0/v1 and BDF serialized
font generation are retired; BDF-generated C++ BitmapFont headers are separate.

Resource coverage does not expand TextSystem scalar admission. The policy sample
explicitly refuses its intentional missing Kana probe; named
Chinese glyph visibility is verified independently by pixel tests.
