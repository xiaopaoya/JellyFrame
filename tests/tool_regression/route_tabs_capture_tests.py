#!/usr/bin/env python3
"""Verify route state through the package's ordinary coordinate capture."""

import subprocess
import sys
from pathlib import Path

from package_image_fixture_tests import pixel_at, read_bmp_pixels


ASSERTIONS = """(function () {
var marker = document.createElement("div");
marker.style.position = "absolute";
marker.style.left = "0px";
marker.style.top = "0px";
marker.style.width = "2px";
marker.style.height = "2px";
marker.style.background = "#ff0000";
document.body.appendChild(marker);

function equal(actual, expected, field) {
  if (actual !== expected) {
    throw new Error("ROUTE_ASSERT " + field + " expected=" + expected + " actual=" + actual);
  }
}
var states = [
  ["today", "Today", "UP NEXT", "09:30", "Morning reading"],
  ["focus", "Focus", "SESSION LENGTH", "25 min", "One thing at a time"],
  ["settings", "Settings", "NOTIFICATIONS", "Quiet", "Your personal space"],
  ["today", "Today", "UP NEXT", "09:30", "Morning reading"]
];
function check(state) {
  var ids = ["title", "detail", "value", "summary"];
  for (var i = 0; i < ids.length; i += 1) {
    equal(document.getElementById(ids[i]).textContent, state[i + 1], ids[i]);
  }
  var tabs = document.querySelectorAll("[data-route]");
  equal(tabs.length, 3, "tab count");
  for (var j = 0; j < tabs.length; j += 1) {
    equal(tabs[j].classList.contains("active"), tabs[j].dataset.route === state[0], tabs[j].id);
  }
}
check(states[0]);
var next = 1;
window.addEventListener("hashchange", function () {
  if (next >= states.length) {
    throw new Error("ROUTE_ASSERT unexpected extra route change");
  }
  equal(location.hash, "#" + states[next][0], "location.hash");
  check(states[next]);
  next += 1;
  if (next === states.length) {
    marker.style.background = "#00ff00";
  }
});
}());
"""


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: route_tabs_capture_tests.py SHELL_EXE APP_DIR OUTPUT_DIR", file=sys.stderr)
        return 2
    shell, app, output_dir = (Path(value).resolve() for value in sys.argv[1:])
    output_dir.mkdir(parents=True, exist_ok=True)
    assertions = output_dir / "route_assertions.js"
    assertions.write_text(ASSERTIONS, encoding="utf-8")
    final_frame = output_dir / "frames/frame_008.bmp"
    final_frame.unlink(missing_ok=True)
    command = [
        str(shell), "--app", str(app), "--frame-script", str(app / "capture_route_tabs.jfcapture"),
        "--capture-frames", str(output_dir / "frames"),
        "--capture-montage", str(output_dir / "montage.bmp"),
        "--script", str(assertions),
    ]
    result = subprocess.run(command, cwd=output_dir, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=120)
    output = result.stdout + result.stderr
    (output_dir / "capture.log").write_text(output, encoding="utf-8")
    if result.returncode or "ROUTE_ASSERT" in output or "script failed" in output or "[error]" in output:
        raise AssertionError(f"route capture failed\n{output}")
    width, height, pixels = read_bmp_pixels(final_frame)
    if (width, height) != (300, 300) or pixel_at(pixels, width, 0, 0) != (0, 255, 0):
        raise AssertionError("route assertions did not complete all three transitions")
    print("route tabs passed initial state and focus/settings/today content, hash and active-tab checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
