#!/usr/bin/env python3
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WATCH_WEATHER = REPO_ROOT / "samples" / "apps" / "packages" / "watch_weather"


def run_command(command: list[str]) -> None:
    print("+ " + " ".join(str(item) for item in command))
    subprocess.run(command, cwd=REPO_ROOT, check=True)


def read_bmp_pixels(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise AssertionError(f"not a BMP file: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0 or bits_per_pixel != 24 or compression != 0:
        raise AssertionError(f"unsupported capture BMP format: {path}")
    height = abs(signed_height)
    stride = ((width * bits_per_pixel + 31) // 32) * 4
    pixels = []
    for y in range(height):
        source_y = y if signed_height < 0 else height - 1 - y
        row = pixel_offset + source_y * stride
        for x in range(width):
            b, g, r = data[row + x * 3:row + x * 3 + 3]
            pixels.append((r, g, b))
    return width, height, pixels


def pixel_at(pixels: list[tuple[int, int, int]], width: int, x: int, y: int) -> tuple[int, int, int]:
    return pixels[y * width + x]


def assert_weather_icon_pixels(capture: Path) -> None:
    width, height, pixels = read_bmp_pixels(capture)
    if (width, height) != (300, 300):
        raise AssertionError(f"unexpected capture size: {width}x{height}")
    # The 64px package icon is in the upper-right hero tile. Require both
    # cloud cyan and sun amber, so text or the dark card cannot satisfy this.
    x1, y1, x2, y2 = 194, 60, 274, 140
    cloud_pixels = 0
    sun_pixels = 0
    for y in range(y1, y2):
        for x in range(x1, x2):
            color = pixel_at(pixels, width, x, y)
            r, g, b = color
            if 70 <= r <= 130 and g >= 190 and b >= 190:
                cloud_pixels += 1
            if r >= 220 and 150 <= g <= 220 and 70 <= b <= 160:
                sun_pixels += 1
    print(f"icon_region cloud_pixels={cloud_pixels} sun_pixels={sun_pixels}")
    if cloud_pixels < 300 or sun_pixels < 100:
        raise AssertionError("package BMP icon did not appear in the Win32 capture")


def assert_image_diagnostics(report_path: Path) -> None:
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    diagnostics = report.get("imageDiagnostics", {})
    if diagnostics.get("imageCount") != 4:
        raise AssertionError(f"unexpected image count: {diagnostics.get('imageCount')}")
    if diagnostics.get("codecCounts") != {"bmp": 4}:
        raise AssertionError(f"unexpected codec counts: {diagnostics.get('codecCounts')}")
    for entry in diagnostics.get("entries", []):
        metadata = entry.get("metadata", {})
        if entry.get("targetSupport") != "supported" or entry.get("codec") != "bmp":
            raise AssertionError(f"unexpected image entry support: {entry}")
        if metadata.get("width") != 64 or metadata.get("height") != 64:
            raise AssertionError(f"unexpected image metadata: {metadata}")
        if metadata.get("bitsPerPixel") != 24 or metadata.get("compression") != 0:
            raise AssertionError(f"unexpected BMP format metadata: {metadata}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: package_image_fixture_tests.py WIN32_BROWSER_EXE BUILD_TOOL_DIR", file=sys.stderr)
        return 2
    win32_browser = Path(sys.argv[1])
    build_tool_dir = Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="jellyframe-package-image-") as directory:
        output_dir = Path(directory)
        report = output_dir / "watch_weather.report.json"
        capture = output_dir / "watch_weather.bmp"
        run_command([
            sys.executable,
            str(REPO_ROOT / "tools" / "jellyframe_cli.py"),
            "check",
            "--root",
            str(WATCH_WEATHER),
            "--target",
            "round-300",
            "--report",
            str(report),
            "--build-dir",
            str(build_tool_dir),
        ])
        assert_image_diagnostics(report)
        run_command([
            str(win32_browser),
            "--capture",
            str(capture),
            "--app",
            str(WATCH_WEATHER),
            "--viewport-width",
            "300",
            "--viewport-height",
            "300",
        ])
        assert_weather_icon_pixels(capture)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
