#!/usr/bin/env python3
"""Prepare and embed the shared Noto Sans SC Core system catalog."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path


def load_preparer(path: Path):
    spec = importlib.util.spec_from_file_location("prepare_font_resource", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Core preparer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def resource_codepoints(blob: bytes) -> list[int]:
    if len(blob) < 128 or blob[:8] != b"JFFONT2\0":
        raise ValueError("coverage source is not JFFONT2")
    glyph_count = struct.unpack_from("<I", blob, 20)[0]
    records_offset = struct.unpack_from("<I", blob, 24)[0]
    record_bytes = struct.unpack_from("<H", blob, 12)[0]
    if record_bytes != 40 or records_offset + glyph_count * record_bytes > len(blob):
        raise ValueError("coverage source has an invalid glyph table")
    points = [struct.unpack_from("<I", blob, records_offset + i * record_bytes)[0]
              for i in range(glyph_count)]
    if points != sorted(set(points)):
        raise ValueError("coverage source glyph map is not strictly sorted")
    return points


def cpp_bytes(blob: bytes) -> str:
    return "\n".join("    " + ", ".join(f"0x{x:02x}" for x in blob[i:i + 16]) + ","
                     for i in range(0, len(blob), 16))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-tool", type=Path, required=True,
                        help="pinned Core tools/prepare_font_resource.py")
    parser.add_argument("--font-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--catalog-dir", type=Path)
    parser.add_argument("--existing-regular", type=Path, required=True)
    args = parser.parse_args()

    prepare = load_preparer(args.core_tool).prepare
    coverage = "".join(chr(cp) for cp in resource_codepoints(args.existing_regular.read_bytes()))
    faces = (
        ("regular", "NotoSansSC-Regular.otf", 400, 16, coverage),
        ("medium", "NotoSansSC-Medium.otf", 500, 16, coverage),
        ("bold", "NotoSansSC-Bold.otf", 700, 16, coverage),
        ("bold-numeric", "NotoSansSC-Bold.otf", 700, 34, "0123456789: C"),
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    catalog_dir = args.catalog_dir or args.output_dir
    catalog_dir.mkdir(parents=True, exist_ok=True)
    prepared = []
    for name, filename, expected_weight, size, face_coverage in faces:
        source = args.font_dir / filename
        font_bytes = source.read_bytes()
        blob, report = prepare(font_bytes, face_coverage, size=size, bits=4)
        if report["weight"] != expected_weight or len(report["glyphs"]) != len(set(face_coverage)):
            raise ValueError(f"unexpected face metadata for {source}")
        report.pop("glyphs")
        asset_name = f"core-system-noto-{name}-{size}.jffont2"
        (args.output_dir / asset_name).write_bytes(blob)
        report.update({
            "asset": asset_name,
            "sourceFile": filename,
            "sourceFileSha256": hashlib.sha256(font_bytes).hexdigest(),
            "license": "SIL Open Font License 1.1",
            "licenseFile": "LICENSE-NOTO-SANS-SC.txt",
            "coverageCodepoints": len(set(face_coverage)),
        })
        prepared.append((asset_name, blob, report))

    header = ("#pragma once\n\n#include \"app_runtime/app_font_set.h\"\n\n"
              "namespace jellyframe {\n"
              "extern const CoreFontResourceView kNotoSansScSystemFontCatalog[];\n"
              "extern const std::size_t kNotoSansScSystemFontCatalogSize;\n"
              "} // namespace jellyframe\n")
    source = ['#include "app_runtime/core_system_font_catalog.h"',
              "\nnamespace jellyframe {"]
    for index, (asset_name, blob, _) in enumerate(prepared):
        symbol = f"kNotoSansScSystemFont{faces[index][0].title().replace('-', '')}{faces[index][3]}"
        source.extend((f"namespace {{ constexpr std::uint8_t {symbol}[] = {{",
                       cpp_bytes(blob), "}; }"))
    source.append("const CoreFontResourceView kNotoSansScSystemFontCatalog[] = {")
    for index, (_, _, _) in enumerate(prepared):
        symbol = f"kNotoSansScSystemFont{faces[index][0].title().replace('-', '')}{faces[index][3]}"
        source.append(f"    {{{symbol}, sizeof({symbol})}},")
    source.extend(("};", "const std::size_t kNotoSansScSystemFontCatalogSize =",
                   "    sizeof(kNotoSansScSystemFontCatalog) / sizeof(kNotoSansScSystemFontCatalog[0]);",
                   "} // namespace jellyframe", ""))
    (catalog_dir / "core_system_font_catalog.h").write_text(header, encoding="utf-8", newline="\n")
    (catalog_dir / "core_system_font_catalog.cpp").write_text("\n".join(source), encoding="utf-8", newline="\n")
    manifest = {
        "family": "Noto Sans SC",
        "version": "2.002",
        "profile": {"rasterPixelSize": 16, "coverageBits": 4, "coverageCodepoints": len(coverage)},
        "faceOrder": ["regular-400-16", "medium-500-16", "bold-700-16", "bold-numeric-700-34"],
        "license": "SIL Open Font License 1.1",
        "licenseFile": "LICENSE-NOTO-SANS-SC.txt",
        "aggregateAssetBytes": sum(len(blob) for _, blob, _ in prepared),
        "assets": [report for _, _, report in prepared],
    }
    (args.output_dir / "core-system-noto-catalog.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"aggregateAssetBytes": manifest["aggregateAssetBytes"],
                      "resourceBytes": [report["resourceBytes"] for _, _, report in prepared],
                      "resourceSha256": [report["resourceSha256"] for _, _, report in prepared]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
