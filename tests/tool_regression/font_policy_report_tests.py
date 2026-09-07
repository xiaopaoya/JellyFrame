#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
FONT_POLICY = REPO_ROOT / "samples" / "apps" / "packages" / "jelly_font_policy"


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
    assert diagnostics["missingNonAsciiCodepointCount"] == 1
    assert diagnostics["missingNonAsciiSample"][0]["codepoint"] == "U+3042"

    fonts = {font["id"]: font for font in diagnostics["manifestFonts"]}
    assert fonts["tiny-cn"]["status"] == "usable"
    assert fonts["tiny-cn"]["sizes"] == [8, 12, 18, 36]
    assert fonts["tiny-cn"]["weights"] == [400, 700]
    assert fonts["tiny-cn"]["representableSizes"] == [8, 16, 24, 32, 40, 48, 56, 64]
    assert fonts["tiny-cn"]["effectiveSizes"] == [8]
    assert fonts["tiny-cn"]["usedGlyphCount"] == 2
    assert fonts["tiny-symbols"]["status"] == "usable"
    assert fonts["tiny-symbols"]["sizes"] == [8, 12, 18, 36]
    assert fonts["tiny-symbols"]["weights"] == [400, 700]
    assert fonts["tiny-symbols"]["representableSizes"] == [8, 16, 24, 32, 40, 48, 56, 64]
    assert fonts["tiny-symbols"]["effectiveSizes"] == [8]
    assert fonts["tiny-symbols"]["usedGlyphCount"] == 3

    family_status = {
        entry["family"]: entry["status"]
        for entry in diagnostics["fontFamilyUsage"]["entries"]
    }
    assert family_status["Jelly Tiny CN"] == "manifest-runtime-font"
    assert family_status["Jelly Tiny Symbols"] == "manifest-runtime-font"
    assert diagnostics["fontFamilyUsage"]["unmatchedPrimaryCount"] == 0

    warnings = report.get("warnings", [])
    assert [warning["code"] for warning in warnings] == [
        "font-size-metadata-inconsistent",
        "font-size-metadata-inconsistent",
        "font-size-not-declared",
        "font-size-not-declared",
        "font-missing-glyphs",
    ]
    assert diagnostics["fontSizeUsage"]["undeclaredCount"] == 2
    assert {
        (entry["family"], entry["requestedSize"])
        for entry in diagnostics["fontSizeUsage"]["entries"]
        if entry["status"] == "undeclared"
    } == {("Jelly Tiny Symbols", 18), ("Jelly Tiny CN", 12)}

    subset = report["fontSubset"]
    assert subset["mode"] == "auto"
    assert subset["generated"] is False
    used_chars = Path(subset["usedChars"])
    assert used_chars.is_file()
    assert "あ" in used_chars.read_text(encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: font_policy_report_tests.py WIN32_BROWSER_EXE BUILD_TOOL_DIR", file=sys.stderr)
        return 2
    win32_browser = Path(sys.argv[1])
    build_tool_dir = Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="jellyframe-font-policy-") as directory:
        output_dir = Path(directory)
        report = output_dir / "font_policy.report.json"
        capture = output_dir / "font_policy.bmp"
        run([
            sys.executable,
            str(REPO_ROOT / "tools" / "jellyframe_cli.py"),
            "check",
            "--root",
            str(FONT_POLICY),
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
            str(FONT_POLICY),
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
        assert capture.is_file() and capture.stat().st_size > 54
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
