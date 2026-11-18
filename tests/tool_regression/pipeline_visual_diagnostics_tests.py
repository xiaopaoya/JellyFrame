import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED_TEXT_POLICY = "geometry-only"

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_pseudo_browser(exe: Path, html: str, css: str, width: int, height: int) -> dict:
    with tempfile.TemporaryDirectory(prefix="jellyframe-visual-diagnostics-") as directory:
        root = Path(directory)
        html_path = root / "index.html"
        css_path = root / "style.css"
        output_path = root / "out.bmp"
        report_path = root / "pipeline.json"
        html_path.write_text(html, encoding="utf-8")
        css_path.write_text(css, encoding="utf-8")
        result = subprocess.run(
            [
                str(exe),
                str(html_path),
                str(css_path),
                str(output_path),
                str(width),
                str(height),
                "--diagnostics-json",
                str(report_path),
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        require(result.returncode == 0, result.stdout)
        require(report_path.is_file(), "pseudo browser did not write diagnostics JSON")
        report = json.loads(report_path.read_text(encoding="utf-8-sig"))
        require(report.get("textPolicy") == EXPECTED_TEXT_POLICY,
                f"diagnostics must identify the actual text route: {EXPECTED_TEXT_POLICY}")
        return report


def diagnostic_codes(report: dict) -> set[str]:
    return {
        str(entry.get("code", ""))
        for entry in report.get("diagnostics", [])
        if isinstance(entry, dict)
    }


def check_image_preflight(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="jellyframe-image-preflight-") as directory:
        root = Path(directory)
        (root / "pages").mkdir()
        html = root / "pages" / "index.html"
        css = root / "style.css"
        output = root / "out.bmp"
        report = root / "pipeline.json"
        # A 4x2 RGBA BMP makes natural dimensions distinguishable from explicit sizing.
        pixels = bytes([160, 220, 32, 255]) * 8
        header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, 4, 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
        (root / "icon.bmp").write_bytes(header + pixels)
        html.write_text('<body><img src="/icon.bmp"></body>', encoding="utf-8")
        css.write_text("body{margin:0;background:#07121f}img{display:block}", encoding="utf-8")
        command = [str(exe), str(html), str(css), str(output), "64", "64",
                   "--diagnostics-json", str(report), "--resource-root", str(root)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, result.stdout)
        require("paint-image-fallback" not in result.stdout, "image preflight must paint real pixels")
        data = output.read_bytes()
        offset = struct.unpack_from("<I", data, 10)[0]
        width, height = struct.unpack_from("<ii", data, 18)
        bits = struct.unpack_from("<H", data, 28)[0]
        stride = ((width * bits + 31) // 32) * 4
        def rgb(x: int, y: int) -> tuple[int, int, int]:
            row = height - y - 1 if height > 0 else y
            blue, green, red = data[offset + row * stride + x * (bits // 8):][:3]
            return red, green, blue
        require(rgb(0, 0) == (32, 220, 160), "natural image must paint selected ready surface")
        require(rgb(3, 1) == (32, 220, 160), "natural image must retain 4x2 decoded dimensions")
        require(rgb(4, 1) == (7, 18, 31) and rgb(3, 2) == (7, 18, 31),
                "natural image must end at its decoded bounds")
        require(json.loads(report.read_text(encoding="utf-8-sig"))["pipeline"]["displayCommands"] > 0,
                "image preflight must retain paint commands")
        pixels24 = bytes([160, 220, 32]) * 8
        header24 = b"BM" + struct.pack("<IHHI", 54 + len(pixels24), 0, 0, 54)
        header24 += struct.pack("<IiiHHIIiiII", 40, 4, 2, 1, 24, 0, len(pixels24), 0, 0, 0, 0)
        (root / "icon.bmp").write_bytes(header24 + pixels24)
        rgb24 = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(rgb24.returncode == 0 and "paint-image-fallback" not in rgb24.stdout, rgb24.stdout)
        data = output.read_bytes()
        require(rgb(0, 0) == (32, 220, 160) and rgb(3, 1) == (32, 220, 160),
                "24-bit BMP must retain its opaque decoded surface")
        (root / "icon.bmp").write_bytes(header + pixels)
        oversized_header = bytearray(header + pixels)
        struct.pack_into("<I", oversized_header, 14, 0xffffffff)
        overlapping_pixels = bytearray(header + pixels)
        struct.pack_into("<I", overlapping_pixels, 10, 20)
        for source, contents in (("missing.bmp", None), ("broken.bmp", b"BMbad"),
                                 ("truncated.bmp", header + pixels[:-1]),
                                 ("oversized-header.bmp", oversized_header),
                                 ("overlapping-pixels.bmp", overlapping_pixels)):
            if contents is not None:
                (root / source).write_bytes(contents)
            html.write_text(f'<body><img src="/{source}"></body>', encoding="utf-8")
            output.unlink(missing_ok=True)
            report.unlink(missing_ok=True)
            failed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            require(failed.returncode != 0 and "pseudo image backend unavailable" in failed.stdout,
                    f"unavailable image must fail through checked preflight: {failed.stdout}")
            require(not output.exists() and not report.exists(), "refusal must publish no image or success report")
        # Hidden images do not require a decoder or alter the visible preflight.
        html.write_text('<body><img style="display:none" src="/missing.bmp"></body>', encoding="utf-8")
        hidden = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(hidden.returncode == 0, hidden.stdout)
        html.write_text('<body><img src="/icon.bmp"></body>', encoding="utf-8")
        css.write_text("body{margin:0;background:#07121f}img{position:absolute;left:16px;top:16px;width:32px;height:32px}",
                       encoding="utf-8")
        positioned = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(positioned.returncode == 0, positioned.stdout)
        require("paint-image-fallback" not in positioned.stdout, "positioned image must paint real pixels")
        data = output.read_bytes()
        require(rgb(16, 16) == (32, 220, 160) and rgb(47, 47) == (32, 220, 160),
                "positioned image must retain its explicit 32 by 32 geometry")
        require(rgb(15, 16) == (7, 18, 31) and rgb(48, 47) == (7, 18, 31),
                "positioned image must preserve its selected bounds")


def check_package_stylesheet_preflight(exe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="jellyframe-package-css-") as directory:
        root = Path(directory)
        (root / "pages").mkdir()
        entry = root / "pages" / "index.html"
        entry.write_text("<link rel='stylesheet' href='/theme.css'>"
                         "<link rel='stylesheet' href='local.css'>"
                         "<body><div id='box'></div></body>", encoding="utf-8")
        (root / "theme.css").write_text("body{margin:0;background:#07121f}", encoding="utf-8")
        (root / "pages" / "local.css").write_text(
            "#box{width:12px;height:8px;background:#20dca0}", encoding="utf-8")
        empty_css = root / "empty.css"
        empty_css.write_text("", encoding="utf-8")
        output, report = root / "capture.bmp", root / "report.json"
        command = [str(exe), str(entry), str(empty_css), str(output), "32", "24",
                   "--resource-root", str(root), "--diagnostics-json", str(report)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, result.stdout)
        diagnostics = json.loads(report.read_text(encoding="utf-8-sig"))
        require(not diagnostic_codes(diagnostics), "package-root and relative stylesheets must both load")
        data = output.read_bytes()
        offset = struct.unpack_from("<I", data, 10)[0]
        width, height = struct.unpack_from("<ii", data, 18)
        bits = struct.unpack_from("<H", data, 28)[0]
        stride = ((width * bits + 31) // 32) * 4
        def rgb(x: int, y: int) -> tuple[int, int, int]:
            row = height - y - 1 if height > 0 else y
            blue, green, red = data[offset + row * stride + x * (bits // 8):][:3]
            return red, green, blue
        require(rgb(0, 0) == (32, 220, 160) and rgb(11, 7) == (32, 220, 160),
                "nested package entry must use its relative stylesheet")
        require(rgb(12, 7) == (7, 18, 31) and rgb(0, 8) == (7, 18, 31),
                "root-relative stylesheet must provide the package background")
        entry.write_text("<link rel='stylesheet' href='/../outside.css'><body></body>", encoding="utf-8")
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, result.stdout)
        require("package-resource-rejected" in diagnostic_codes(
            json.loads(report.read_text(encoding="utf-8-sig"))), "stylesheet traversal must use package URL rules")


def main() -> int:
    global EXPECTED_TEXT_POLICY
    parser = argparse.ArgumentParser()
    parser.add_argument("pseudo", type=Path)
    parser.add_argument("pipeline_dump", type=Path)
    parser.add_argument("--image-preflight", action="store_true")
    parser.add_argument("--text-policy", choices=("geometry-only", "core-text"), default="geometry-only")
    args = parser.parse_args()
    exe = args.pseudo
    pipeline_dump = args.pipeline_dump
    EXPECTED_TEXT_POLICY = args.text_policy
    require(exe.exists(), f"missing executable: {exe}")
    require(pipeline_dump.exists(), f"missing executable: {pipeline_dump}")
    repo_root = Path(__file__).resolve().parents[2]
    check_package_stylesheet_preflight(exe)
    if args.image_preflight:
        check_image_preflight(exe)

    overflow_report = run_pseudo_browser(
        exe,
        "<body><div class='wide'></div></body>",
        "body { margin: 0; } .wide { width: 400px; height: 24px; background: #224466; }",
        160,
        120,
    )
    require("visual-horizontal-overflow" in diagnostic_codes(overflow_report),
            "horizontal paint overflow should be reported")
    horizontal = next(entry for entry in overflow_report.get("diagnostics", [])
                      if entry.get("code") == "visual-horizontal-overflow")
    require("viewport=160x120" in horizontal.get("detail", ""),
            "horizontal overflow detail should include viewport")
    require("overflowRight=240" in horizontal.get("detail", ""),
            "horizontal overflow detail should include overflowRight")
    require('node="div.wide"' in horizontal.get("detail", ""),
            "horizontal overflow detail should include compact node label when a layout box caused it")
    require('path="' in horizontal.get("detail", "") and "div.wide" in horizontal.get("detail", ""),
            "horizontal overflow detail should include stable DOM path when a layout box caused it")
    require("boxOverflowRight=240" in horizontal.get("detail", ""),
            "horizontal overflow detail should include box overflow metrics")

    vertical_report = run_pseudo_browser(
        exe,
        "<body><div class='up'></div></body>",
        "body { margin: 0; } .up { position: absolute; top: -10px; left: 8px; width: 30px; height: 20px; background: #000; }",
        100,
        80,
    )
    require("visual-vertical-paint-overflow" in diagnostic_codes(vertical_report),
            "vertical paint overflow should be reported")
    vertical = next(entry for entry in vertical_report.get("diagnostics", [])
                    if entry.get("code") == "visual-vertical-paint-overflow")
    require('node="div.up"' in vertical.get("detail", ""),
            "vertical overflow detail should include compact node label when a layout box caused it")
    require('path="' in vertical.get("detail", "") and "div.up" in vertical.get("detail", ""),
            "vertical overflow detail should include stable DOM path when a layout box caused it")
    require("boxOverflowTop=10" in vertical.get("detail", ""),
            "vertical overflow detail should include box overflow metrics")

    dense_html = "<body>" + "".join("<i></i>" for _ in range(600)) + "</body>"
    dense_report = run_pseudo_browser(
        exe,
        dense_html,
        "body { margin: 0; } i { display: block; width: 1px; height: 1px; background: #000; }",
        100,
        100,
    )
    codes = diagnostic_codes(dense_report)
    require("visual-display-command-density" in codes, "display command density should be reported")
    require("visual-scroll-needed" in codes, "vertical overflow should be reported as scroll-needed")
    density = next(entry for entry in dense_report.get("diagnostics", [])
                   if entry.get("code") == "visual-display-command-density")
    require("flattenedDisplayCommands=" in density.get("detail", ""),
            "density diagnostic should include the measured command count")
    require("viewportPixels=10000" in density.get("detail", ""),
            "density diagnostic should include the target pixel area")
    require("commandsPerKPixel=" in density.get("detail", ""),
            "density diagnostic should include a bounded command density")

    scroll_container_report = run_pseudo_browser(
        exe,
        "<body><section id='hours'><button>06</button><button>07</button><button>08</button></section></body>",
        "body { margin: 0; }"
        "#hours { width: 90px; height: 36px; overflow: scroll; }"
        "button { display: block; width: 90px; height: 24px; }",
        120,
        100,
    )
    require("visual-scroll-container" in diagnostic_codes(scroll_container_report),
            "internal scroll containers should report clipped scrollable content")
    scroll_container = next(entry for entry in scroll_container_report.get("diagnostics", [])
                            if entry.get("code") == "visual-scroll-container")
    require('node="section#hours"' in scroll_container.get("detail", ""),
            "scroll container diagnostic should include compact node label")
    require('path="' in scroll_container.get("detail", "") and "section#hours" in scroll_container.get("detail", ""),
            "scroll container diagnostic should include stable DOM path")

    nested_scroll_report = run_pseudo_browser(
        exe,
        "<body><section id='feed'><article id='agenda'><p>1</p><p>2</p><p>3</p></article><p>4</p><p>5</p></section></body>",
        "body { margin: 0; }"
        "#feed { width: 100px; height: 56px; overflow: auto; }"
        "#agenda { width: 100px; height: 32px; overflow: scroll; }"
        "p { display: block; width: 100px; height: 20px; margin: 0; }",
        120,
        100,
    )
    require("visual-nested-scroll-container" in diagnostic_codes(nested_scroll_report),
            "nested overflowing scroll containers should report gesture competition")
    nested_scroll = next(entry for entry in nested_scroll_report.get("diagnostics", [])
                         if entry.get("code") == "visual-nested-scroll-container")
    require('node="article#agenda"' in nested_scroll.get("detail", ""),
            "nested scroll diagnostic should identify the inner scroll container")
    require('ancestorNode="section#feed"' in nested_scroll.get("detail", ""),
            "nested scroll diagnostic should identify the overflowing ancestor")
    require('ancestorPath="' in nested_scroll.get("detail", "") and "section#feed" in nested_scroll.get("detail", ""),
            "nested scroll diagnostic should include the ancestor DOM path")

    timer_template = repo_root / "tools" / "templates" / "apps" / "timer"
    timer_template_report = run_pseudo_browser(
        exe,
        (timer_template / "index.html").read_text(encoding="utf-8"),
        (timer_template / "styles" / "app.css").read_text(encoding="utf-8"),
        300,
        300,
    )
    timer_template_codes = diagnostic_codes(timer_template_report)
    require("layout-text-overflow" not in timer_template_codes,
            "timer template should fit its default round-300 text layout")
    require("visual-horizontal-overflow" not in timer_template_codes,
            "timer template should fit its default round-300 width")

    responsive_html = (
        "<body><main class='shell'><header><h1>Activity overview</h1>"
        "<span class='status'>Today</span></header><section class='metrics'>"
        "<article><strong>Steps</strong><b>8,420</b></article>"
        "<article><strong>Sleep</strong><b>7h 42m</b></article>"
        "<article class='optional'><strong>Recovery</strong><b>92%</b></article>"
        "</section><p class='summary'>A longer status message should wrap inside the available card width.</p>"
        "</main></body>"
    )
    responsive_css = (
        "body { margin: 0; } .shell { box-sizing: border-box; width: 100%; max-width: 100%; "
        "padding: 12px; } header { display: flex; width: 100%; justify-content: space-between; "
        "align-items: center; } h1, .status, .summary { overflow-wrap: anywhere; } h1 { margin: 0; "
        "font-size: 20px; } .metrics { display: flex; flex-wrap: wrap; gap: 8px; width: 100%; } "
        "article { box-sizing: border-box; flex: 1 1 72px; min-width: 0; padding: 8px; } "
        "article strong, article b { display: block; overflow-wrap: anywhere; } @media (max-width: 200px) { "
        ".optional { display: none; } }"
    )
    for width in (172, 240, 300):
        responsive_report = run_pseudo_browser(exe, responsive_html, responsive_css, width, 320)
        responsive_codes = diagnostic_codes(responsive_report)
        require("visual-horizontal-overflow" not in responsive_codes,
                f"responsive fixture must fit width {width}")
        require("layout-text-overflow" not in responsive_codes,
                f"responsive fixture text must fit width {width}")
        require(responsive_report.get("viewport", {}).get("width") == width,
                f"responsive fixture report must preserve viewport width {width}")
        require(responsive_report.get("pipeline", {}).get("displayCommands", 0) > 0,
                f"responsive fixture must produce paint commands at width {width}")

    with tempfile.TemporaryDirectory(prefix="jellyframe-pipeline-dump-") as directory:
        root = Path(directory)
        html_path = root / "index.html"
        css_path = root / "style.css"
        html_path.write_text("<body><p>diagnostic text</p></body>", encoding="utf-8")
        css_path.write_text("body { margin: 0; } p { font-size: 20px; }", encoding="utf-8")
        result = subprocess.run(
            [str(pipeline_dump), str(html_path), str(css_path), "120"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        require(result.returncode == 0, result.stdout)
        require("text_policy=geometry-only (no font metrics or glyph painting)" in result.stdout,
                "pipeline dump must identify its geometry-only text contract")

    print("pipeline visual diagnostics tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
