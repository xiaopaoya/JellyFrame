#!/usr/bin/env python3
"""Static and optional desktop checks for the three wearable scenario apps."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APPS = {
    "alarm": (ROOT / "samples/apps/packages/jelly_alarm", ["alarm-1-toggle", "alarm-1-main", "add-alarm"]),
    "workout": (ROOT / "samples/apps/packages/jelly_workout", ["workout-0", "target-range", "start-workout", "next-page"]),
    "messages": (ROOT / "samples/apps/packages/jelly_messages", ["message-1-main", "detail-back"]),
}


def main():
    for name, (root, ids) in APPS.items():
        manifest = json.loads((root / "jellyframe.app.json").read_text(encoding="utf-8"))
        assert manifest["runtime"]["script"] == "classic", name
        html = (root / "index.html").read_text(encoding="utf-8")
        script = (root / "scripts/app.js").read_text(encoding="utf-8")
        capture = (root / "capture_review.jfcapture").read_text(encoding="utf-8")
        assert 'background:#000' in (root / "styles/app.css").read_text(encoding="utf-8"), name
        if name == "alarm":
            assert "alarm.id + \"-toggle\"" in script and "alarm.id + \"-main\"" in script
            assert 'id="add-alarm"' in html
        elif name == "workout":
            assert 'button.id = "workout-"' in script and 'id="start-workout"' in html
        else:
            assert 'copy.id = message.id + "-main"' in script and 'id="detail-back"' in html
        assert "event 2" in capture and "viewport 300 300" in capture, name
    shell = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if shell and shell.is_file():
        output = ROOT / "build/test_outputs/wearable_scenarios"
        output.mkdir(parents=True, exist_ok=True)
        for name, (root, _) in APPS.items():
            result = subprocess.run([str(shell), "--app", str(root), "--frame-script", str(root / "capture_review.jfcapture"), "--capture-frames", str(output / name)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
            assert result.returncode == 0, f"{name}: {result.stdout[-4000:]}"
    print("Wearable scenario package checks passed")


if __name__ == "__main__":
    main()
