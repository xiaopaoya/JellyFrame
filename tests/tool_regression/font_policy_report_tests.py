#!/usr/bin/env python3
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
FONT_POLICY = REPO_ROOT / "samples" / "apps" / "packages" / "jelly_font_policy"
FONT_SIZE_WARNING = REPO_ROOT / "samples" / "apps" / "packages" / "jelly_font_size_warning"


def run(command: list[str], capture: bool = False) -> subprocess.CompletedProcess:
    print("+ " + " ".join(str(item) for item in command), flush=True)
    return subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )


def assert_font_report(report_path: Path) -> None:
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    diagnostics = report["fontDiagnostics"]
    assert diagnostics["usableRuntimeFontCount"] == 2
    assert diagnostics["runtimeFontGlyphs"] == 5
    assert diagnostics["missingNonAsciiCodepointCount"] == 0

    fonts = {font["id"]: font for font in diagnostics["manifestFonts"]}
    assert fonts["tiny-cn"]["status"] == "usable"
    assert fonts["tiny-cn"]["sizes"] == [8, 12, 18, 36]
    assert fonts["tiny-cn"]["weights"] == [400]
    assert fonts["tiny-cn"]["usedGlyphCount"] == 2
    assert fonts["tiny-cn"]["nativeSize"] == 16
    assert fonts["tiny-cn"]["renderableSizes"] == []
    assert fonts["tiny-cn"]["usedSizes"] == [12]
    assert fonts["tiny-cn"]["declaredUnavailableSizes"] == []
    assert fonts["tiny-cn"]["unavailableUsedSizes"] == []
    assert fonts["tiny-symbols"]["status"] == "usable"
    assert fonts["tiny-symbols"]["sizes"] == [8, 12, 18, 36]
    assert fonts["tiny-symbols"]["weights"] == [400]
    assert fonts["tiny-symbols"]["usedGlyphCount"] == 3
    assert fonts["tiny-symbols"]["nativeSize"] == 16
    assert fonts["tiny-symbols"]["usedSizes"] == [18]
    assert fonts["tiny-symbols"]["declaredUnavailableSizes"] == []
    assert fonts["tiny-symbols"]["unavailableUsedSizes"] == []

    size_usage = diagnostics["fontSizeUsage"]
    assert size_usage["usedSizes"] == [12, 13, 14, 18, 28]
    assert size_usage["unresolved"] == []

    family_status = {
        entry["family"]: entry["status"]
        for entry in diagnostics["fontFamilyUsage"]["entries"]
    }
    assert family_status["Jelly Tiny CN"] == "manifest-runtime-font"
    assert family_status["Jelly Tiny Symbols"] == "manifest-runtime-font"
    assert diagnostics["fontFamilyUsage"]["unmatchedPrimaryCount"] == 0

    warnings = report.get("warnings", [])
    assert not warnings

    subset = report["fontSubset"]
    assert subset["mode"] == "auto"
    assert subset["generated"] is False
    used_chars = Path(subset["usedChars"])
    assert used_chars.is_file()
    assert "\u4e2d" in used_chars.read_text(encoding="utf-8")


def assert_font_size_warning_report(report_path: Path) -> None:
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    warnings = report.get("warnings", [])
    assert not any(w["code"] == "font-size-unavailable" for w in warnings)
    font = report["fontDiagnostics"]["manifestFonts"][0]
    assert font["id"] == "size-probe" and font["scalable"]
    assert font["usedSizes"] == [12]
    assert font["declaredUnavailableSizes"] == []
    assert font["unavailableUsedSizes"] == []


def main() -> int:
    if len(sys.argv) not in (3, 5):
        print("usage: font_policy_report_tests.py WIN32_BROWSER_EXE BUILD_TOOL_DIR [FONT_POLICY_DIR FONT_SIZE_WARNING_DIR]", file=sys.stderr)
        return 2
    win32_browser = Path(sys.argv[1])
    build_tool_dir = Path(sys.argv[2])
    font_policy = Path(sys.argv[3]) if len(sys.argv) == 5 else FONT_POLICY
    font_size_warning = Path(sys.argv[4]) if len(sys.argv) == 5 else FONT_SIZE_WARNING
    with tempfile.TemporaryDirectory(prefix="jellyframe-font-policy-") as directory:
        output_dir = Path(directory)
        report = output_dir / "font_policy.report.json"
        capture = output_dir / "font_policy.bmp"
        run([
            sys.executable,
            str(REPO_ROOT / "tools" / "jellyframe_cli.py"),
            "check",
            "--root",
            str(font_policy),
            "--target",
            "round-300",
            "--report",
            str(report),
            "--build-dir",
            str(build_tool_dir),
        ])
        assert_font_report(report)

        result = run([
            str(win32_browser),
            "--capture",
            str(capture),
            "--app",
            str(font_policy),
            "--viewport-width",
            "300",
            "--viewport-height",
            "300",
            "--use-app-fonts",
        ], capture=True)
        output = result.stdout or ""
        print(output, end="")
        assert "diagnostics: 0" in output
        assert "app_fonts=on" in output
        assert "text_backend=core-text" in output
        assert capture.is_file() and capture.stat().st_size > 54

        unsupported = output_dir / "unsupported-kana"
        shutil.copytree(font_policy, unsupported)
        entry = unsupported / "index.html"
        html = entry.read_text(encoding="utf-8")
        assert "&#x4E2D;" in html
        entry.write_text(html.replace("&#x4E2D;", "\u3042"), encoding="utf-8")
        rejected_report = output_dir / "unsupported-kana.report.json"
        rejected = subprocess.run([
            sys.executable, str(REPO_ROOT / "tools" / "jellyframe_cli.py"), "check",
            "--root", str(unsupported), "--target", "round-300", "--report", str(rejected_report),
            "--build-dir", str(build_tool_dir), "--strict",
        ], cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        rejected_output = rejected.stdout + rejected.stderr
        assert rejected.returncode != 0, rejected_output
        assert "layout refused" in rejected_output or "layout-core-text-refused" in rejected_output, rejected_output
        missing = json.loads(rejected_report.read_text(encoding="utf-8"))["fontDiagnostics"]
        assert missing["missingNonAsciiCodepointCount"] == 1
        assert missing["missingNonAsciiSample"][0]["codepoint"] == "U+3042"

        warning_report = output_dir / "font_size_warning.report.json"
        warning_bundle = output_dir / "font_size_warning.jfapp"
        run([
            sys.executable,
            str(REPO_ROOT / "tools" / "package_app.py"),
            "--root",
            str(font_size_warning),
            "--target",
            "rect-172x320",
            "--report",
            str(warning_report),
            "--output-bundle",
            str(warning_bundle),
        ])
        assert_font_size_warning_report(warning_report)
        assert warning_bundle.is_file() and warning_bundle.stat().st_size > 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
