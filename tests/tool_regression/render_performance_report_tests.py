#!/usr/bin/env python3
"""Regression tests for the source-aware render performance report."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "render_performance_report.py"


class RenderPerformanceReportTests(unittest.TestCase):
    def test_trace_aggregation_keeps_sources_and_computes_percentiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace.jsonl"
            trace.write_text(
                json.dumps({"format": "jellyframe.render.trace.v0", "type": "session", "viewport": {"width": 172, "height": 320}}) + "\n"
                + json.dumps({"type": "frame", "frame": 1, "totalUs": 100, "stagesUs": {"layout": 25, "paint": 75}, "dirtyRectCount": 1, "dirtyAreaPercent": 4}) + "\n"
                + json.dumps({"type": "frame", "frame": 2, "totalUs": 200, "stagesUs": {"layout": 50, "paint": 150}, "commands": [{"type": "BoxShadow", "us": 120}]}) + "\n",
                encoding="utf-8",
            )
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--trace", str(trace), "--output", str(output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["format"], "jellyframe.render.performance.report")
            self.assertEqual(report["summary"]["frameCount"], 2)
            self.assertEqual(report["summary"]["totalUs"]["p95"], 200)
            self.assertEqual(report["summary"]["stageSharesPercent"]["paint"], 75)
            self.assertEqual(report["summary"]["commandAttributionUs"]["BoxShadow"], 120)
            self.assertEqual(report["metadata"]["viewport"], {"width": 172, "height": 320})

    def test_html_output_is_generated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "pipeline.json"
            report.write_text(json.dumps({"timingsUs": {"paint": 40, "present": 60, "total": 100}}), encoding="utf-8")
            output = root / "report.json"
            html_output = root / "report.html"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--report", str(report), "--output", str(output), "--html-output", str(html_output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("JellyFrame Render Performance", html_output.read_text(encoding="utf-8"))

    def test_invalid_trace_record_is_reported_without_fabricating_a_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace.jsonl"
            trace.write_text("not-json\n" + json.dumps({"type": "frame", "frame": 1}) + "\n", encoding="utf-8")
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--trace", str(trace), "--output", str(output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("no usable performance input", result.stderr)


if __name__ == "__main__":
    unittest.main()
