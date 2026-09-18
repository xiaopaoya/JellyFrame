#!/usr/bin/env python3
"""Regression tests for repeated device performance comparisons."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "device_performance_compare.py"


def load_module():
    spec = importlib.util.spec_from_file_location("device_performance_compare", TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DevicePerformanceCompareTests(unittest.TestCase):
    def setUp(self):
        self.module = load_module()

    def write_side(
        self,
        root: Path,
        name: str,
        frame_values: list[int],
        *,
        visual: str = "exact-readback",
        conditions: dict | None = None,
        failures: int = 0,
        stability: str = "pass",
        acceptance_mode: str = "target-improvement",
        present_values: list[int] | None = None,
        histogram: tuple[int, int] | None = (1000, 128000),
    ) -> Path:
        side = root / name
        side.mkdir()
        reports = []
        for index, frame in enumerate(frame_values, 1):
            report_path = side / f"repeat-{index:02d}.json"
            metrics = {
                "windowFrames": 120,
                "warmupFrames": 30,
                "frames": 120,
                "presentFrames": 120,
                "partial": 0,
                "contaminated": 0,
                "frameP95Us": frame,
                "inputP50Us": 1,
                "inputP95Us": 3,
                "planningP50Us": 2,
                "planningP95Us": 4,
                "pipelineFrames": 120,
                "pipelineP50Us": frame - 20,
                "pipelineP95Us": frame - 10,
                "paintP95Us": 100,
                "presentP95Us": present_values[index - 1] if present_values else 100,
                "dmaSubmitP50Us": 5,
                "dmaSubmitP95Us": 7,
                "presentFailures": failures,
                "internalFreeMinBytes": 10000,
            }
            if histogram is not None:
                metrics.update({
                    "histogramBucketUs": histogram[0],
                    "histogramCeilingUs": histogram[1],
                })
            report_path.write_text(json.dumps({
                "format": "jellyframe.render.performance.report",
                "deviceTelemetry": [{
                    "format": self.module.PROFILE_FORMAT,
                    "metrics": metrics,
                }],
            }), encoding="utf-8")
            reports.append(report_path.name)
        manifest = side / "manifest.json"
        manifest.write_text(json.dumps({
            "format": self.module.FORMAT,
            "workload": "drag-scroll",
            "identity": {"commit": name, "firmwareSha256": name * 4},
            "conditions": conditions or {
                "board": "ws147",
                "viewport": "172x320",
                "pixelFormat": "RGB565",
                "warmupFrames": 30,
                "measuredFrames": 120,
            },
            "visualEvidence": {"status": visual},
            "stability": {"status": stability},
            "acceptance": {"mode": acceptance_mode, "targetMetric": "frameP95Us"},
            "reports": reports,
        }), encoding="utf-8")
        return manifest

    def test_summarizes_repeat_percentiles_by_median(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1200, 1100])
            candidate = self.write_side(root, "candidate", [900, 1000, 950])
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "PASS")
            frame = next(row for row in result["metrics"] if row["name"] == "frameP95Us")
            self.assertEqual(frame["baseline"]["median"], 1100)
            self.assertEqual(frame["candidate"]["median"], 950)
            self.assertEqual(frame["deltaPercent"], -13.64)
            pipeline = next(row for row in result["metrics"] if row["name"] == "pipelineP95Us")
            self.assertEqual(pipeline["baseline"]["median"], 1090)
            self.assertEqual(pipeline["candidate"]["median"], 940)
            self.assertEqual(pipeline["deltaPercent"], -13.76)
            self.assertTrue(any(row["name"] == "planningP95Us" for row in result["metrics"]))
            self.assertTrue(any(row["name"] == "dmaSubmitP95Us" for row in result["metrics"]))

    def test_visual_equivalent_only_is_accepted_without_readback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000], visual="visual-equivalent-only")
            candidate = self.write_side(root, "candidate", [900, 900, 900], visual="visual-equivalent-only")
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "PASS")
            self.assertFalse(any("visual" in reason for reason in result["statusReasons"]))

    def test_missing_visual_evidence_remains_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000], visual="missing")
            candidate = self.write_side(root, "candidate", [900, 900, 900], visual="visual-equivalent-only")
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "PARTIAL")
            self.assertTrue(any("visual evidence is missing" in reason for reason in result["statusReasons"]))

    def test_condition_mismatch_is_invalid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000])
            candidate = self.write_side(root, "candidate", [900, 900, 900], conditions={"board": "other"})
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "INVALID")
            self.assertEqual(result["fixedConditionMismatches"], ["conditions"])

    def test_present_failure_is_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000])
            candidate = self.write_side(root, "candidate", [900, 900, 900], failures=1)
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "FAIL")
            self.assertTrue(any("presentFailures" in reason for reason in result["statusReasons"]))

    def test_requires_three_repeats(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.write_side(root, "baseline", [1000, 1000])
            with self.assertRaisesRegex(SystemExit, "at least 3 repeats"):
                self.module.load_side(manifest)

    def test_stability_failure_is_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000])
            candidate = self.write_side(root, "candidate", [900, 900, 900], stability="fail")
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "FAIL")
            self.assertTrue(any("stability" in reason for reason in result["statusReasons"]))

    def test_non_regression_mode_accepts_unchanged_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000], acceptance_mode="non-regression")
            candidate = self.write_side(root, "candidate", [1000, 1000, 1000])
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "PASS")

    def test_saturated_target_is_inconclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(
                root, "baseline", [127000, 127000, 127000], acceptance_mode="non-regression"
            )
            candidate = self.write_side(root, "candidate", [127000, 127000, 127000])
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            frame = next(row for row in result["metrics"] if row["name"] == "frameP95Us")
            self.assertEqual(result["status"], "PARTIAL")
            self.assertEqual(frame["reason"], "histogram-saturated")
            self.assertNotIn("deltaPercent", frame)
            self.assertTrue(any(check.get("inconclusive") for check in result["acceptance"]["checks"]))

    def test_present_stage_regression_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(
                root, "baseline", [1000, 1000, 1000],
                acceptance_mode="non-regression", present_values=[100, 100, 100],
            )
            candidate = self.write_side(
                root, "candidate", [1000, 1000, 1000], present_values=[104, 104, 104]
            )
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            check = next(
                check for check in result["acceptance"]["checks"]
                if check["name"] == "stageRegression" and check["metric"] == "presentP95Us"
            )
            self.assertEqual(result["status"], "FAIL")
            self.assertEqual(check["actualDeltaPercent"], 4)
            self.assertFalse(check["pass"])

    def test_zero_stage_values_are_non_regressing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(
                root, "baseline", [1000, 1000, 1000],
                acceptance_mode="non-regression", present_values=[0, 0, 0],
            )
            candidate = self.write_side(
                root, "candidate", [1000, 1000, 1000], present_values=[0, 0, 0]
            )
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            check = next(
                check for check in result["acceptance"]["checks"]
                if check["name"] == "stageRegression" and check["metric"] == "presentP95Us"
            )
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(check["actualDeltaPercent"], 0)
            self.assertTrue(check["pass"])

    def test_histogram_mismatch_is_invalid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_side(root, "baseline", [1000, 1000, 1000])
            candidate = self.write_side(
                root, "candidate", [900, 900, 900], histogram=(500, 128000)
            )
            result = self.module.compare_sides(self.module.load_side(baseline), self.module.load_side(candidate))
            self.assertEqual(result["status"], "INVALID")
            self.assertIn("histogramConfig", result["fixedConditionMismatches"])


if __name__ == "__main__":
    unittest.main()
