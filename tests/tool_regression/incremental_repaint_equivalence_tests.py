#!/usr/bin/env python3
"""Verify that retained dirty repaint is pixel-identical to full repaint."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from dataclasses import dataclass, replace
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


def make_text_property_case(root: Path) -> CaptureCase:
    app = root / "text-property-app"
    app.mkdir()
    (app / "index.html").write_text(
        "<!doctype html><meta charset='utf-8'><style>"
        "body{margin:0;background:#fff;color:#000}"
        "#label{width:170px;height:32px;margin:0;font-family:sans-serif;"
        "font-weight:700;font-size:16px;line-height:20px;letter-spacing:2px;"
        "text-align:right;background:#fff;color:#000;border:0}"
        ".wrap{width:125px;margin:0;font-family:sans-serif;font-weight:400;"
        "font-size:12px;line-height:14px;white-space:normal;"
        "overflow-wrap:anywhere;text-transform:uppercase;"
        "text-decoration:underline;text-shadow:1px 1px 0 #f00}"
        "</style><body><input id='label' value='AB'>"
        "<p class='wrap'>Long unbrokenword and wrapped 文字</p>"
        "</body>",
        encoding="utf-8",
    )
    (app / "jellyframe.app.json").write_text(
        '{"id":"org.jellyframe.tests.text-property-repaint",'
        '"name":"Text Property Repaint","role":"app",'
        '"versionName":"1.0.0","versionCode":1,"entry":"/index.html",'
        '"runtime":{"minJellyFrame":"0.6.0","minRenderCore":"0.8.0","script":"none"},'
        '"viewport":{"designWidth":240,"designHeight":100},"fonts":[]}',
        encoding="utf-8",
    )
    (app / "repaint.jfcapture").write_text(
        "frames 3\nstep-ms 33\nstart-ms 1000\nevent 1 set-value label CDE\n"
        "event 2 set-value label AB\n",
        encoding="utf-8",
    )
    return CaptureCase(
        name="text-properties",
        app=str(app),
        frame_count=3,
        frame_script="repaint.jfcapture",
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
        cases = (*CASES, make_text_property_case(root))
        run_count = 0
        for case in cases:
            for use_app_fonts, policy in ((False, "legacy"), (True, "core")):
                backend_case = replace(
                    case,
                    name=f"{case.name}-{policy}",
                    use_app_fonts=use_app_fonts,
                )
                incremental = root / backend_case.name / "incremental"
                full = root / backend_case.name / "full"
                run_capture(shell, repo, backend_case, incremental, False)
                run_capture(shell, repo, backend_case, full, True)

                incremental_frames = sorted(incremental.glob("frame_*.bmp"))
                full_frames = sorted(full.glob("frame_*.bmp"))
                if (
                    len(incremental_frames) != backend_case.frame_count
                    or len(full_frames) != backend_case.frame_count
                ):
                    raise RuntimeError(
                        f"{backend_case.name}: expected {backend_case.frame_count} frames per mode, got "
                        f"{len(incremental_frames)} and {len(full_frames)}"
                    )
                if backend_case.require_pixel_mutation:
                    for before, after in zip(incremental_frames, incremental_frames[1:]):
                        if before.read_bytes() == after.read_bytes():
                            raise RuntimeError(
                                f"{backend_case.name}: scripted text mutation did not change pixels"
                            )
                    for before, after in zip(full_frames, full_frames[1:]):
                        if before.read_bytes() == after.read_bytes():
                            raise RuntimeError(
                                f"{backend_case.name}: full repaint did not capture the text mutation"
                            )
                    if incremental_frames[0].read_bytes() != incremental_frames[-1].read_bytes():
                        raise RuntimeError(
                            f"{backend_case.name}: returning to the original style did not restore pixels"
                        )
                    if full_frames[0].read_bytes() != full_frames[-1].read_bytes():
                        raise RuntimeError(
                            f"{backend_case.name}: full repaint did not restore the original style"
                        )
                for index, (incremental_frame, full_frame) in enumerate(
                    zip(incremental_frames, full_frames)
                ):
                    if incremental_frame.read_bytes() != full_frame.read_bytes():
                        raise RuntimeError(
                            f"{backend_case.name}: incremental repaint diverged from full repaint at frame {index}: "
                            f"{incremental_frame.name}"
                        )
                total_frames += backend_case.frame_count
                run_count += 1

    print(
        "incremental repaint matches full repaint for "
        f"{total_frames} deterministic frames across {run_count} backend/app flows"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"incremental repaint equivalence test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
