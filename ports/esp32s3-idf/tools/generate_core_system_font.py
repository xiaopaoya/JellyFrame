#!/usr/bin/env python3
"""Wrap a prepared JFFONT2 asset as a read-only C++ byte array."""

from __future__ import annotations

import argparse
from pathlib import Path

def format_bytes(data: bytes) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16]) + ","
        for offset in range(0, len(data), 16)
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-resource", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    blob = args.font_resource.read_bytes()
    if len(blob) < 128 or blob[:8] != b"JFFONT2\0":
        raise SystemExit(f"not a JFFONT2 resource: {args.font_resource}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    header_path = args.output_dir / "jellyframe_esp32s3_core_system_font.h"
    source_path = args.output_dir / "jellyframe_esp32s3_core_system_font.cpp"
    header_path.write_text(
        "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n"
        "namespace jellyframe_esp32s3_generated {\n"
        "extern const std::uint8_t kCoreSystemFont[];\n"
        "extern const std::size_t kCoreSystemFontBytes;\n"
        "} // namespace jellyframe_esp32s3_generated\n",
        encoding="utf-8",
        newline="\n",
    )
    source_path.write_text(
        "// Generated from the vendored Noto Sans SC Regular face.\n"
        '#include "jellyframe_esp32s3_core_system_font.h"\n\n'
        "namespace jellyframe_esp32s3_generated {\n"
        "const std::uint8_t kCoreSystemFont[] = {\n"
        + format_bytes(blob)
        + "\n};\n"
        "const std::size_t kCoreSystemFontBytes = sizeof(kCoreSystemFont);\n"
        "} // namespace jellyframe_esp32s3_generated\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"embedded {len(blob)} bytes from {args.font_resource}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
