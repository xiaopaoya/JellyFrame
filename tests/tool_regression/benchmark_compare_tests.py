#!/usr/bin/env python3
"""Regression tests for fixed-condition benchmark comparisons."""

import importlib.util
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

    def test_loader_rejects_invalid_operation_batch_size(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            path.write_text(json.dumps({
                **self.base,
                "operationsPerSample": 0,
            }), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "operationsPerSample must be a positive integer"):
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
            self.assertIn("Benchmark comparison: comparable", html_output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
