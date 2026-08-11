import json
import subprocess
import sys
import tempfile
from pathlib import Path


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
        return json.loads(report_path.read_text(encoding="utf-8-sig"))


def diagnostic_codes(report: dict) -> set[str]:
    return {
        str(entry.get("code", ""))
        for entry in report.get("diagnostics", [])
        if isinstance(entry, dict)
    }


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: pipeline_visual_diagnostics_tests.py PATH_TO_PSEUDO_BROWSER")
        return 2

    exe = Path(sys.argv[1])
    require(exe.exists(), f"missing executable: {exe}")
    repo_root = Path(__file__).resolve().parents[2]

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

    print("pipeline visual diagnostics tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
