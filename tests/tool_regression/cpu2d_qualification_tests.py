"""Exercise qualification output and ensure it cannot become a timing report."""

import argparse
import importlib.util
import json
import hashlib
import math
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
        cost_root = root / "cost"
        subprocess.run([str(runner), str(cost_root), "3", "rounded-quality-cost"], check=True, capture_output=True)
        cost_loaded, _ = quality.load_masks(cost_root / "coverage.json")
        assert loaded == cost_loaded, "timed draw output differs from untimed quality fixture"
        quality.attach_cost(report, cost_loaded, cost_root / "cost.json")
        assert report["format"] == quality.JOINT_FORMAT
        assert report["performanceMeasured"] is True and report["performanceComparable"] is False
        assert report["sampleCount"] == 3
        assert all(len(case["cost"]["paintUs"]) == 3 for backend in report["backends"] for case in backend["cases"])
        assert "no speed ranking" in quality.markdown(report)
        for suffix in (["10001", "rounded-quality-cost"], ["3", "rounded-quality-cost", "sdl2"]):
            rejected = root / "bad-cost"
            result = subprocess.run([str(runner), str(rejected), *suffix], capture_output=True, text=True)
            assert result.returncode != 0 and not rejected.exists()
        probe_root = root / "core-probes"
        subprocess.run([str(runner), str(probe_root), "6", "rounded-core-probes"], check=True, capture_output=True)
        probes = json.loads((probe_root / "probes.json").read_text(encoding="utf-8"))
        assert probes["format"] == "jellyframe.rounded.core-probes.v0"
        assert probes["productionPhaseTimings"] is False and probes["performanceComparable"] is False
        assert probes["sampleCount"] == 6 and probes["warmupIterations"] == 30
        assert probes["timingContract"]["operationsPerSample"] == 1
        assert {case["name"] for case in probes["cases"]} == expected_cases
        geometry = {case["name"]: case for case in probes["fixtures"]}
        for case in probes["cases"]:
            fixture = geometry[case["name"]]
            x0, y0, width, height = fixture["rect"]
            radius = fixture["radius"]
            pgm = (quality_root / "core-quarter-grid" / (case["name"] + ".pgm")).read_bytes()
            assert case["maskSha256"] == hashlib.sha256(pgm).hexdigest()
            assert case["outputValidation"] == "production-replay-oracle-exact"
            pixels = pgm.split(b"\n", 3)[3]
            center = 0
            corner_values = []
            for y in range(max(0, y0), min(320, y0 + height)):
                for x in range(max(0, x0), min(172, x0 + width)):
                    corner = ((x < x0 + radius or x >= x0 + width - radius) and
                              (y < y0 + radius or y >= y0 + height - radius))
                    if corner:
                        corner_values.append(pixels[y * 172 + x])
                    else:
                        center += 1
            counts = case["workCounts"]
            assert counts["source"] == "untimed-probe-plan"
            assert counts["centerPixels"] == center
            assert counts["cornerPixels"] == len(corner_values)
            assert counts["sampleTests"] == len(corner_values) * 16
            assert counts["cornerZero"] == corner_values.count(0)
            assert counts["cornerFull"] == corner_values.count(255)
            assert counts["cornerPartial"] == sum(0 < value < 255 for value in corner_values)
            assert [p["id"] for p in case["probes"]] == ["production-draw", "isolated-coverage", "cached-writes"]
            for probe in case["probes"]:
                assert len(probe["us"]) == 6
                assert all(math.isfinite(value) and value >= 0 for value in probe["us"])
                assert abs(probe["p50Us"] - sorted(probe["us"])[2]) <= 0.001
                assert abs(probe["p95Us"] - max(probe["us"])) <= 0.001
        try:
            module.load_run(probe_root / "probes.json")
        except SystemExit as error:
            assert "unsupported benchmark run format" in str(error)
        else:
            raise AssertionError("isolated probes accepted as comparable performance")
        assert "not production phase timings" in (probe_root / "report.md").read_text(encoding="utf-8")
        for suffix in (["10001", "rounded-core-probes"], ["6", "rounded-core-probes", "sdl2"]):
            rejected = root / "bad-probes"
            result = subprocess.run([str(runner), str(rejected), *suffix], capture_output=True, text=True)
            assert result.returncode != 0 and not rejected.exists()
    print("rounded qualification contract passed")


if __name__ == "__main__":
    main()
