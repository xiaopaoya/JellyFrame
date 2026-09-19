"""Independent area oracle, non-diluted edge metrics and fail-closed input tests."""

import copy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rounded_aa_quality.py"
spec = importlib.util.spec_from_file_location("rounded_aa_quality", TOOL)
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


class QualityTests(unittest.TestCase):
    def test_oracle_encloses_quarter_circle_area(self):
        fixture = {"rect": [0, 0, 2, 2], "radius": 1}
        intervals = [quality.area_bounds(fixture, x, y) for y in range(2) for x in range(2)]
        self.assertEqual(len(set(intervals)), 1)
        lo, hi = intervals[0]
        self.assertLessEqual(lo, math.pi / 4)
        self.assertGreaterEqual(hi, math.pi / 4)
        self.assertLess(hi - lo, 0.04)

    def test_oracle_refines_interval_without_losing_area(self):
        fixture = {"rect": [0, 0, 2, 2], "radius": 1}
        low3, high3 = quality.area_bounds(fixture, 0, 0, 3)
        low6, high6 = quality.area_bounds(fixture, 0, 0, 6)
        self.assertLessEqual(low3, low6)
        self.assertGreaterEqual(high3, high6)
        with self.assertRaises(ValueError):
            quality.area_bounds(fixture, 0, 0, 9)

    def test_rectangle_and_negative_coordinate_translation(self):
        rect = {"rect": [-2, -1, 4, 4], "radius": 0}
        self.assertEqual(quality.area_bounds(rect, -2, -1), (1, 1))
        self.assertEqual(quality.area_bounds(rect, 2, 0), (0, 0))
        shifted = {"rect": [-10, -10, 2, 2], "radius": 1}
        origin = {"rect": [0, 0, 2, 2], "radius": 1}
        self.assertEqual(quality.area_bounds(shifted, -10, -10), quality.area_bounds(origin, 0, 0))

    def test_oracle_encloses_closed_form_total_area(self):
        fixture = {"rect": [2, 3, 15, 12], "radius": 3}
        bounds = [quality.area_bounds(fixture, x, y) for y in range(18) for x in range(20)]
        area = 15 * 12 - (4 - math.pi) * 3 * 3
        self.assertLessEqual(sum(lo for lo, _ in bounds), area)
        self.assertGreaterEqual(sum(hi for _, hi in bounds), area)

    def test_exact_interior_exterior_gate(self):
        bounds = [(0, 0), (1, 1)]
        self.assertEqual(quality.evaluate(bytes([0, 255]), bounds)["status"], "pass")
        for mask in (bytes([0, 0]), bytes([1, 255]), bytes([0, 254])):
            self.assertEqual(quality.evaluate(mask, bounds)["status"], "fail")
        with self.assertRaises(ValueError):
            quality.evaluate(b"", bounds)

    def test_edge_error_not_diluted_by_empty_background(self):
        small = quality.evaluate(bytes([0]), [(0.49, 0.51)])
        large = quality.evaluate(bytes(10001), [(0.49, 0.51)] + [(0, 0)] * 10000)
        self.assertEqual(small["status"], "fail")
        self.assertEqual(small["edgeRmsError"], large["edgeRmsError"])

    def test_uncertainty_is_not_a_pass(self):
        result = quality.evaluate(bytes([128]), [(0.375, 0.625)])
        self.assertEqual(result["status"], "indeterminate")
        self.assertEqual(result["reasons"], ["oracle-interval-crosses-limit"])

    def test_analytic_area_mask_passes_binary_control_fails(self):
        fixture = {"rect": [0, 0, 2, 2], "radius": 1}
        bounds = [quality.area_bounds(fixture, x, y) for y in range(2) for x in range(2)]
        self.assertEqual(quality.evaluate(bytes([round(math.pi / 4 * 255)] * 4), bounds)["status"], "pass")
        self.assertEqual(quality.evaluate(bytes([255] * 4), bounds)["status"], "fail")
        # Existing quarter-origin sampling is assessed, not used as the ideal.
        self.assertEqual(quality.evaluate(bytes([128, 191, 191, 239]), bounds)["status"], "fail")

    def test_quality_report_cannot_enter_performance_ranking(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "quality.json"
            path.write_text(json.dumps({"format": quality.FORMAT, "performanceMeasured": False}))
            comparator = ROOT / "tools" / "benchmark_compare.py"
            spec = importlib.util.spec_from_file_location("benchmark_compare", comparator)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with self.assertRaisesRegex(SystemExit, "unsupported benchmark run format"):
                module.load_run(path)
            path.write_text(json.dumps({"format": quality.JOINT_FORMAT, "performanceMeasured": True}))
            with self.assertRaisesRegex(SystemExit, "unsupported benchmark run format"):
                module.load_run(path)


class InputFixtureMixin:
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "coverage.json"
        cases = []
        for fixture in quality.FIXTURES:
            name = fixture["name"] + ".pgm"
            (self.root / name).write_bytes(quality.PGM_HEADER + bytes(quality.WIDTH * quality.HEIGHT))
            cases.append({"name": fixture["name"], "mask": name})
        self.manifest = {"format": quality.INPUT_FORMAT, "fixtureSet": "rounded-uniform-v0",
                         "viewport": {"width": quality.WIDTH, "height": quality.HEIGHT},
                         "performanceMeasured": False, "fixtures": copy.deepcopy(quality.FIXTURES),
                         "backends": [{"id": "test", "method": "synthetic", "cases": cases}]}

    def write(self):
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")


class InputTests(InputFixtureMixin, unittest.TestCase):
    def test_load_valid_hashes_and_assets(self):
        self.write()
        loaded, paths = quality.load_masks(self.path)
        self.assertEqual(len(paths), 9)
        self.assertEqual(len(loaded["sha256"]), 64)
        self.assertEqual(len(loaded["backends"][0]["masks"]), 8)

    def test_changed_geometry_rejected(self):
        for value in (True, 1.0, 9):
            self.manifest["fixtures"][2]["radius"] = value
            self.write()
            with self.assertRaisesRegex(ValueError, "geometry changed"):
                quality.load_masks(self.path)

    def test_duplicate_backend_and_missing_case_rejected(self):
        self.manifest["backends"].append(copy.deepcopy(self.manifest["backends"][0]))
        self.write()
        with self.assertRaisesRegex(ValueError, "duplicate"):
            quality.load_masks(self.path)
        self.manifest["backends"].pop()
        self.manifest["backends"][0]["cases"].pop()
        self.write()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            quality.load_masks(self.path)

    def test_path_escape_and_asset_reuse_rejected(self):
        for bad in ("../outside.pgm", "/outside.pgm", "C:/outside.pgm", "rectangle.pgm"):
            self.manifest["backends"][0]["cases"][0]["mask"] = bad
            self.write()
            with self.assertRaises((ValueError, OSError)):
                quality.load_masks(self.path)

    def test_malformed_and_oversized_inputs_rejected(self):
        self.write()
        mask = self.root / "card.pgm"
        for raw in (b"P5\n1 1\n255\n\0", quality.PGM_HEADER + bytes(quality.WIDTH * quality.HEIGHT + 1)):
            mask.write_bytes(raw)
            with self.assertRaises(ValueError):
                quality.load_masks(self.path)
        self.path.write_bytes(b" " * (64 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, "size limit"):
            quality.load_masks(self.path)

    def test_cli_does_not_overwrite_inputs(self):
        self.write()
        original = self.path.read_bytes()
        result = subprocess.run([sys.executable, str(TOOL), "--input", str(self.path), "--output", str(self.path)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot overwrite inputs", result.stderr)
        self.assertEqual(self.path.read_bytes(), original)

    def test_cli_records_failed_quality_without_timing(self):
        self.write()
        report = self.root / "quality.json"
        markdown = self.root / "quality.md"
        subprocess.run([sys.executable, str(TOOL), "--input", str(self.path), "--output", str(report),
                        "--markdown-output", str(markdown)], check=True, capture_output=True, text=True)
        result = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(result["format"], quality.FORMAT)
        self.assertEqual(result["backends"][0]["status"], "fail")
        self.assertFalse(result["performanceComparable"])
        self.assertNotIn("measurements", result)
        self.assertIn("no timing or speed ranking", markdown.read_text(encoding="utf-8"))


class CostTests(InputFixtureMixin, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.write()
        self.loaded, _ = quality.load_masks(self.path)
        self.cost_path = self.root / "cost.json"
        self.cost = {"format": "jellyframe.rounded.cost.v0", "fixtureSet": "rounded-uniform-v0",
                     "fixtures": copy.deepcopy(quality.FIXTURES), "viewport": {"width": 172, "height": 320},
                     "sampleCount": 3, "warmupIterations": 30, "buildType": "release", "coreVersion": "test",
                     "timingContract": copy.deepcopy(quality.TIMING_CONTRACT), "backends": [{"id": "test",
                         "method": "synthetic", "cases": [
                             {"name": fixture["name"], "maskSha256": digest, "paint_us": [3, 1, 2]}
                             for fixture, (_, digest) in zip(quality.FIXTURES, self.loaded["backends"][0]["masks"])]}]}
        self.report = {"format": quality.FORMAT, "performanceMeasured": False, "performanceComparable": False,
                       "backends": [{"id": "test", "cases": [{"name": fixture["name"], "status": "fail"}
                                    for fixture in quality.FIXTURES]}]}

    def join(self):
        self.cost_path.write_text(json.dumps(self.cost), encoding="utf-8")
        quality.attach_cost(self.report, self.loaded, self.cost_path)

    def test_cost_keeps_failed_quality_and_disables_ranking(self):
        self.join()
        case = self.report["backends"][0]["cases"][0]
        self.assertEqual(case["status"], "fail")
        self.assertEqual(case["cost"]["p50Us"], 2)
        self.assertEqual(case["cost"]["p95Us"], 3)
        self.assertEqual(case["cost"]["paintUs"], [3, 1, 2])
        self.assertEqual(case["cost"]["use"], "diagnostic-only")
        self.assertEqual(self.report["format"], quality.JOINT_FORMAT)
        self.assertTrue(self.report["performanceMeasured"])
        self.assertFalse(self.report["performanceComparable"])

    def test_mismatched_mask_does_not_partially_modify_report(self):
        before = copy.deepcopy(self.report)
        self.cost["backends"][0]["cases"][-1]["maskSha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "mask identity"):
            self.join()
        self.assertEqual(self.report, before)

    def test_changed_timing_contract_or_geometry_rejected(self):
        for name, bad in (("warmupIterations", 31), ("fixtureSet", "different"),
                          ("timingContract", {}), ("fixtures", []), ("sampleCount", True)):
            old = self.cost[name]
            self.cost[name] = bad
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.join()
            self.cost[name] = old

    def test_invalid_or_truncated_samples_rejected(self):
        for bad in ([1, 2], [1, True, 2], [1, -1, 2], [1, float("nan"), 2],
                    [1, float("inf"), 2], [1, 10**400, 2]):
            self.cost["backends"][0]["cases"][0]["paint_us"] = bad
            with self.subTest(samples=bad), self.assertRaisesRegex(ValueError, "invalid cost samples"):
                self.join()

    def test_mismatched_backend_and_missing_case_rejected(self):
        self.cost["backends"][0]["method"] = "different"
        with self.assertRaisesRegex(ValueError, "backend identity"):
            self.join()
        self.cost["backends"][0]["method"] = "synthetic"
        self.cost["backends"][0]["cases"].pop()
        with self.assertRaisesRegex(ValueError, "cases missing"):
            self.join()

    def test_cli_cannot_overwrite_cost_input(self):
        self.cost_path.write_text(json.dumps(self.cost), encoding="utf-8")
        original = self.cost_path.read_bytes()
        result = subprocess.run([sys.executable, str(TOOL), "--input", str(self.path),
                                 "--cost-input", str(self.cost_path), "--output", str(self.cost_path)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot overwrite inputs", result.stderr)
        self.assertEqual(self.cost_path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
