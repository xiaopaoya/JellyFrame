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
                + json.dumps({"type": "frame", "frame": 2, "totalUs": 200, "stagesUs": {"layout": 50, "paint": 150}, "commands": [{"type": "BoxShadow", "owner": "id:card-1", "us": 120, "pixels": 320, "samples": 2}, {"type": "BoxShadow", "owner": "not a safe owner", "us": 30, "pixels": 20, "samples": 1}], "commandsTruncated": True}) + "\n",
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
            self.assertEqual(report["summary"]["commandAttributionUs"]["BoxShadow"], 150)
            self.assertEqual(
                report["summary"]["commandOwnerAttribution"][0],
                {"owner": "id:card-1", "type": "BoxShadow", "us": 120, "pixels": 320, "samples": 2},
            )
            self.assertEqual(
                report["summary"]["commandOwnerAttribution"][1]["owner"],
                "unattributed",
            )
            self.assertTrue(report["summary"]["commandOwnerAttributionTruncated"])
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
            rendered = html_output.read_text(encoding="utf-8")
            self.assertIn("JellyFrame Render Performance", rendered)
            self.assertIn("Command / owner attribution", rendered)

    def test_device_profile_telemetry_is_kept_separate_from_desktop_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            telemetry = root / "device-profile.log"
            telemetry.write_text(
                "device_profile format=jellyframe.device.profile.v0 case=drag "
                "window=1 frame_us_p50=28600 frame_us_p95=41700 frames=120 pipeline_frames=8 partial=0\n"
                "device_profile_timing window=1 input_us_p95=310\n"
                "device_profile_pipeline window=1 paint_us_p95=16800 present_us_p95=22800\n"
                "device_profile_present window=1 dma_wait_us_p95=17100\n"
                "device_profile_counters window=1 dirty_pixels_avg=18944 packed_bytes=4546560 "
                "internal_free_min=80399\n",
                encoding="utf-8",
            )
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--device-telemetry", str(telemetry), "--output", str(output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(report["summary"]["frameCount"], 0)
        self.assertEqual(report["deviceTelemetry"][0]["metrics"]["frameP95Us"], 41700)
        self.assertEqual(report["deviceTelemetry"][0]["metrics"]["paintP95Us"], 16800)
        self.assertEqual(report["deviceTelemetry"][0]["metrics"]["dmaWaitP95Us"], 17100)
        self.assertEqual(report["deviceTelemetry"][0]["metrics"]["pipelineFrames"], 8)
        self.assertEqual(report["deviceTelemetry"][0]["metrics"]["packedBytes"], 4546560)

    def test_device_profile_telemetry_rejects_incomplete_window(self):
        with tempfile.TemporaryDirectory() as directory:
            telemetry = Path(directory) / "device-profile.log"
            telemetry.write_text(
                "device_profile window=1 frames=120\n"
                "device_profile_timing window=1 frame_us_p95=41700\n",
                encoding="utf-8",
            )
            output = Path(directory) / "report.json"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--device-telemetry", str(telemetry), "--output", str(output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("incomplete Device Performance Profile V0 window 1", result.stderr)

    def test_device_profile_telemetry_rejects_multiple_windows(self):
        profile = (
            "device_profile window=1 frames=120\n"
            "device_profile_timing window=1 frame_us_p95=41700\n"
            "device_profile_pipeline window=1 paint_us_p95=16800\n"
            "device_profile_present window=1 dma_wait_us_p95=17100\n"
            "device_profile_counters window=1 packed_bytes=4546560\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            telemetry = Path(directory) / "device-profile.log"
            telemetry.write_text(profile + profile.replace("window=1", "window=2"), encoding="utf-8")
            output = Path(directory) / "report.json"
            result = subprocess.run(
                [sys.executable, str(TOOL), "--device-telemetry", str(telemetry), "--output", str(output)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("accepts exactly one complete window; found windows 1, 2", result.stderr)

    def test_legacy_key_value_device_telemetry_keeps_numeric_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            telemetry = Path(directory) / "legacy-port.log"
            telemetry.write_text("port_telemetry frames=60 frame_ms_p95=41.7 dma_wait_ms_avg=5.2\n", encoding="utf-8")
            loaded = __import__("importlib").util.spec_from_file_location("render_performance_report", TOOL)
            assert loaded is not None and loaded.loader is not None
            module = __import__("importlib").util.module_from_spec(loaded)
            loaded.loader.exec_module(module)
            report = module.load_device_telemetry(telemetry)

        self.assertEqual(report["metrics"]["frames"], 60)
        self.assertEqual(report["metrics"]["p95FrameMs"], 41.7)
        self.assertEqual(report["metrics"]["averageDmaWaitMs"], 5.2)

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

    def test_trace_rejects_non_monotonic_frame_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "trace.jsonl"
            trace.write_text(
                json.dumps({"format": "jellyframe.render.trace.v0", "type": "session"}) + "\n"
                + json.dumps({"format": "jellyframe.render.trace.v0", "type": "frame", "frame": 2, "totalUs": 10}) + "\n"
                + json.dumps({"format": "jellyframe.render.trace.v0", "type": "frame", "frame": 2, "totalUs": 20}) + "\n"
                + json.dumps({"format": "jellyframe.render.trace.v0", "type": "frame", "frame": 1, "totalUs": 30}) + "\n",
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
            self.assertEqual(report["summary"]["frameCount"], 1)
            self.assertTrue(any("not strictly greater" in note for note in report["warnings"]))


if __name__ == "__main__":
    unittest.main()
