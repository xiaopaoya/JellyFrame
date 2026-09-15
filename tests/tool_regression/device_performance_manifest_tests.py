#!/usr/bin/env python3
"""Regression tests for device performance manifest collection."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "device_performance_manifest.py"


def load_module():
    spec = importlib.util.spec_from_file_location("device_performance_manifest", TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DevicePerformanceManifestTests(unittest.TestCase):
    def setUp(self):
        self.module = load_module()

    def write_matrix(self, root: Path, repeats: int = 3, differing_hash: bool = False) -> Path:
        for repeat in range(1, repeats + 1):
            folder = root / "drag-scroll" / f"repeat-{repeat:02d}" / "baseline" / "on"
            folder.mkdir(parents=True)
            (folder / "result.json").write_text(json.dumps({"format": "jellyframe.render.performance.report"}), encoding="utf-8")
            digest = ("B" if differing_hash and repeat == repeats else "A") * 64
            (folder / "firmware.sha256").write_text(digest + "  firmware.bin\n", encoding="ascii")
        conditions = root / "conditions.json"
        conditions.write_text(json.dumps({"board": "ws147", "viewport": "172x320"}), encoding="utf-8")
        return conditions

    def namespace(self, root: Path, conditions: Path, output: Path):
        return type("Args", (), {
            "root": root,
            "workload": "drag-scroll",
            "side": "baseline",
            "output": output,
            "conditions_json": conditions,
            "commit": "abc123",
            "visual_status": "visual-equivalent-only",
            "stability_status": "pass",
            "target_metric": "frameP95Us",
            "acceptance_mode": "target-improvement",
            "minimum_improvement_percent": 5.0,
            "max_frame_regression_percent": 3.0,
            "max_memory_regression_percent": 5.0,
        })

    def test_collects_three_repeats_and_relative_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "matrix"
            root.mkdir()
            conditions = self.write_matrix(root)
            output = root / "manifests" / "baseline.json"
            manifest = self.module.build_manifest(self.namespace(root, conditions, output))
            self.assertEqual(len(manifest["reports"]), 3)
            self.assertTrue(all(path.startswith("../drag-scroll/") for path in manifest["reports"]))
            self.assertEqual(manifest["visualEvidence"]["status"], "visual-equivalent-only")
            self.assertEqual(manifest["identity"]["firmwareSha256"], "A" * 64)

    def test_rejects_short_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "matrix"
            root.mkdir()
            conditions = self.write_matrix(root, repeats=2)
            with self.assertRaisesRegex(SystemExit, "at least 3"):
                self.module.build_manifest(self.namespace(root, conditions, root / "manifest.json"))

    def test_rejects_mixed_firmware_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "matrix"
            root.mkdir()
            conditions = self.write_matrix(root, differing_hash=True)
            with self.assertRaisesRegex(SystemExit, "different firmware hashes"):
                self.module.build_manifest(self.namespace(root, conditions, root / "manifest.json"))


if __name__ == "__main__":
    unittest.main()
