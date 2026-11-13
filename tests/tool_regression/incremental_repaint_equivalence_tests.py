#!/usr/bin/env python3
"""Verify that retained dirty repaint is pixel-identical to full repaint."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CaptureCase:
    name: str
    app: str
    frame_count: int
    frame_script: str | None = None
    frame_events: tuple[str, ...] = ()
    frame_step_ms: int | None = None
    frame_start_ms: int | None = None
    use_app_fonts: bool = False
    require_pixel_mutation: bool = False


CASES = (
    CaptureCase(
        name="motion",
        app="samples/apps/packages/jelly_motion_lab",
        frame_count=30,
        frame_events=("8:click:150:260",),
        frame_step_ms=33,
        frame_start_ms=1000,
    ),
    CaptureCase(
        name="route-tabs",
        app="samples/apps/packages/jelly_route_tabs",
        frame_count=9,
        frame_script="capture_route_tabs.jfcapture",
    ),
    CaptureCase(
        name="weather",
        app="samples/apps/packages/watch_weather",
        frame_count=36,
        frame_script="capture_weather_interaction.jfcapture",
    ),
    CaptureCase(
        name="component-scroll",
        app="samples/apps/packages/jelly_component_recipes",
        frame_count=12,
        frame_script="capture_scroll_recipes.jfcapture",
    ),
    CaptureCase(
        name="watch-face",
        app="samples/apps/packages/jelly_watch_face",
        frame_count=30,
        frame_script="capture_watch_face_30fps.jfcapture",
    ),
)


def run_capture(shell: Path, repo: Path, case: CaptureCase, output: Path, force_full: bool) -> None:
    app = repo / case.app
    command = [
        str(shell),
        "--app",
        str(app),
    ]
    if case.frame_script is not None:
        command.extend(("--frame-script", str(app / case.frame_script)))
    command.extend((
        "--capture-frames",
        str(output),
        "--frame-count",
        str(case.frame_count),
        "--viewport-width",
        "300",
        "--viewport-height",
        "300",
    ))
    if case.frame_step_ms is not None:
        command.extend(("--frame-step-ms", str(case.frame_step_ms)))
    if case.frame_start_ms is not None:
        command.extend(("--frame-start-ms", str(case.frame_start_ms)))
    for event in case.frame_events:
        command.extend(("--frame-event", event))
    if case.use_app_fonts:
        command.append("--use-app-fonts")
    if force_full:
        command.append("--force-full-repaint")
    result = subprocess.run(command, cwd=repo, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"capture failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    expected_policy = "core" if case.use_app_fonts else "legacy"
    if f"text_policy={expected_policy}" not in result.stdout:
        raise RuntimeError(
            f"{case.name}: expected text_policy={expected_policy} in capture output\n"
            f"stdout:\n{result.stdout}"
        )


def make_core_letter_spacing_case(root: Path) -> CaptureCase:
    app = root / "core-letter-spacing-app"
    app.mkdir()
    (app / "index.html").write_text(
        "<!doctype html><meta charset='utf-8'><body style='margin:0;background:#fff'>"
        "<input id='label' value='AB' style='width:180px;height:40px;font-size:16px;"
        "letter-spacing:2px;color:#000;background:#fff;border:0'></body>",
        encoding="utf-8",
    )
    (app / "jellyframe.app.json").write_text(
        '{"id":"org.jellyframe.tests.core-letter-spacing",'
        '"name":"Core Letter Spacing Repaint","role":"app",'
        '"versionName":"1.0.0","versionCode":1,"entry":"/index.html",'
        '"runtime":{"minJellyFrame":"0.6.0","minRenderCore":"0.8.0","script":"none"},'
        '"viewport":{"designWidth":240,"designHeight":100},"fonts":[]}',
        encoding="utf-8",
    )
    (app / "repaint.jfcapture").write_text(
        "frames 3\nstep-ms 33\nstart-ms 1000\nevent 1 set-value label CDE\n",
        encoding="utf-8",
    )
    return CaptureCase(
        name="core-letter-spacing",
        app=str(app),
        frame_count=3,
        frame_script="repaint.jfcapture",
        use_app_fonts=True,
        require_pixel_mutation=True,
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: incremental_repaint_equivalence_tests.py SHELL REPO", file=sys.stderr)
        return 2
    shell = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-repaint-") as temporary:
        root = Path(temporary)
        total_frames = 0
        cases = (*CASES, make_core_letter_spacing_case(root))
        for case in cases:
            incremental = root / case.name / "incremental"
            full = root / case.name / "full"
            run_capture(shell, repo, case, incremental, False)
            run_capture(shell, repo, case, full, True)

            incremental_frames = sorted(incremental.glob("frame_*.bmp"))
            full_frames = sorted(full.glob("frame_*.bmp"))
            if len(incremental_frames) != case.frame_count or len(full_frames) != case.frame_count:
                raise RuntimeError(
                    f"{case.name}: expected {case.frame_count} frames per mode, got "
                    f"{len(incremental_frames)} and {len(full_frames)}"
                )
            if case.require_pixel_mutation:
                if incremental_frames[0].read_bytes() == incremental_frames[1].read_bytes():
                    raise RuntimeError(f"{case.name}: scripted label mutation did not change pixels")
                if full_frames[0].read_bytes() == full_frames[1].read_bytes():
                    raise RuntimeError(f"{case.name}: full repaint did not capture the label mutation")
            for index, (incremental_frame, full_frame) in enumerate(
                zip(incremental_frames, full_frames)
            ):
                if incremental_frame.read_bytes() != full_frame.read_bytes():
                    raise RuntimeError(
                        f"{case.name}: incremental repaint diverged from full repaint at frame {index}: "
                        f"{incremental_frame.name}"
                    )
            total_frames += case.frame_count

    print(
        "incremental repaint matches full repaint for "
        f"{total_frames} deterministic frames across {len(cases)} app flows"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"incremental repaint equivalence test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
