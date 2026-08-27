#!/usr/bin/env python3
"""Regenerate README gallery images through the selected desktop shell."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib
from datetime import date
from pathlib import Path


GALLERY = (("weather", "weather"), ("clock", "clock"),
           ("timer", "timer"), ("calculator", "calculator"))


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_render_core_version(root: Path) -> str:
    text = (root / "cmake" / "render_core_version.cmake").read_text(encoding="utf-8")
    match = re.search(r'JELLYFRAME_RENDER_CORE_PACKAGE_VERSION\s+"([^"]+)"', text)
    if match is None:
        raise RuntimeError("could not read the locked Render Core package version")
    return match.group(1)


def source_revision(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=root, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    return result.stdout.strip()


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError(f"expected binary PPM capture: {path}")
    cursor = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while cursor < len(data) and data[cursor] in b" \t\r\n":
            cursor += 1
        if cursor < len(data) and data[cursor] == ord("#"):
            while cursor < len(data) and data[cursor] not in b"\r\n":
                cursor += 1
            continue
        end = cursor
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[cursor:end])
        cursor = end
    if cursor >= len(data) or data[cursor] not in b" \t\r\n":
        raise RuntimeError(f"missing PPM header separator: {path}")
    # PPM has exactly one whitespace separator after the max-value token. Do
    # not skip arbitrary whitespace here: the first RGB byte is allowed to be
    # a whitespace value.
    if data[cursor:cursor + 2] == b"\r\n":
        cursor += 2
    else:
        cursor += 1
    width, height, maximum = (int(token) for token in tokens)
    if maximum != 255:
        raise RuntimeError(f"unsupported PPM range in {path}: {maximum}")
    pixels = data[cursor:]
    if len(pixels) != width * height * 3:
        raise RuntimeError(f"truncated PPM capture: {path}")
    return width, height, pixels


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def write_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    scanlines = b"".join(
        b"\x00" + pixels[row * width * 3:(row + 1) * width * 3]
        for row in range(height)
    )
    encoded = (b"\x89PNG\r\n\x1a\n" +
               png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
               png_chunk(b"IDAT", zlib.compress(scanlines, level=9)) +
               png_chunk(b"IEND", b""))
    path.write_bytes(encoded)


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--desktop-shell", type=Path,
        default=root / "build" / "script-engine-desktop" / "Debug" / "jellyframe_desktop_shell.exe",
        help="current JellyFrame desktop shell executable",
    )
    parser.add_argument(
        "--output-dir", type=Path, default=root / "docs" / "assets" / "screenshots",
        help="directory for gallery PNGs and provenance JSON",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    shell = args.desktop_shell.resolve()
    output_dir = args.output_dir.resolve()
    if not shell.is_file():
        raise SystemExit(f"desktop shell not found: {shell}")
    output_dir.mkdir(parents=True, exist_ok=True)

    generated: list[dict[str, str]] = []
    for template, output_name in GALLERY:
        capture = output_dir / f".{output_name}.ppm"
        command = [
            str(shell), "--app", str(root / "tools" / "templates" / "apps" / template),
            "--capture", str(capture), "--viewport-width", "300", "--viewport-height", "300",
        ]
        try:
            subprocess.run(command, cwd=root, check=True)
            width, height, pixels = read_ppm(capture)
            write_png(output_dir / f"{output_name}.png", width, height, pixels)
            generated.append({"template": template, "image": f"{output_name}.png"})
        finally:
            capture.unlink(missing_ok=True)

    shell_identity = str(shell.relative_to(root)) if shell.is_relative_to(root) else str(shell)
    provenance = {
        "schemaVersion": 1,
        "renderCoreVersion": read_render_core_version(root),
        "sourceRevision": source_revision(root),
        "desktopShell": shell_identity,
        "desktopShellSha256": hashlib.sha256(shell.read_bytes()).hexdigest(),
        "viewport": {"width": 300, "height": 300},
        "generated": date.today().isoformat(),
        "images": generated,
    }
    (output_dir / "gallery-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(provenance, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
