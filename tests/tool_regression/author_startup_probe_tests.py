import sys
import tempfile
import subprocess
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/debug"))
from author_startup_probe import CLI_BOOTSTRAP, measure, read_app_manifest


class StartupProbeTests(unittest.TestCase):
    def test_app_root_and_manifest_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "specific App folder"):
                read_app_manifest(root)
            app = root / "weather"
            app.mkdir()
            manifest = app / "jellyframe.app.json"
            manifest.write_text('{"runtime":{"script":"classic"}}', encoding="utf-8")
            self.assertEqual(read_app_manifest(app), read_app_manifest(manifest))
            self.assertEqual(read_app_manifest(app)[0], app.resolve())
            manifest.write_text('[]', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "JSON objects"):
                read_app_manifest(app)
            manifest.write_text('{invalid', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Cannot read App manifest"):
                read_app_manifest(app)

    def test_invalid_app_has_actionable_error_and_no_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "report"
            script = Path(__file__).resolve().parents[2] / "tools/debug/author_startup_probe.py"
            result = subprocess.run([sys.executable, str(script), "--sdk", str(root),
                                     "--app", str(root), "--output", str(output)],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertIn("specific App folder", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse(output.exists())

    def test_first_frame_stops_process(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = measure([sys.executable, "-c",
                              "print('JF_FRAME\\t1\\t1\\t1\\tframe.bmp',flush=True); assert input()=='quit'"],
                             root, root / "frame.log", first_frame=True)
            self.assertEqual(result["exitCode"], 0)
            self.assertIsNotNone(result["firstFrameMs"])
            self.assertFalse(result["timedOut"])

    def test_timeout_and_failure_are_not_successes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = measure([sys.executable, "-c", "import time; time.sleep(30)"],
                             root, root / "timeout.log", timeout=0.1)
            self.assertTrue(result["timedOut"])
            result = measure([sys.executable, "-c", "raise SystemExit(7)"], root, root / "fail.log")
            self.assertEqual(result["exitCode"], 7)

    def test_bootstrap_records_nested_process(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "cli.py"
            script.write_text("import subprocess, sys\nsubprocess.call([sys.executable, '-c', 'print(42)'])\n")
            result = measure([sys.executable, "-c", CLI_BOOTSTRAP, str(script)], root, root / "steps.log")
            self.assertEqual(result["exitCode"], 0)
            self.assertEqual(len(result["steps"]), 1)
            self.assertEqual(result["steps"][0]["exitCode"], 0)


if __name__ == "__main__":
    unittest.main()
