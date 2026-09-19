"""Exercise qualification output and ensure it cannot become a timing report."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    runner = args.runner.resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        result = subprocess.run([str(runner), str(root / "probe"), "1", "rounded-supersample-qualification"],
                                capture_output=True, text=True, check=True)
        report_path = root / "probe" / "qualification.json"
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["format"] == "jellyframe.benchmark.qualification.v0"
        assert report["performanceMeasured"] is False
        assert "measurements" not in report
        assert not (root / "probe" / "jellyframe.json").exists()
        assert not (root / "probe" / "gdi.json").exists()
        expected_cases = {"card", "rectangle", "small-radius", "odd-dimensions", "maximum-radius",
                          "clipped-left", "clipped-bottom-right", "tiny"}
        cases = report["cases"]
        assert len(cases) == len(expected_cases)
        assert {case["name"] for case in cases} == expected_cases
        failures = 0
        for case in cases:
            assert case["coreOraclePassed"] is True
            assert 0 <= case["coverageMismatches"] <= 172 * 320
            assert 0 <= case["maxCoverageError"] <= 255
            if case["coverageMismatches"]:
                failures += 1
                assert case["maxCoverageError"] > 0
            else:
                assert case["maxCoverageError"] == 0
                assert case["coreDigest"] == case["referenceDigest"]
            for name in ("core.bmp", "gdiplus.bmp", "difference.bmp"):
                image = (root / "probe" / case["name"] / name).read_bytes()
                assert image[:2] == b"BM" and len(image) > 172 * 320 * 3
        assert report["failedCases"] == failures
        assert report["status"] == ("not-comparable" if failures else "output-qualified")
        assert "performance_measured=false" in result.stdout
        rectangle = next(case for case in cases if case["name"] == "rectangle")
        assert rectangle["coverageMismatches"] == 0, "mask coordinate/stride control failed"

        tool = Path(__file__).resolve().parents[2] / "tools" / "benchmark_compare.py"
        spec = importlib.util.spec_from_file_location("benchmark_compare", tool)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        try:
            module.load_run(report_path)
        except SystemExit as error:
            assert "unsupported benchmark run format" in str(error)
        else:
            raise AssertionError("qualification report accepted as timing manifest")

        for suffix in (["2", "rounded-supersample-qualification"],
                       ["1", "rounded-supersample-qualification", "sdl2"]):
            rejected = root / "rejected"
            result = subprocess.run([str(runner), str(rejected), *suffix], capture_output=True, text=True)
            assert result.returncode != 0
            assert not rejected.exists(), "invalid request produced artifacts"
        quality_root = root / "quality"
        subprocess.run([str(runner), str(quality_root), "1", "rounded-quality-masks"], check=True, capture_output=True)
        tool = Path(__file__).resolve().parents[2] / "tools" / "rounded_aa_quality.py"
        spec = importlib.util.spec_from_file_location("rounded_aa_quality", tool)
        quality = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(quality)
        loaded, _ = quality.load_masks(quality_root / "coverage.json")
        report = quality.make_report(loaded)
        assert len(report["backends"]) == 3
        assert report["performanceMeasured"] is False and report["performanceComparable"] is False
        for backend in report["backends"]:
            assert len(backend["cases"]) == 8
            assert next(case for case in backend["cases"] if case["name"] == "rectangle")["status"] == "pass"
        binary = next(backend for backend in report["backends"] if backend["id"] == "gdiplus-binary-control")
        assert binary["status"] == "fail", "binary edges must not pass the native-AA quality gate"
    print("rounded qualification contract passed")


if __name__ == "__main__":
    main()
