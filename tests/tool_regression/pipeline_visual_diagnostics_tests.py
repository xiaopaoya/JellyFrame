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

    overflow_report = run_pseudo_browser(
        exe,
        "<body><div class='wide'></div></body>",
        "body { margin: 0; } .wide { width: 400px; height: 24px; background: #224466; }",
        160,
        120,
    )
    require("visual-horizontal-overflow" in diagnostic_codes(overflow_report),
            "horizontal paint overflow should be reported")

    vertical_report = run_pseudo_browser(
        exe,
        "<body><div class='up'></div></body>",
        "body { margin: 0; } .up { position: absolute; top: -10px; left: 8px; width: 30px; height: 20px; background: #000; }",
        100,
        80,
    )
    require("visual-vertical-paint-overflow" in diagnostic_codes(vertical_report),
            "vertical paint overflow should be reported")

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

    print("pipeline visual diagnostics tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
