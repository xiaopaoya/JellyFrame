#!/usr/bin/env python3
"""Regression tests for fixed-condition benchmark comparisons."""

import importlib.util
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "benchmark_compare.py"


def load_module():
    spec = importlib.util.spec_from_file_location("benchmark_compare", TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BenchmarkCompareTests(unittest.TestCase):
    def setUp(self):
        self.module = load_module()
        self.base = {
            "format": self.module.FORMAT,
            "library": "jellyframe",
            "version": "0.6.0-dev",
            "workload": "opaque-fill",
            "viewport": {"width": 172, "height": 320},
            "pixelFormat": "rgba8888",
            "antialiasing": False,
            "mode": "full",
            "environment": {"os": "windows", "architecture": "x64", "cpu": "test-cpu", "buildType": "release"},
            "warmupIterations": 30,
            "workloadParameters": {
                "rect": {"x": 0, "y": 0, "width": 172, "height": 320},
                "sourceRgba": "164757ff",
                "blend": "source-over",
            },
            "outputValidation": {"status": "pass", "method": "rgba-sha256", "reference": "fixture-v1"},
            "measurements": {"frame_us": [100, 120, 110], "pixels": [10000, 10000, 10000]},
        }

    def test_comparable_result_has_percentiles_and_throughput(self):
        candidate = {**self.base, "library": "cairo", "version": "1.18", "measurements": {"frame_us": [80, 100, 90], "pixels": [10000, 10000, 10000]}}
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "comparable")
        frame = next(item for item in result["metrics"] if item["name"] == "frame_us")
        self.assertEqual(frame["baseline"]["p95"], 120)
        self.assertEqual(frame["candidate"]["p95"], 100)
        self.assertEqual(frame["deltaPercent"], -16.67)
        throughput = next(item for item in result["metrics"] if item["unit"] == "MPix/s")
        self.assertEqual(throughput["name"], "mpix_per_s_using_frame_us")
        self.assertEqual(result["fixedConditions"]["workloadParameters"], self.base["workloadParameters"])
        self.assertEqual(result["candidateFixedConditions"]["workloadParameters"], self.base["workloadParameters"])

    def test_fixed_condition_mismatch_is_not_comparable(self):
        candidate = {**self.base, "workload": "rounded-card"}
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["workload"])
        self.assertTrue(all(item["status"] == "not-comparable" for item in result["metrics"]))

    def test_full_and_dirty_modes_are_not_comparable(self):
        candidate = {**self.base, "mode": "dirty"}
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["mode"])

    def test_different_operation_batch_sizes_are_not_comparable(self):
        candidate = {**self.base, "operationsPerSample": 64}
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["operationsPerSample"])

    def test_different_workload_parameters_are_not_comparable(self):
        candidate = {
            **self.base,
            "workloadParameters": {
                **self.base["workloadParameters"],
                "sourceRgba": "ffffffff",
            },
        }
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["workloadParameters"])

    def test_missing_workload_parameters_on_one_side_is_not_comparable(self):
        candidate = dict(self.base)
        candidate.pop("workloadParameters")
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["workloadParameters"])

    def test_bitmap_text_font_and_placement_mismatch_is_rejected(self):
        parameters = {"fontIdentity": "clock-5x7-v1", "fontMasksHex": "708898a8c88870",
                      "fontSize": 14, "fontWeight": 400, "wrapWidth": 144,
                      "lineBreakMode": "fixed-lines-no-wrap", "scale": 2}
        baseline = {**self.base, "workload": "bitmap-clock-text-rgb-v1", "workloadParameters": parameters}
        for key in parameters:
            with self.subTest(field=key):
                candidate = {**baseline, "workloadParameters": {**parameters, key: "changed"}}
                result = self.module.compare_runs(baseline, candidate)
                self.assertEqual(result["status"], "not-comparable")
                self.assertEqual(result["fixedConditionMismatches"], ["workloadParameters"])

    def test_failed_output_validation_is_not_comparable(self):
        candidate = {**self.base, "outputValidation": {"status": "fail", "method": "rgba-sha256", "reference": "fixture-v1"}}
        result = self.module.compare_runs(self.base, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertEqual(result["fixedConditionMismatches"], ["outputValidation.status"])

    def test_loader_rejects_metrics_without_standard_units(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            path.write_text(json.dumps({
                **self.base,
                "measurements": {"mystery_score": [1, 2, 3]},
            }), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "has no standardized unit"):
                self.module.load_run(path)

    def test_qualification_report_is_not_a_performance_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "qualification.json"
            path.write_text(json.dumps({"format": "jellyframe.benchmark.qualification.v0",
                                        "status": "not-comparable", "performanceMeasured": False}), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "unsupported benchmark run format"):
                self.module.load_run(path)

    def test_loader_rejects_invalid_operation_batch_size(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            path.write_text(json.dumps({
                **self.base,
                "operationsPerSample": 0,
            }), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "operationsPerSample must be a positive integer"):
                self.module.load_run(path)

    def test_loader_rejects_invalid_workload_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            path.write_text(json.dumps({
                **self.base,
                "workloadParameters": [],
            }), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "workloadParameters must be a non-empty object"):
                self.module.load_run(path)

    def test_sample_count_mismatch_is_metric_specific(self):
        candidate = {**self.base, "measurements": {"frame_us": [80, 90], "pixels": [10000, 10000]}}
        result = self.module.compare_runs(self.base, candidate)
        frame = next(item for item in result["metrics"] if item["name"] == "frame_us")
        self.assertEqual(frame["status"], "not-comparable")
        self.assertEqual(frame["reason"], "sample-count-mismatch")
        self.assertFalse(any(item["unit"] == "MPix/s" for item in result["metrics"]))
        self.assertEqual(result["status"], "not-comparable")

    def test_cli_generates_json_and_html(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.json"
            candidate = root / "candidate.json"
            output = root / "comparison.json"
            html_output = root / "comparison.html"
            baseline.write_text(json.dumps(self.base), encoding="utf-8")
            candidate.write_text(json.dumps({**self.base, "library": "sdl", "version": "2.30"}), encoding="utf-8")
            result = subprocess.run([
                sys.executable, str(TOOL), "--baseline", str(baseline),
                "--candidate", str(candidate), "--output", str(output),
                "--html-output", str(html_output),
            ], cwd=ROOT, check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["status"], "comparable")
            rendered = html_output.read_text(encoding="utf-8")
            self.assertIn("Benchmark comparison: comparable", rendered)
            self.assertIn("Baseline fixed conditions", rendered)
            self.assertIn("sourceRgba", rendered)

    def repeats(self):
        return [copy.deepcopy(self.base) for _ in range(3)]

    def test_repeats_report_median_and_range_without_pooling(self):
        baseline, candidate = self.repeats(), self.repeats()
        for run, values in zip(baseline, ([1, 2, 3], [2, 3, 4], [90, 95, 100])):
            run["measurements"]["frame_us"] = values
        for run in candidate:
            run["measurements"]["frame_us"] = [1, 2, 2]
        result = self.module.compare_repeated_runs(baseline, candidate)
        self.assertEqual(result["status"], "comparable")
        metric = next(m for m in result["metrics"] if m["name"] == "frame_us")
        self.assertEqual(metric["baseline"]["repeatP95"], [3, 4, 100])
        self.assertEqual(metric["baseline"]["medianP95"], 4)
        self.assertEqual(metric["baseline"]["maxP95"], 100)
        self.assertEqual(metric["deltaPercent"], -50)
        self.assertEqual(len(result["pairs"]), 3)
        self.assertNotIn("speedup", result)

    def test_repeat_condition_drift_on_both_sides_is_rejected(self):
        baseline, candidate = self.repeats(), self.repeats()
        baseline[1]["viewport"]["width"] = candidate[1]["viewport"]["width"] = 240
        result = self.module.compare_repeated_runs(baseline, candidate)
        self.assertEqual(result["status"], "not-comparable")
        self.assertIn("baseline[2].viewport", result["statusReasons"])
        self.assertTrue(all("deltaPercent" not in m for m in result["metrics"]))

    def test_repeat_environment_version_and_validation_drift_are_rejected(self):
        for field in ("environment", "version", "outputValidation"):
            with self.subTest(field=field):
                candidate = self.repeats()
                if field == "environment":
                    candidate[2][field]["cpu"] = "different-cpu"
                elif field == "version":
                    candidate[2][field] = "different-version"
                else:
                    candidate[2][field]["status"] = "fail"
                self.assertEqual(self.module.compare_repeated_runs(self.repeats(), candidate)["status"], "not-comparable")

    def test_different_libraries_between_sides_are_allowed(self):
        candidate = self.repeats()
        for run in candidate:
            run.update(library="sdl", version="2.28.2")
        self.assertEqual(self.module.compare_repeated_runs(self.repeats(), candidate)["status"], "comparable")

    def test_repeat_count_guards(self):
        for baseline, candidate in ((self.repeats()[:2], self.repeats()[:2]),
                                    (self.repeats(), self.repeats()[:2])):
            result = self.module.compare_repeated_runs(baseline, candidate)
            self.assertEqual(result["status"], "not-comparable")
        for baseline in ([], self.repeats() * 11):
            with self.assertRaises(ValueError):
                self.module.compare_repeated_runs(baseline, self.repeats())

    def test_repeat_missing_metric_and_sample_drift_do_not_get_pooled(self):
        for missing in (True, False):
            candidate = self.repeats()
            if missing:
                del candidate[1]["measurements"]["frame_us"]
            else:
                candidate[1]["measurements"]["frame_us"].append(100)
            result = self.module.compare_repeated_runs(self.repeats(), candidate)
            self.assertEqual(result["status"], "not-comparable")
            frame = next(m for m in result["metrics"] if m["name"] == "frame_us")
            self.assertNotIn("deltaPercent", frame)
            pixels = next(m for m in result["metrics"] if m["name"] == "pixels")
            self.assertEqual(pixels["status"], "comparable")

    def test_repeat_delta_preserves_submicrosecond_precision_and_zero(self):
        baseline, candidate = self.repeats(), self.repeats()
        for old, new in zip(baseline, candidate):
            old["measurements"]["frame_us"] = [0.004] * 3
            new["measurements"]["frame_us"] = [0.002] * 3
        result = self.module.compare_repeated_runs(baseline, candidate)
        metric = next(m for m in result["metrics"] if m["name"] == "frame_us")
        self.assertEqual(metric["deltaPercent"], -50)
        for old in baseline:
            old["measurements"]["frame_us"] = [0] * 3
        result = self.module.compare_repeated_runs(baseline, candidate)
        metric = next(m for m in result["metrics"] if m["name"] == "frame_us")
        self.assertIsNone(metric["deltaPercent"])
        self.assertEqual(metric["pairedDeltaPercent"], [None] * 3)

    def test_repeat_html_escapes_identity_and_shows_range(self):
        baseline = self.repeats()
        for run in baseline:
            run["library"] = "<script>alert(1)</script>"
        rendered = self.module.render_html(self.module.compare_repeated_runs(baseline, self.repeats()))
        self.assertNotIn("<script>", rendered)
        self.assertIn("median p95 [min, max]", rendered)
        self.assertIn("Repeat evidence", rendered)

    def test_repeat_cli_and_path_guards(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [root / f"run-{i}.json" for i in range(6)]
            for path in paths:
                path.write_text(json.dumps(self.base), encoding="utf-8")
            output, html_output = root / "out.json", root / "out.html"
            command = [sys.executable, str(TOOL), "--baseline", *map(str, paths[:3]),
                       "--candidate", *map(str, paths[3:]), "--output", str(output),
                       "--html-output", str(html_output)]
            process = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            result = json.loads(output.read_text())
            self.assertEqual(result["status"], "comparable")
            self.assertEqual(len(result["inputRuns"]["baseline"]), 3)
            self.assertEqual(len(result["inputRuns"]["baseline"][0]["sha256"]), 64)
            self.assertIn("Repeated benchmark", html_output.read_text())
            for change in ("duplicate", "overwrite", "same-output"):
                altered = command.copy()
                if change == "duplicate":
                    altered[4] = altered[3]
                elif change == "overwrite":
                    altered[altered.index("--output") + 1] = str(paths[0])
                else:
                    altered[altered.index("--html-output") + 1] = str(output)
                process = subprocess.run(altered, capture_output=True, text=True)
                self.assertNotEqual(process.returncode, 0, change)
            self.assertEqual(json.loads(paths[0].read_text()), self.base)


if __name__ == "__main__":
    unittest.main()
