#!/usr/bin/env python3
"""Verify that retained dirty repaint is pixel-identical to full repaint."""

from __future__ import annotations

import json
import shutil
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
    viewport_width: int = 300
    viewport_height: int = 300
    require_pixel_mutation: bool = False
    require_text_presence: bool = False
    text_region: tuple[int, int, int, int] | None = None
    require_forecast_presence: bool = False
    forecast_frame_indices: tuple[int, ...] = ()


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
        require_text_presence=True,
        text_region=(0, 60, 300, 110),
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
        str(case.viewport_width),
        "--viewport-height",
        str(case.viewport_height),
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
    if case.require_pixel_mutation and case.use_app_fonts and "layout-core-text-refused" in result.stdout:
        raise RuntimeError(
            f"{case.name}: Core text layout refused a positive repaint fixture\n"
            f"stdout:\n{result.stdout}"
        )


def make_text_property_case(
    root: Path, property_name: str, initial: str, changed: str, core_version: str
) -> CaptureCase:
    app = root / f"text-property-{property_name}"
    app.mkdir()
    (app / "index.html").write_text(
        "<!doctype html><meta charset='utf-8'><style>"
        "body{margin:0;background:#fff;color:#000}"
        "#label{display:block;width:220px;height:48px;margin:0;padding:0;"
        "font-family:sans-serif;font-size:20px;font-weight:400;line-height:28px;"
        "text-align:start;color:#000;background:#fff;border:0}"
        "#toggle{display:block;width:100px;height:24px;margin:0}"
        "</style><body><p id='label'>CORE TEXT</p><button id='toggle'>Toggle</button>"
        f"<script>var label=document.getElementById('label');"
        f"var initial='{initial}', changed='{changed}';"
        f"label.style.{property_name}=initial;"
        f"document.getElementById('toggle').addEventListener('click',function(){{"
        f"label.style.{property_name}=label.style.{property_name}===changed?initial:changed;"
        "});</script></body>",
        encoding="utf-8",
    )
    (app / "jellyframe.app.json").write_text(
        f'{{"id":"org.jellyframe.tests.text-property-{property_name}",'
        f'"name":"Text Property {property_name}","role":"app",'
        '"versionName":"1.0.0","versionCode":1,"entry":"/index.html",'
        '"runtime":{"minJellyFrame":"0.6.0","minRenderCore":"' + core_version + '","script":"classic"},'
        '"viewport":{"designWidth":240,"designHeight":100},"fonts":[]}',
        encoding="utf-8",
    )
    (app / "repaint.jfcapture").write_text(
        "frames 3\nstep-ms 33\nstart-ms 1000\nevent 1 click-id toggle\n"
        "event 2 click-id toggle\n",
        encoding="utf-8",
    )
    return CaptureCase(
        name=f"text-{property_name}",
        app=str(app),
        frame_count=3,
        frame_script="repaint.jfcapture",
        require_pixel_mutation=True,
        require_text_presence=True,
        text_region=(0, 0, 220, 48),
    )


def adapt_sample_case(case: CaptureCase, repo: Path, root: Path, core_version: str) -> CaptureCase:
    source = Path(case.app)
    if source.is_absolute():
        return case
    source = repo / source
    destination = root / f"sample-{case.name}"
    shutil.copytree(source, destination)
    manifest_path = destination / "jellyframe.app.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["runtime"]["minRenderCore"] = core_version
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return replace(case, app=str(destination))


def make_band_shell_case(
    repo: Path, root: Path, core_version: str, exercise_route: bool = False
) -> CaptureCase:
    source = repo / "ports/esp32s3-idf/resources/app"
    suffix = "route" if exercise_route else "static"
    destination = root / f"band-shell-launcher-{suffix}"
    (destination / "styles").mkdir(parents=True)
    shutil.copy2(source / "band_shell.html", destination / "band_shell.html")
    shutil.copy2(source / "styles/band_shell.css", destination / "styles/band_shell.css")
    manifest = json.loads((source / "jellyframe.app.json").read_text(encoding="utf-8"))
    manifest["entry"] = "/band_shell.html"
    manifest["runtime"]["minRenderCore"] = core_version
    manifest["viewport"] = {"designWidth": 172, "designHeight": 320, "shape": "rect"}
    (destination / "jellyframe.app.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    # Keep launcher markup and styling byte-for-byte. Device routes switch the
    # active view class; the host fixture uses that same state transition.
    html_path = destination / "band_shell.html"
    html = html_path.read_text(encoding="utf-8")
    capture_script = None
    frame_count = 1
    frame_indices: tuple[int, ...] = ()
    if exercise_route:
        shim = """<script>
          (function() {
            var routes = {'home-to-weather': 'view-weather', 'weather-home': 'view-home'};
            Object.keys(routes).forEach(function(id) {
              var control = document.getElementById(id);
              if (!control) return;
              control.addEventListener('click', function() {
                document.querySelectorAll('.band-view').forEach(function(view) {
                  view.setAttribute('class', 'band-view' + (view.id === routes[id] ? ' active' : ''));
                });
              });
            });
          })();
        </script>"""
        html = html.replace("</body>", shim + "</body>")
        capture_script = str(destination / "launcher-weather-route.jfcapture")
        Path(capture_script).write_text(
            "frames 9\nstep-ms 33\nstart-ms 1000\n"
            "event 2 click 86 214\n"
            "event 7 click 25 20\n",
            encoding="utf-8",
        )
        frame_count = 9
        frame_indices = (3, 4, 5, 6)
    else:
        html = html.replace('id="view-home" class="band-view active"', 'id="view-home" class="band-view"')
        html = html.replace('id="view-weather" class="band-view"', 'id="view-weather" class="band-view active"')
    html_path.write_text(html, encoding="utf-8")
    return CaptureCase(
        name="launcher-weather-route" if exercise_route else "launcher-weather-source",
        app=str(destination),
        frame_count=frame_count,
        frame_script=capture_script,
        viewport_width=172,
        viewport_height=320,
        require_forecast_presence=True,
        forecast_frame_indices=frame_indices,
    )


def bmp_region_pixels(path: Path, region: tuple[int, int, int, int]) -> bytes:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"{path}: expected a Windows BMP capture")
    pixel_offset = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little", signed=True)
    signed_height = int.from_bytes(data[22:26], "little", signed=True)
    bits_per_pixel = int.from_bytes(data[28:30], "little")
    if width <= 0 or signed_height == 0 or bits_per_pixel not in (24, 32):
        raise RuntimeError(f"{path}: unsupported BMP geometry or pixel format")
    height = abs(signed_height)
    left, top, right, bottom = region
    if left < 0 or top < 0 or right > width or bottom > height:
        raise RuntimeError(f"{path}: text proof region is outside the captured frame")
    pixel_bytes = bits_per_pixel // 8
    stride = ((width * pixel_bytes + 3) // 4) * 4
    pixels = bytearray()
    for y in range(top, bottom):
        bmp_y = height - 1 - y if signed_height > 0 else y
        for x in range(left, right):
            offset = pixel_offset + bmp_y * stride + x * pixel_bytes
            pixels.extend(data[offset:offset + 3])
    return bytes(pixels)


def bmp_region_has_ink(path: Path, region: tuple[int, int, int, int]) -> bool:
    pixels = bmp_region_pixels(path, region)
    return any(pixels[offset:offset + 3] != b"\xff\xff\xff" for offset in range(0, len(pixels), 3))


def bmp_region_has_light_ink(path: Path, region: tuple[int, int, int, int]) -> bool:
    pixels = bmp_region_pixels(path, region)
    return any(
        min(pixels[offset:offset + 3]) > 96
        for offset in range(0, len(pixels), 3)
    )


def bmp_region_has_visible_pixels(path: Path, region: tuple[int, int, int, int]) -> bool:
    pixels = bmp_region_pixels(path, region)
    return any(
        max(pixels[offset:offset + 3]) > 32
        for offset in range(0, len(pixels), 3)
    )


def main() -> int:
    include_core_text = False
    core_version = "0.8.0"
    selected_case: str | None = None
    args: list[str] = []
    index = 1
    while index < len(sys.argv):
        argument = sys.argv[index]
        if argument == "--include-core-text":
            include_core_text = True
            index += 1
        elif argument == "--core-version" and index + 1 < len(sys.argv):
            core_version = sys.argv[index + 1]
            index += 2
        elif argument == "--case" and index + 1 < len(sys.argv):
            selected_case = sys.argv[index + 1]
            index += 2
        else:
            args.append(argument)
            index += 1
    if len(args) != 2:
        print(
            "usage: incremental_repaint_equivalence_tests.py SHELL REPO [--core-version VERSION] [--include-core-text] [--case NAME]",
            file=sys.stderr,
        )
        return 2
    shell = Path(args[0]).resolve()
    repo = Path(args[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="jellyframe-repaint-") as temporary:
        root = Path(temporary)
        total_frames = 0
        text_property_cases = (
            make_text_property_case(root, "color", "rgb(0,0,0)", "rgb(220,0,0)", core_version),
            make_text_property_case(root, "fontSize", "20px", "28px", core_version),
            make_text_property_case(root, "fontWeight", "400", "700", core_version),
            make_text_property_case(root, "textAlign", "start", "end", core_version),
            make_text_property_case(root, "lineHeight", "28px", "40px", core_version),
        )
        cases = tuple(adapt_sample_case(case, repo, root, core_version) for case in CASES) + text_property_cases
        cases += (
            make_band_shell_case(repo, root, core_version),
            make_band_shell_case(repo, root, core_version, exercise_route=True),
        )
        if selected_case is not None:
            cases = tuple(case for case in cases if case.name == selected_case)
            if not cases:
                raise RuntimeError(f"unknown capture case: {selected_case}")
        run_count = 0
        policies = [(False, "legacy")]
        if include_core_text:
            policies.append((True, "core"))
        for case in cases:
            for use_app_fonts, policy in policies:
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
                if backend_case.require_pixel_mutation and backend_case.use_app_fonts:
                    if backend_case.text_region is None:
                        raise RuntimeError(f"{backend_case.name}: text proof region is missing")
                    if not bmp_region_has_ink(incremental_frames[0], backend_case.text_region):
                        raise RuntimeError(
                            f"{backend_case.name}: initial Core text fixture contains no visible glyph pixels"
                        )
                    if not bmp_region_has_ink(full_frames[0], backend_case.text_region):
                        raise RuntimeError(
                            f"{backend_case.name}: initial full repaint contains no visible glyph pixels"
                        )
                    incremental_region = [
                        bmp_region_pixels(frame, backend_case.text_region)
                        for frame in incremental_frames
                    ]
                    full_region = [
                        bmp_region_pixels(frame, backend_case.text_region)
                        for frame in full_frames
                    ]
                    if incremental_region[0] == incremental_region[1]:
                        raise RuntimeError(
                            f"{backend_case.name}: style mutation did not change text-region pixels"
                        )
                    if incremental_region[0] != incremental_region[-1]:
                        raise RuntimeError(
                            f"{backend_case.name}: restoring the style did not restore text-region pixels"
                        )
                    if full_region[0] == full_region[1]:
                        raise RuntimeError(
                            f"{backend_case.name}: full repaint did not show changed text-region pixels"
                        )
                    if full_region[0] != full_region[-1]:
                        raise RuntimeError(
                            f"{backend_case.name}: full repaint did not restore text-region pixels"
                        )
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
                if backend_case.require_text_presence and backend_case.use_app_fonts:
                    if backend_case.text_region is None:
                        raise RuntimeError(f"{backend_case.name}: text proof region is missing")
                    for mode, frames in (("incremental", incremental_frames), ("full", full_frames)):
                        for index, frame in enumerate(frames):
                            if not bmp_region_has_ink(frame, backend_case.text_region):
                                raise RuntimeError(
                                    f"{backend_case.name}: {mode} repaint has no visible text "
                                    f"at frame {index} ({frame.name})"
                                )
                if backend_case.require_forecast_presence and backend_case.use_app_fonts:
                    # Check launcher forecast text rows separately from the yellow icon dots.
                    forecast_regions = (
                        (12, 165, 46, 197),
                        (47, 165, 81, 197),
                        (82, 165, 116, 197),
                        (117, 165, 151, 197),
                    )
                    frame_indices = backend_case.forecast_frame_indices or tuple(
                        range(backend_case.frame_count)
                    )
                    for mode, frames in (("incremental", incremental_frames), ("full", full_frames)):
                        for index in frame_indices:
                            frame = frames[index]
                            missing = [
                                region for region in forecast_regions
                                if not bmp_region_has_light_ink(frame, region)
                            ]
                            if missing:
                                raise RuntimeError(
                                    f"{backend_case.name}: forecast text is missing in {mode} frame {index}; "
                                    f"missing regions={missing} ({frame.name})"
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
