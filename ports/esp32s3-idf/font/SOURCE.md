# Noto Sans SC font source

> Last updated: 2026-07-26; Applies to: 0.5.0

The ESP32-S3 production bitmap pack is generated offline from the three font
files in `source/noto-sans-sc-2.002`. They are retained with the port so a
developer does not need an undocumented `C:\Windows\Fonts` installation to
reproduce the pack. The generated C++ output is written to the build tree,
not kept as a checked-in source file. The checked-in
`core-system-noto-regular-16.jffont2` is the Core-prepared 16 px, 4-bit default
face from the Regular source, using the production coverage set while excluding
codepoints absent from the source font cmap. Its SHA-256 is
`7454E5A35C99E1BFBEB0CF226C0E9C128C1D9520B40EC9037A293A2A6C43A320`; it
contains 4,539 mappings in 716,274 bytes. The build wraps those bytes as a
read-only C++ array in flash and attaches it to the device image host without a
heap copy. The retained UI can opt into the Core text path through
`JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM`; it is disabled by default until
device visual acceptance is complete.

## Provenance

- Family: Noto Sans SC
- Version: 2.002
- Vendor metadata: `GOOG`, Adobe
- Copyright metadata: Copyright 2014-2020 Adobe
- License: SIL Open Font License 1.1; see `LICENSE-NOTO-SANS-SC.txt`
- Upstream project: https://github.com/notofonts/noto-cjk
- Contemporary upstream release: https://github.com/notofonts/noto-cjk/releases/tag/v20201206-cjk

The three exact binaries were originally installed by the Windows Noto Sans SC
optional-font package. They are now vendored here under the OFL. The upstream
2020 tag identifies the corresponding Noto CJK generation but its repository
blobs are not byte-identical to these Windows subset OTFs; use the hashes below,
not the tag alone, when auditing exact regeneration.

| file | bytes | SHA-256 |
|---|---:|---|
| `NotoSansSC-Regular.otf` | 8,482,020 | `A2B93E6C2DB05D6BBBF6F27D413EC73269735B7B679019C8A5AA9670FF0FFBF2` |
| `NotoSansSC-Medium.otf` | 8,508,580 | `9C62CEB174D7529AE4A7F2071F6531991CFADBC2F1897910B48BA951A580AC57` |
| `NotoSansSC-Bold.otf` | 8,716,392 | `D1961BE1161EA1BE08496C920862D06EA5C23A757628F4FD69368DE1D9F51BED` |

## Reproduction

From the repository root, with the pinned Pillow and fontTools versions from
`tools/font_resource_requirements.txt` installed:

```powershell
python ports/esp32s3-idf/tools/generate_noto_sans_sc_font_pack.py `
  --output-dir ports/esp32s3-idf/main `
  --sizes 16,20,24 `
  --bits-per-pixel 1
```

Use `--bits-per-pixel 2` or `4` with a separate output directory for product
A/B candidates. The generator validates the shared lowercase/capital baseline
using `a`, `b`, `g`, `A`, and `.` before writing output.
