import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/debug"))
from author_startup_probe import CLI_BOOTSTRAP, measure


class StartupProbeTests(unittest.TestCase):
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
