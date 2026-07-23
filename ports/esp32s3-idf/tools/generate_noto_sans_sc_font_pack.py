#!/usr/bin/env python3
"""Generate the ESP32-S3 production bitmap font pack from Noto Sans SC.

The checked-in generated C++ output keeps normal ESP-IDF builds independent of
Pillow. Re-run this tool only when changing coverage, sizes, weights, or source
fonts.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Iterable

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:  # pragma: no cover - developer environment guard
    raise SystemExit("Pillow is required to regenerate fonts: python -m pip install pillow") from exc


DEFAULT_FONT_ROOT = pathlib.Path(__file__).resolve().parents[1] / "font" / "source" / "noto-sans-sc-2.002"
DEFAULT_FONT_PATHS = {
    "regular": DEFAULT_FONT_ROOT / "NotoSansSC-Regular.otf",
    "medium": DEFAULT_FONT_ROOT / "NotoSansSC-Medium.otf",
    "bold": DEFAULT_FONT_ROOT / "NotoSansSC-Bold.otf",
}

COMMON_SYMBOLS = (
    "，。！？；：、（）【】《》“”‘’"
    "·…-—_+=*/\\|@#%&~^<>"
    "￥$€£°℃㎡①②③④⑤⑥⑦⑧⑨⑩"
    "←→↑↓✓✔✕×○●□■▲▼"
)


@dataclass(frozen=True)
class Face:
    label: str
    weight: int
    size: int
    font_path: pathlib.Path


@dataclass
class Glyph:
    codepoint: int
    width: int
    height: int
    advance: int
    bytes_per_row: int
    rows: bytes


def symbol_name(value: str) -> str:
    return re.sub(r"[^0-9A-Za-z_]+", "_", value).strip("_").lower()


def gb2312_rows(start: int, end: int) -> Iterable[str]:
    for row in range(start, end + 1):
        lead = 0xA0 + row
        for cell in range(1, 95):
            trail = 0xA0 + cell
            try:
                yield bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue


def default_coverage() -> list[str]:
    chars: set[str] = set(chr(codepoint) for codepoint in range(0x20, 0x7F))
    chars.update(COMMON_SYMBOLS)
    chars.update(gb2312_rows(1, 9))
    chars.update(gb2312_rows(16, 55))
    chars.discard("\x7f")
    return sorted(chars, key=ord)


def ceil_length(font: ImageFont.FreeTypeFont, ch: str) -> int:
    try:
        return int(math.ceil(font.getlength(ch)))
    except AttributeError:
        return int(math.ceil(font.getsize(ch)[0]))


def render_glyph(font: ImageFont.FreeTypeFont,
                 ch: str,
                 line_height: int,
                 bits_per_pixel: int = 1) -> Glyph:
    codepoint = ord(ch)
    bbox = font.getbbox(ch)
    if bbox is None:
        bytes_per_row = (bits_per_pixel + 7) // 8
        return Glyph(codepoint,
                     1,
                     line_height,
                     max(1, ceil_length(font, ch)),
                     bytes_per_row,
                     bytes(bytes_per_row * line_height))

    left, top, right, bottom = bbox
    advance = max(1, min(255, ceil_length(font, ch)))
    width = max(1, min(255, max(advance, int(math.ceil(right - left)))))
    height = max(1, min(255, line_height))
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    # Pillow's default anchor places y=0 at the ascender line. Keep the
    # glyph-specific top offset inside the common line-height bitmap so all
    # glyphs retain one baseline. Cropping by -top would top-align their ink.
    draw.text((-left, 0), ch, font=font, fill=255)

    bytes_per_row = (width * bits_per_pixel + 7) // 8
    packed = bytearray(bytes_per_row * height)
    pixels = image.load()
    levels = (1 << bits_per_pixel) - 1
    for y in range(height):
        row_offset = y * bytes_per_row
        for x in range(width):
            coverage = pixels[x, y]
            if bits_per_pixel == 1:
                if coverage > 32:
                    packed[row_offset + x // 8] |= 1 << (7 - (x % 8))
                continue
            value = (coverage * levels + 127) // 255
            bit_index = x * bits_per_pixel
            packed[row_offset + bit_index // 8] |= value << (8 - bits_per_pixel - (bit_index % 8))
    return Glyph(codepoint, width, height, advance, bytes_per_row, bytes(packed))


def glyph_ink_rows(glyph: Glyph) -> tuple[int, int] | None:
    populated_rows = [
        y
        for y in range(glyph.height)
        if any(glyph.rows[y * glyph.bytes_per_row : (y + 1) * glyph.bytes_per_row])
    ]
    if not populated_rows:
        return None
    return populated_rows[0], populated_rows[-1]


def validate_baseline_alignment(glyphs: list[Glyph]) -> None:
    by_codepoint = {glyph.codepoint: glyph for glyph in glyphs}
    rows = {ch: glyph_ink_rows(by_codepoint[ord(ch)]) for ch in "abgA."}
    if any(bounds is None for bounds in rows.values()):
        raise RuntimeError(f"baseline validation found an empty reference glyph: {rows}")

    a_top, a_bottom = rows["a"]
    b_top, b_bottom = rows["b"]
    g_top, g_bottom = rows["g"]
    cap_top, cap_bottom = rows["A"]
    dot_top, _ = rows["."]
    if not (
        b_top < a_top
        and cap_top < a_top
        and b_bottom == a_bottom == cap_bottom
        and g_top == a_top
        and g_bottom > a_bottom
        and dot_top > a_top
    ):
        raise RuntimeError(f"font baseline validation failed: {rows}")


def format_byte_array(data: bytes, indent: str = "    ") -> str:
    if not data:
        return indent + "0x00"
    lines: list[str] = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk))
    return ",\n".join(lines)


def generate_face(font_path: pathlib.Path,
                  size: int,
                  chars: list[str],
                  bits_per_pixel: int) -> tuple[int, list[Glyph]]:
    font = ImageFont.truetype(str(font_path), size=size)
    ascent, descent = font.getmetrics()
    line_height = max(1, min(255, ascent + descent))
    glyphs = [render_glyph(font, ch, line_height, bits_per_pixel) for ch in chars]
    glyphs.sort(key=lambda glyph: glyph.codepoint)
    validate_baseline_alignment(glyphs)
    return line_height, glyphs


def write_header(path: pathlib.Path) -> None:
    path.write_text(
        """#pragma once

#include \"render_core/bitmap_font.h\"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3_generated {

struct FontFace {
    const jellyframe::BitmapFont* font;
    int pixel_size;
    int weight;
};

extern const FontFace kNotoSansScFaces[];
extern const std::size_t kNotoSansScFaceCount;
extern const std::size_t kNotoSansScCoverageCount;
extern const std::size_t kNotoSansScGlyphCount;
extern const std::size_t kNotoSansScBitmapByteCount;
extern const std::uint8_t kNotoSansScBitsPerPixel;
extern const char kNotoSansScCoverageName[];

} // namespace jellyframe_esp32s3_generated
""",
        encoding="utf-8",
    )


def write_source(path: pathlib.Path,
                 header_name: str,
                 faces: list[Face],
                 chars: list[str],
                 bits_per_pixel: int) -> dict[str, int]:
    face_records: list[tuple[Face, int, list[Glyph]]] = []
    total_glyphs = 0
    total_bitmap_bytes = 0
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write(
            f"""// Generated by ports/esp32s3-idf/tools/generate_noto_sans_sc_font_pack.py.
// Source font: Noto Sans SC. Coverage: GB2312 level 1 + GB2312 symbols + ASCII.

#include \"{header_name}\"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3_generated {{
namespace {{

"""
        )
        for face in faces:
            line_height, glyphs = generate_face(face.font_path, face.size, chars, bits_per_pixel)
            face_records.append((face, line_height, glyphs))
            total_glyphs += len(glyphs)
            for glyph in glyphs:
                total_bitmap_bytes += len(glyph.rows)

            prefix = symbol_name(f"noto_sans_sc_{face.label}_{face.size}")
            for glyph in glyphs:
                out.write(
                    f"constexpr std::uint8_t {prefix}_u{glyph.codepoint:04x}[] = {{\n"
                    f"{format_byte_array(glyph.rows)}\n"
                    "};\n\n"
                )

            out.write(f"constexpr jellyframe::BitmapFontGlyph {prefix}_glyphs[] = {{\n")
            for glyph in glyphs:
                bpp_suffix = f", {bits_per_pixel}" if bits_per_pixel != 1 else ""
                out.write(
                    f"    {{0x{glyph.codepoint:x}, {glyph.width}, {glyph.height}, "
                    f"{glyph.advance}, {glyph.bytes_per_row}, {prefix}_u{glyph.codepoint:04x}{bpp_suffix}}},\n"
                )
            out.write("};\n\n")
            fallback_advance = max(1, min(255, face.size))
            out.write(
                f"constexpr jellyframe::BitmapFont {prefix}_font{{\n"
                f"    {prefix}_glyphs,\n"
                f"    sizeof({prefix}_glyphs) / sizeof({prefix}_glyphs[0]),\n"
                f"    {line_height},\n"
                f"    {fallback_advance},\n"
                "};\n\n"
            )

        out.write("} // namespace\n\n")
        out.write("const FontFace kNotoSansScFaces[] = {\n")
        for face, _, _ in face_records:
            prefix = symbol_name(f"noto_sans_sc_{face.label}_{face.size}")
            out.write(f"    {{&{prefix}_font, {face.size}, {face.weight}}},\n")
        out.write("};\n\n")
        out.write("const std::size_t kNotoSansScFaceCount = sizeof(kNotoSansScFaces) / sizeof(kNotoSansScFaces[0]);\n")
        out.write(f"const std::size_t kNotoSansScCoverageCount = {len(chars)};\n")
        out.write(f"const std::size_t kNotoSansScGlyphCount = {total_glyphs};\n")
        out.write(f"const std::size_t kNotoSansScBitmapByteCount = {total_bitmap_bytes};\n")
        out.write(f"const std::uint8_t kNotoSansScBitsPerPixel = {bits_per_pixel};\n")
        coverage_name = "gb2312-level1-symbols-ascii"
        if bits_per_pixel != 1:
            coverage_name += f"-{bits_per_pixel}bpp"
        out.write(f'const char kNotoSansScCoverageName[] = "{coverage_name}";\n\n')
        out.write("} // namespace jellyframe_esp32s3_generated\n")

    return {
        "faces": len(face_records),
        "coverage": len(chars),
        "glyphs": total_glyphs,
        "bitmap_bytes": total_bitmap_bytes,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--sizes", default="16,20,24")
    parser.add_argument("--regular", type=pathlib.Path, default=DEFAULT_FONT_PATHS["regular"])
    parser.add_argument("--medium", type=pathlib.Path, default=DEFAULT_FONT_PATHS["medium"])
    parser.add_argument("--bold", type=pathlib.Path, default=DEFAULT_FONT_PATHS["bold"])
    parser.add_argument("--bits-per-pixel", type=int, choices=(1, 2, 4), default=1)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sizes = [int(value) for value in args.sizes.split(",") if value.strip()]
    weight_paths = [
        ("regular", 400, args.regular),
        ("medium", 500, args.medium),
        ("bold", 700, args.bold),
    ]
    for _, _, path in weight_paths:
        if not path.exists():
            raise SystemExit(f"font file not found: {path}")

    faces = [Face(label, weight, size, path) for label, weight, path in weight_paths for size in sizes]
    chars = default_coverage()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    header = args.output_dir / "jellyframe_esp32s3_noto_sans_sc_font.h"
    source = args.output_dir / "jellyframe_esp32s3_noto_sans_sc_font.cpp"
    write_header(header)
    stats = write_source(source, header.name, faces, chars, args.bits_per_pixel)
    print(
        "generated "
        f"{stats['faces']} faces, {stats['coverage']} chars, "
        f"{stats['glyphs']} glyphs, {stats['bitmap_bytes']} bitmap bytes, "
        f"{args.bits_per_pixel}bpp"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
