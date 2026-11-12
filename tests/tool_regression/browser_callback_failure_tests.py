import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-callback-") as directory:
        root = Path(directory)
        page = root / "index.html"
        style = root / "style.css"
        script = root / "script.js"
        frames = root / "frames"
        page.write_text('<button id="go">GO</button>', encoding="utf-8")
        style.write_text("button { width: 100px; height: 60px; }", encoding="utf-8")
        script.write_text(
            'var button = document.getElementById("go");\n'
            'var depth = 0;\n'
            'button.addEventListener("click", function () {\n'
            '  depth += 1;\n'
            '  if (depth < 40) button.click();\n'
            '  depth -= 1;\n'
            '});\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            [str(executable), str(page), str(style), "240", "240",
             "--script", str(script), "--capture-frames", str(frames),
             "--frame-count", "3", "--frame-event", "1:click-id:go"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout)
        for expected in (
            "script_callback_failure boundary=pointer_up reason=budget-exceeded",
            "script stopped reason=budget-exceeded",
        ):
            if expected not in result.stdout:
                raise AssertionError(f"Missing {expected}\n{result.stdout}")
        if len(list(frames.glob("frame_*.bmp"))) != 3:
            raise AssertionError(f"Incomplete recovery capture\n{result.stdout}")
        page.write_text('<input id="focus" autofocus>', encoding="utf-8")
        script.write_text(
            'document.getElementById("focus").addEventListener("focus", function () {\n'
            '  throw new Error("autofocus failure");\n'
            '});\n',
            encoding="utf-8",
        )
        focus_frames = root / "focus-frames"
        result = subprocess.run(
            [str(executable), str(page), str(style), "240", "240",
             "--script", str(script), "--capture-frames", str(focus_frames),
             "--frame-count", "3"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout)
        for expected in (
            "script_callback_failure boundary=autofocus reason=runtime-error",
            "script stopped reason=runtime-error",
        ):
            if expected not in result.stdout:
                raise AssertionError(f"Missing {expected}\n{result.stdout}")
        if len(list(focus_frames.glob("frame_*.bmp"))) != 3:
            raise AssertionError(f"Incomplete autofocus recovery capture\n{result.stdout}")
        page.write_text(
            '<div id="scroll"><input id="focus" autofocus>'
            '<div id="content">SCROLL</div></div>', encoding="utf-8")
        style.write_text(
            'body { margin: 0; } #scroll { width: 220px; height: 160px; overflow: auto; } '
            '#content { height: 800px; } input { width: 100px; height: 40px; }',
            encoding="utf-8",
        )
        script.write_text(
            'var armed = false;\n'
            'document.getElementById("scroll").addEventListener("wheel", function () { armed = true; });\n'
            'document.getElementById("focus").addEventListener("focus", function () {\n'
            '  if (armed) throw new Error("scroll focus failure");\n'
            '});\n',
            encoding="utf-8",
        )
        scroll_frames = root / "scroll-frames"
        result = subprocess.run(
            [str(executable), str(page), str(style), "240", "240",
             "--script", str(script), "--capture-frames", str(scroll_frames),
             "--frame-count", "3", "--frame-event", "1:wheel:40:80:-120",
             "--render-trace", str(root / "scroll-trace.jsonl")],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout)
        if "script_callback_failure boundary=autofocus reason=runtime-error" not in result.stdout:
            raise AssertionError(f"Scroll focus failure was lost\n{result.stdout}")
        if len(list(scroll_frames.glob("frame_*.bmp"))) != 3:
            raise AssertionError(f"Incomplete scroll recovery capture\n{result.stdout}")
        print("Browser callback failure recovery passed")


if __name__ == "__main__":
    main()
