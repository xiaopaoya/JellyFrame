# Noto Sans SC font source

> Last updated: 2026-07-26; Applies to: 0.5.0

The ESP32-S3 production bitmap pack is generated offline from the three font
files in `source/noto-sans-sc-2.002`. They are retained with the port so a
developer does not need an undocumented `C:\Windows\Fonts` installation to
reproduce the pack. The generated C++ output is written to the build tree,
not kept as a checked-in source file. The checked-in
The checked-in catalog contains Core-prepared 16 px, 4-bit Regular, Medium and
Bold default-family faces. Every face covers the same 4,539 codepoints recorded
by the prior Regular asset. The immutable Runtime table is generated as
`src/app_runtime/core_system_font_catalog.cpp`; consumers attach its three
views in Regular, Medium, Bold order without heap copies. The retained UI can
opt into the Core text path through
`JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM`; it is disabled by default until
device visual acceptance is complete.

| asset | weight | bytes | SHA-256 |
|---|---:|---:|---|
| `core-system-noto-regular-16.jffont2` | 400 | 716,274 | `7454E5A35C99E1BFBEB0CF226C0E9C128C1D9520B40EC9037A293A2A6C43A320` |
| `core-system-noto-medium-16.jffont2` | 500 | 717,419 | `032DDF64D6EC6D0694F564850C5668BC8B7F43FB099DCDE522568A340EAE5593` |
| `core-system-noto-bold-16.jffont2` | 700 | 726,078 | `0D8083B975AFDA901D70998991C3CDA1BA70D0C29F151D13D4BF44BD3A821301` |

The catalog contributes 2,159,771 bytes (about 2.06 MiB) to the image plan. An
8 MiB partition has 6,228,837 bytes remaining after the font assets alone;
other code and resources consume part of that capacity. Final linked image
size and remaining margin require a port build measurement.

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

Prepared asset SHA-256 values are recorded in
`core-system-noto-catalog.json`, alongside each exact source hash and Core
generator report.

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

## Core catalog regeneration

Install the exact versions in Core's
`tools/font_resource_requirements.txt`, then run the Runtime helper with the
pinned Core preparer checkout:

```powershell
python tools/prepare_core_system_font_catalog.py `
  --core-tool E:\Workspaces\migration-followup\Core-remove-host-text-adapter-20261114\tools\prepare_font_resource.py `
  --font-dir ports/esp32s3-idf/font/source/noto-sans-sc-2.002 `
  --output-dir ports/esp32s3-idf/font `
  --catalog-dir src/app_runtime `
  --existing-regular ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2
```
