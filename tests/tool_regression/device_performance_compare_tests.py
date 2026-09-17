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

    def write_side(self, root: Path, name: str, frame_values: list[int], *, visual: str = "exact-readback", conditions: dict | None = None, failures: int = 0, stability: str = "pass", acceptance_mode: str = "target-improvement") -> Path:
        side = root / name
        side.mkdir()
        reports = []
        for index, frame in enumerate(frame_values, 1):
            report_path = side / f"repeat-{index:02d}.json"
            report_path.write_text(json.dumps({
                "format": "jellyframe.render.performance.report",
                "deviceTelemetry": [{
                    "format": self.module.PROFILE_FORMAT,
                    "metrics": {
                        "windowFrames": 120,
                        "warmupFrames": 30,
                        "frames": 120,
                        "presentFrames": 120,
                        "partial": 0,
                        "contaminated": 0,
                        "frameP95Us": frame,
                        "presentP95Us": 100,
                        "presentFailures": failures,
                        "internalFreeMinBytes": 10000,
                    },
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


if __name__ == "__main__":
    unittest.main()
