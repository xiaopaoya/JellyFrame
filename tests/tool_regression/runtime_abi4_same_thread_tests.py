import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_ppm_pixel(path: Path, x: int, y: int) -> tuple[int, int, int]:
    data = path.read_bytes()
    require(data.startswith(b"P6\n"), "desktop capture must be a binary PPM")
    header_end = data.find(b"\n255\n")
    require(header_end >= 0, "desktop PPM must declare 8-bit RGB pixels")
    header = data[:header_end].decode("ascii").split()
    width, height = int(header[1]), int(header[2])
    require(0 <= x < width and 0 <= y < height, "pixel probe must be inside the capture")
    offset = header_end + len(b"\n255\n") + (y * width + x) * 3
    return tuple(data[offset : offset + 3])


def write_solid_bmp(path: Path, width: int, height: int, rgb: tuple[int, int, int]) -> None:
    row_bytes = (width * 3 + 3) & ~3
    red, green, blue = rgb
    row = bytes((blue, green, red)) * width + bytes(row_bytes - width * 3)
    pixels = row * height
    dib = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    header = b"BM" + struct.pack("<IHHI", 14 + len(dib) + len(pixels), 0, 0, 14 + len(dib))
    path.write_bytes(header + dib + pixels)


def main() -> int:
    require(len(sys.argv) == 2, "usage: runtime_abi4_same_thread_tests.py DESKTOP_SHELL")
    executable = Path(sys.argv[1]).resolve()
    require(executable.is_file(), f"desktop shell does not exist: {executable}")

    with tempfile.TemporaryDirectory(prefix="jellyframe-runtime-abi4-same-thread-") as temporary:
        root = Path(temporary)
        manifest = {
            "format": "jellyframe.app",
            "formatVersion": 0,
            "id": "org.jellyframe.tests.abi4-same-thread",
            "name": "ABI4 Same Thread Probe",
            "version": {"name": "1.0.0", "code": 1},
            "entry": "/index.html",
            "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.9.0-dev", "script": "none"},
            "viewport": {"designWidth": 96, "designHeight": 64, "shape": "rect"},
            "budgets": {"maxResourceBytes": 32768, "maxDomNodes": 32, "maxCssRules": 16,
                         "maxDisplayCommands": 64, "maxTimers": 0, "maxEventListeners": 0},
            "capabilities": [],
            "targets": {},
        }
        (root / "jellyframe.app.json").write_text(json.dumps(manifest), encoding="utf-8")
        (root / "index.html").write_text(
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<link rel='stylesheet' href='styles/app.css'></head><body>"
            "<main id='probe'></main></body></html>",
            encoding="utf-8",
        )
        styles = root / "styles"
        styles.mkdir()
        (styles / "app.css").write_text(
            "html, body { margin: 0; width: 96px; height: 64px; background: #ffffff; }\n"
            "#probe { width: 96px; height: 64px; background: "
            "url('/assets/green.bmp'), linear-gradient(#0000ff, #0000ff); }\n",
            encoding="ascii",
        )
        assets = root / "assets"
        assets.mkdir()
        write_solid_bmp(assets / "green.bmp", 96, 64, (0, 255, 0))
        capture = root / "capture.ppm"
        result = subprocess.run(
            [str(executable), "--app", str(root), "--capture", str(capture),
             "--viewport-width", "96", "--viewport-height", "64"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        require(result.returncode == 0, f"ABI4 same-thread app launch failed: {result.stdout}")
        require(capture.is_file(), f"desktop shell did not write the capture: {result.stdout}")
        actual = read_ppm_pixel(capture, 48, 32)
        require(actual == (0, 255, 0),
                f"topmost canonical CSS image layer must paint over its gradient; got {actual}; {result.stdout}")
        print("ABI4 same-thread image and gradient layers passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
