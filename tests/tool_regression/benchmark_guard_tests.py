#!/usr/bin/env python3
"""Regression tests for machine-readable benchmark guard output."""

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "project_tools" / "benchmark_guard.py"


def load_module():
    spec = importlib.util.spec_from_file_location("benchmark_guard", TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {TOOL}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class BenchmarkGuardTests(unittest.TestCase):
    def test_parse_results_keeps_average_and_statistical_probes(self):
        module = load_module()
        results = module.parse_results(
            "full_pipeline iterations=20 avg_us=123.5\n"
            "modern_paint_shadow_stats samples=16 iterations_per_sample=12 "
            "p50_us=229.25 p95_us=238.417 display_commands=1 peak_surface_bytes=220160\n"
        )
        self.assertEqual(results["full_pipeline"], {
            "kind": "average",
            "iterations": 20,
            "avg_us": 123.5,
        })
        self.assertEqual(results["modern_paint_shadow_stats"], {
            "kind": "statistics",
            "samples": 16,
            "iterations_per_sample": 12,
            "p50_us": 229.25,
            "p95_us": 238.417,
            "display_commands": 1,
            "peak_surface_bytes": 220160,
        })


if __name__ == "__main__":
    unittest.main()
