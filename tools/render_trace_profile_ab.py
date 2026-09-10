#!/usr/bin/env python3
"""Measure the desktop capture cost of opt-in Render Trace profiling.

This runner deliberately measures only paired desktop-shell process time. It
does not infer MCU FPS, DMA time, panel latency, or complete paint attribution.
Each profiled capture is matched against an unprofiled capture with identical
App, deterministic frame inputs, viewport, and capture settings. Frame BMP
hashes must match byte-for-byte before timing results are written.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


FORMAT = "jellyframe.render_trace.profile_ab.v0"


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return parsed


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, max(0, math.ceil(len(ordered) * fraction) - 1))]


def round_number(value: float) -> int | float:
    rounded = round(value, 2)
    return int(rounded) if rounded.is_integer() else rounded


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def output_paths(root: Path, mode: str, round_index: int) -> tuple[Path, Path, Path]:
    run = root / mode / f"run-{round_index:02d}"
    return run, run / "frames", run / "render-trace.jsonl"


def shell_command(args: argparse.Namespace, frame_directory: Path, trace_path: Path | None) -> list[str]:
    command = [
        str(args.shell),
        "--app", str(args.app),
        "--capture-frames", str(frame_directory),
        "--frame-count", str(args.frames),
        "--frame-step-ms", str(args.frame_step_ms),
        "--viewport-width", str(args.viewport_width),
        "--viewport-height", str(args.viewport_height),
    ]
    if args.frame_start_ms is not None:
        command.extend(("--frame-start-ms", str(args.frame_start_ms)))
    if args.frame_script is not None:
        command.extend(("--frame-script", str(args.frame_script)))
    for event in args.frame_event:
        command.extend(("--frame-event", event))
    if trace_path is not None:
        command.extend(("--render-trace", str(trace_path)))
    return command


def run_capture(args: argparse.Namespace, root: Path, mode: str, round_index: int) -> dict[str, Any]:
    run_directory, frames_directory, trace_path = output_paths(root, mode, round_index)
    frames_directory.mkdir(parents=True, exist_ok=False)
    command = shell_command(args, frames_directory, trace_path if mode == "profiled" else None)
    started = time.perf_counter_ns()
    completed = subprocess.run(command, cwd=args.repo, capture_output=True, text=True, check=False)
    elapsed_us = max(0, (time.perf_counter_ns() - started) // 1000)
    (run_directory / "stdout.log").write_text(completed.stdout, encoding="utf-8")
    (run_directory / "stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(f"{mode} run {round_index} failed with exit code {completed.returncode}")

    frames = sorted(frames_directory.glob("frame_*.bmp"))
    if len(frames) != args.frames:
        raise RuntimeError(f"{mode} run {round_index} produced {len(frames)} frames, expected {args.frames}")
    hashes = {frame.name: sha256(frame) for frame in frames}
    result: dict[str, Any] = {
        "round": round_index,
        "mode": mode,
        "command": command,
        "captureProcessUs": elapsed_us,
        "perFrameCaptureProcessUs": round_number(elapsed_us / args.frames),
        "frames": hashes,
    }
    if mode == "profiled":
        if not trace_path.is_file() or trace_path.stat().st_size == 0:
            raise RuntimeError(f"profiled run {round_index} did not produce a non-empty render trace")
        try:
            lines = [json.loads(line) for line in trace_path.read_text(encoding="utf-8-sig").splitlines() if line]
        except json.JSONDecodeError as error:
            raise RuntimeError(f"profiled run {round_index} produced invalid JSONL: {error}") from error
        frame_records = [line for line in lines if isinstance(line, dict) and line.get("type") == "frame"]
        if len(frame_records) != args.frames:
            raise RuntimeError(
                f"profiled run {round_index} trace contains {len(frame_records)} frame records, expected {args.frames}"
            )
        result["trace"] = str(trace_path.relative_to(root))
        result["traceBytes"] = trace_path.stat().st_size
    return result


def verify_pair(baseline: dict[str, Any], profiled: dict[str, Any]) -> None:
    if baseline["frames"] != profiled["frames"]:
        differing = sorted(
            name for name in set(baseline["frames"]) | set(profiled["frames"])
            if baseline["frames"].get(name) != profiled["frames"].get(name)
        )
        raise RuntimeError(
            f"round {baseline['round']} profiling changed framebuffer output: {', '.join(differing[:8])}"
        )


def mode_statistics(runs: list[dict[str, Any]]) -> dict[str, int | float]:
    values = [float(run["captureProcessUs"]) for run in runs]
    return {
        "runs": len(values),
        "averageUs": round_number(sum(values) / len(values)),
        "p50Us": round_number(percentile(values, 0.50)),
        "p95Us": round_number(percentile(values, 0.95)),
        "maxUs": round_number(max(values)),
    }


def paired_overhead_statistics(baseline_runs: list[dict[str, Any]], profiled_runs: list[dict[str, Any]]) -> dict[str, int | float]:
    values = [
        float(profiled["captureProcessUs"]) - float(baseline["captureProcessUs"])
        for baseline, profiled in zip(baseline_runs, profiled_runs)
    ]
    return {
        "runs": len(values),
        "averageUs": round_number(sum(values) / len(values)),
        "p50Us": round_number(percentile(values, 0.50)),
        "p95Us": round_number(percentile(values, 0.95)),
        "minUs": round_number(min(values)),
        "maxUs": round_number(max(values)),
    }


def markdown_report(summary: dict[str, Any]) -> str:
    baseline = summary["captureProcessUs"]["baseline"]
    profiled = summary["captureProcessUs"]["profiled"]
    overhead = summary["profilingOverhead"]
    return "\n".join((
        "# Render Trace profiling A/B",
        "",
        f"> Format: `{FORMAT}`; measured: `{summary['measuredAtUtc']}`",
        "",
        "## Scope",
        "",
        "This is paired Win32 desktop-shell capture wall time. It includes process startup, deterministic capture,",
        "frame-image output and, on the profiled side, trace collection/serialization. It is not MCU FPS, DMA, panel",
        "or a complete paint-time comparison. Every matched BMP SHA-256 was equal.",
        "",
        "## Result",
        "",
        "| Mode | Runs | Average | p50 | p95 | Max |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
        f"| baseline | {baseline['runs']} | {baseline['averageUs']} us | {baseline['p50Us']} us | {baseline['p95Us']} us | {baseline['maxUs']} us |",
        f"| profiled | {profiled['runs']} | {profiled['averageUs']} us | {profiled['p50Us']} us | {profiled['p95Us']} us | {profiled['maxUs']} us |",
        "",
        f"- paired average overhead: `{overhead['averageUs']} us`",
        f"- paired p50 overhead: `{overhead['p50Us']} us` ({overhead['p50Percent']}%)",
        f"- paired p95 overhead: `{overhead['p95Us']} us` ({overhead['p95Percent']}%)",
        f"- Frame equivalence: `{summary['frameEquivalence']}` across `{summary['framesPerRun']}` frames × `{summary['rounds']}` pairs.",
        "",
        "Do not use this result as a Render Core or device baseline. Compare it only with an archive produced by",
        "the same shell binary, App, capture inputs, machine and power state.",
        "",
    ))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shell", type=Path, required=True, help="Desktop shell executable.")
    parser.add_argument("--app", type=Path, required=True, help="App root used by both capture modes.")
    parser.add_argument("--output", type=Path, required=True, help="New directory for the complete A/B archive.")
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="Working directory for the shell.")
    parser.add_argument("--frame-script", type=Path, help="Optional deterministic .jfcapture file.")
    parser.add_argument("--frame-event", action="append", default=[], help="Repeatable FRAME:kind event argument.")
    parser.add_argument("--frames", type=positive_int, default=30, help="Frames per capture (default: 30).")
    parser.add_argument("--rounds", type=positive_int, default=5, help="Paired A/B rounds, at least 5 (default: 5).")
    parser.add_argument("--warmup", type=int, default=1, help="Unarchived paired warmup rounds (default: 1).")
    parser.add_argument("--frame-step-ms", type=positive_int, default=33, help="Deterministic frame step (default: 33).")
    parser.add_argument("--frame-start-ms", type=int, help="Optional deterministic frame-start clock.")
    parser.add_argument("--viewport-width", type=positive_int, default=300)
    parser.add_argument("--viewport-height", type=positive_int, default=300)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.shell = args.shell.resolve()
    args.app = args.app.resolve()
    args.repo = args.repo.resolve()
    args.output = args.output.resolve()
    if args.frame_script is not None:
        args.frame_script = args.frame_script.resolve()
    if args.warmup < 0:
        raise SystemExit("--warmup must not be negative")
    if args.rounds < 5:
        raise SystemExit("--rounds must be at least 5 for an interpretable paired A/B archive")
    if not args.shell.is_file():
        raise SystemExit(f"desktop shell does not exist: {args.shell}")
    if not args.app.is_dir():
        raise SystemExit(f"App root does not exist: {args.app}")
    if not args.repo.is_dir():
        raise SystemExit(f"repo does not exist: {args.repo}")
    if args.frame_script is not None and not args.frame_script.is_file():
        raise SystemExit(f"frame script does not exist: {args.frame_script}")
    if args.output.exists():
        raise SystemExit(f"refusing to write into an existing archive directory: {args.output}")

    args.output.mkdir(parents=True)
    try:
        for warmup in range(args.warmup):
            warmup_root = args.output / ".warmup" / f"pair-{warmup:02d}"
            baseline = run_capture(args, warmup_root, "baseline", warmup)
            profiled = run_capture(args, warmup_root, "profiled", warmup)
            verify_pair(baseline, profiled)
        shutil.rmtree(args.output / ".warmup", ignore_errors=True)

        baseline_runs: list[dict[str, Any]] = []
        profiled_runs: list[dict[str, Any]] = []
        for round_index in range(args.rounds):
            modes = ("baseline", "profiled") if round_index % 2 == 0 else ("profiled", "baseline")
            pair: dict[str, dict[str, Any]] = {}
            for mode in modes:
                pair[mode] = run_capture(args, args.output, mode, round_index)
            verify_pair(pair["baseline"], pair["profiled"])
            baseline_runs.append(pair["baseline"])
            profiled_runs.append(pair["profiled"])

        baseline_stats = mode_statistics(baseline_runs)
        profiled_stats = mode_statistics(profiled_runs)
        overhead_stats = paired_overhead_statistics(baseline_runs, profiled_runs)
        summary: dict[str, Any] = {
            "format": FORMAT,
            "measuredAtUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "scope": "paired Win32 desktop-shell capture process wall time; not device or complete paint timing",
            "shell": {"path": str(args.shell), "sha256": sha256(args.shell)},
            "app": str(args.app),
            "frameScript": str(args.frame_script) if args.frame_script else None,
            "frameEvents": args.frame_event,
            "framesPerRun": args.frames,
            "rounds": args.rounds,
            "warmupPairs": args.warmup,
            "viewport": {"width": args.viewport_width, "height": args.viewport_height},
            "frameStepMs": args.frame_step_ms,
            "frameStartMs": args.frame_start_ms,
            "platform": {"system": platform.system(), "release": platform.release(), "python": sys.version.split()[0]},
            "frameEquivalence": "pass",
            "captureProcessUs": {"baseline": baseline_stats, "profiled": profiled_stats},
            "profilingOverhead": {
                **overhead_stats,
                "p50Percent": round_number(100.0 * float(overhead_stats["p50Us"]) / float(baseline_stats["p50Us"])) if baseline_stats["p50Us"] else 0,
                "p95Percent": round_number(100.0 * float(overhead_stats["p95Us"]) / float(baseline_stats["p95Us"])) if baseline_stats["p95Us"] else 0,
            },
            "runs": {"baseline": baseline_runs, "profiled": profiled_runs},
        }
        (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        (args.output / "report.md").write_text(markdown_report(summary), encoding="utf-8")
        print(json.dumps({"format": FORMAT, "output": str(args.output), "frameEquivalence": "pass"}))
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Render Trace profiling A/B failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
