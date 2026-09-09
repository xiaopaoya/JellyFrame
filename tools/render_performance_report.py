#!/usr/bin/env python3
"""Build a bounded, source-aware Render Core performance report.

The tool deliberately keeps desktop timings, device aggregate telemetry, and
per-frame traces separate. It never infers MCU stage timings from a desktop
measurement or turns a microbenchmark into an application attribution.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import re
import statistics
import sys
from pathlib import Path
from typing import Any


TRACE_FORMAT = "jellyframe.render.trace.v0"
REPORT_FORMAT = "jellyframe.render.performance.report"


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise SystemExit(f"JSON root must be an object: {path}")
    return value


def number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(float(value)) and float(value) >= 0:
        return float(value)
    return None


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(percent * len(ordered)) - 1))
    return ordered[index]


def round_number(value: float) -> int | float:
    rounded = round(value, 2)
    return int(rounded) if rounded.is_integer() else rounded


def normalize_frame(raw: dict[str, Any], source: str, fallback_index: int) -> dict[str, Any] | None:
    stages = raw.get("stagesUs", raw.get("timingsUs", {}))
    if not isinstance(stages, dict):
        stages = {}
    normalized_stages = {
        str(key): round_number(value)
        for key, raw_value in stages.items()
        if (value := number(raw_value)) is not None and str(key) != "total"
    }
    explicit_total = number(raw.get("totalUs"))
    total = explicit_total
    if total is None:
        total = number(stages.get("total"))
    if total is None:
        total = sum(float(value) for value in normalized_stages.values())
    if not normalized_stages and explicit_total is None:
        return None
    result: dict[str, Any] = {
        "source": source,
        "frame": raw.get("frame", raw.get("frameIndex", fallback_index)),
        "totalUs": round_number(total),
        "stagesUs": normalized_stages,
    }
    for key in ("action", "reason", "repaint", "dirtyMode", "dirtyReason",
                "dirtyRectCount", "dirtyAreaPercent", "pipeline", "timingComplete"):
        if key in raw:
            result[key] = raw[key]
    commands = raw.get("commands", raw.get("commandAttribution"))
    if isinstance(commands, list):
        result["commands"] = commands
    return result


def frames_from_pipeline_report(path: Path) -> tuple[list[dict[str, Any]], list[str], dict[str, Any]]:
    report = read_json(path)
    pipeline = report.get("pipelineDiagnostics", report)
    if not isinstance(pipeline, dict):
        return [], [], {}
    frame = normalize_frame(
        {
            "timingsUs": pipeline.get("timingsUs", {}),
            "pipeline": pipeline.get("pipeline", {}),
            "action": pipeline.get("frameUpdate", {}).get("action")
            if isinstance(pipeline.get("frameUpdate"), dict) else None,
            "reason": pipeline.get("frameUpdate", {}).get("reason")
            if isinstance(pipeline.get("frameUpdate"), dict) else None,
        },
        str(path),
        0,
    )
    return ([frame] if frame else []), ["This is a single desktop/pseudo-browser measurement, not device timing."], {}


def frames_from_trace(path: Path) -> tuple[list[dict[str, Any]], list[str], dict[str, Any]]:
    frames: list[dict[str, Any]] = []
    warnings: list[str] = []
    metadata: dict[str, Any] = {}
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError as error:
        raise SystemExit(f"failed to read trace {path}: {error}") from error
    previous_frame: int | None = None
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            warnings.append(f"{path}:{line_number}: invalid JSONL record ({error.msg})")
            continue
        if not isinstance(value, dict):
            warnings.append(f"{path}:{line_number}: ignored non-object record")
            continue
        if value.get("format") == TRACE_FORMAT and value.get("type") != "frame":
            metadata.update({key: value[key] for key in ("viewport", "appId", "profile") if key in value})
            continue
        if value.get("type", "frame") != "frame":
            continue
        raw_frame = value.get("frame", value.get("frameIndex"))
        if not isinstance(raw_frame, int) or isinstance(raw_frame, bool) or raw_frame < 0:
            warnings.append(f"{path}:{line_number}: frame number must be a non-negative integer")
            continue
        if previous_frame is not None and raw_frame <= previous_frame:
            warnings.append(
                f"{path}:{line_number}: frame number {raw_frame} is not strictly greater than {previous_frame}"
            )
            continue
        frame = normalize_frame(value, str(path), len(frames))
        if frame is None:
            warnings.append(f"{path}:{line_number}: frame has no usable timing")
        else:
            frames.append(frame)
            previous_frame = raw_frame
    return frames, warnings, metadata


def read_microbench(path: Path) -> list[dict[str, Any]]:
    pattern = re.compile(r"^(?P<name>[a-zA-Z0-9_]+)\s+iterations=(?P<iterations>\d+)\s+avg_us=(?P<avg>[-+0-9.eE]+)\s*$")
    results: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError as error:
        raise SystemExit(f"failed to read microbench output {path}: {error}") from error
    for line in lines:
        match = pattern.match(line.strip())
        if match:
            results.append({
                "name": match.group("name"),
                "iterations": int(match.group("iterations")),
                "avgUs": round_number(float(match.group("avg"))),
                "source": str(path),
            })
    return results


def load_device_telemetry(path: Path) -> dict[str, Any]:
    values: dict[str, Any] = {}
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError as error:
        raise SystemExit(f"failed to read device telemetry {path}: {error}") from error
    aliases = {
        "frame_ms_avg": "averageFrameMs", "frame_ms_p95": "p95FrameMs",
        "frame_ms_max": "maxFrameMs", "present_ms_avg": "averagePresentMs",
        "present_ms_p95": "p95PresentMs", "dma_wait_ms_avg": "averageDmaWaitMs",
        "flush_done_ms_avg": "averageFlushDoneMs", "frames": "frames",
        "full": "fullFrames", "dirty": "dirtyFrames", "flushes": "flushes",
    }
    for line in lines:
        for key, raw_value in re.findall(r"([a-zA-Z][a-zA-Z0-9_]*)=([^\s]+)", line):
            target = aliases.get(key)
            if target is None:
                continue
            parsed = number(raw_value)
            if parsed is not None:
                values[target] = round_number(parsed)
    return {"source": str(path), "metrics": values}


def aggregate(frames: list[dict[str, Any]]) -> dict[str, Any]:
    totals = [float(frame["totalUs"]) for frame in frames]
    stage_totals: dict[str, float] = {}
    for frame in frames:
        for name, raw_value in frame.get("stagesUs", {}).items():
            value = number(raw_value)
            if value is not None:
                stage_totals[name] = stage_totals.get(name, 0.0) + value
    total_stage_time = sum(stage_totals.values())
    shares = {
        name: round_number(100.0 * value / total_stage_time) if total_stage_time else 0
        for name, value in sorted(stage_totals.items(), key=lambda entry: entry[1], reverse=True)
    }
    command_totals: dict[str, float] = {}
    for frame in frames:
        for command in frame.get("commands", []):
            if not isinstance(command, dict):
                continue
            name = str(command.get("name", command.get("type", "unknown")))
            value = number(command.get("us", command.get("microseconds")))
            if value is not None:
                command_totals[name] = command_totals.get(name, 0.0) + value
    return {
        "frameCount": len(frames),
        "totalUs": {
            "average": round_number(statistics.fmean(totals)) if totals else 0,
            "p50": round_number(percentile(totals, 0.50)),
            "p95": round_number(percentile(totals, 0.95)),
            "max": round_number(max(totals)) if totals else 0,
        },
        "stageTotalsUs": {name: round_number(value) for name, value in sorted(stage_totals.items(), key=lambda entry: entry[1], reverse=True)},
        "stageSharesPercent": shares,
        "commandAttributionUs": {name: round_number(value) for name, value in sorted(command_totals.items(), key=lambda entry: entry[1], reverse=True)},
    }


def build_report(report_paths: list[Path], trace_paths: list[Path], telemetry_paths: list[Path], microbench_paths: list[Path]) -> dict[str, Any]:
    frames: list[dict[str, Any]] = []
    warnings: list[str] = []
    metadata: dict[str, Any] = {}
    for path in report_paths:
        loaded, notes, extra = frames_from_pipeline_report(path)
        frames.extend(loaded)
        warnings.extend(notes)
        metadata.update(extra)
    for path in trace_paths:
        loaded, notes, extra = frames_from_trace(path)
        frames.extend(loaded)
        warnings.extend(notes)
        metadata.update(extra)
    device = [load_device_telemetry(path) for path in telemetry_paths]
    microbench = [entry for path in microbench_paths for entry in read_microbench(path)]
    if not frames and not device and not microbench:
        raise SystemExit("no usable performance input was found")
    limitations = [
        "Desktop/pseudo-browser timings and device telemetry are reported separately.",
        "Microbench probes measure isolated commands and do not prove per-element application cost.",
        "Element-level attribution requires producer records with a stable node id or command attribution.",
    ]
    return {
        "format": REPORT_FORMAT,
        "formatVersion": 0,
        "metadata": metadata,
        "summary": aggregate(frames),
        "frames": frames,
        "deviceTelemetry": device,
        "microbenchProbes": microbench,
        "warnings": warnings,
        "limitations": limitations,
    }


def render_html(report: dict[str, Any]) -> str:
    summary = report["summary"]
    rows = []
    for name, share in summary.get("stageSharesPercent", {}).items():
        rows.append(
            f"<tr><td><code>{html.escape(name)}</code></td><td>{html.escape(str(summary['stageTotalsUs'].get(name, 0)))} us</td>"
            f"<td><meter min='0' max='100' value='{float(share)}'></meter> {html.escape(str(share))}%</td></tr>"
        )
    frame_rows = []
    for frame in report.get("frames", []):
        frame_rows.append(
            f"<tr><td>{html.escape(str(frame.get('frame', '')))}</td><td>{html.escape(str(frame.get('totalUs', 0)))} us</td>"
            f"<td>{html.escape(str(frame.get('action', '')))}</td><td>{html.escape(str(frame.get('reason', '')))}</td>"
            f"<td>{html.escape(str(frame.get('dirtyRectCount', '')))}</td><td>{html.escape(str(frame.get('dirtyAreaPercent', '')))}</td></tr>"
        )
    return """<!doctype html>
<meta charset="utf-8"><title>JellyFrame Render Performance</title>
<style>body{{font:14px system-ui,sans-serif;max-width:1100px;margin:28px auto;color:#202124}}table{{border-collapse:collapse;width:100%;margin:12px 0 28px}}th,td{{border-bottom:1px solid #ddd;text-align:left;padding:7px}}meter{{width:180px;height:12px}}code{{font-family:ui-monospace,monospace}}small{{color:#5f6368}}</style>
<h1>JellyFrame Render Performance</h1>
<p><small>Source-aware report. Desktop timings, device aggregate telemetry and isolated microbenchmarks are not combined.</small></p>
<h2>Frame summary</h2>
<p>Frames: {frames} · average: {average} us · p50: {p50} us · p95: {p95} us · max: {maximum} us</p>
<h2>Stage share</h2><table><tr><th>Stage</th><th>Total</th><th>Share</th></tr>{stage_rows}</table>
<h2>Frames</h2><table><tr><th>Frame</th><th>Total</th><th>Action</th><th>Reason</th><th>Dirty rects</th><th>Dirty area %</th></tr>{frame_rows}</table>
<h2>Limits</h2><ul>{limits}</ul>
""".format(
        frames=html.escape(str(summary.get("frameCount", 0))),
        average=html.escape(str(summary.get("totalUs", {}).get("average", 0))),
        p50=html.escape(str(summary.get("totalUs", {}).get("p50", 0))),
        p95=html.escape(str(summary.get("totalUs", {}).get("p95", 0))),
        maximum=html.escape(str(summary.get("totalUs", {}).get("max", 0))),
        stage_rows="".join(rows) or "<tr><td colspan='3'>No stage timings</td></tr>",
        frame_rows="".join(frame_rows) or "<tr><td colspan='6'>No per-frame trace</td></tr>",
        limits="".join(f"<li>{html.escape(item)}</li>" for item in report.get("limitations", [])),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="append", type=Path, default=[], help="Package/pipeline JSON report; may be repeated.")
    parser.add_argument("--trace", action="append", type=Path, default=[], help=f"Versioned JSONL frame trace ({TRACE_FORMAT}); may be repeated.")
    parser.add_argument("--device-telemetry", action="append", type=Path, default=[], help="Device aggregate telemetry log; may be repeated.")
    parser.add_argument("--microbench", action="append", type=Path, default=[], help="Render Core microbench stdout; may be repeated.")
    parser.add_argument("--output", type=Path, required=True, help="Output JSON report.")
    parser.add_argument("--html-output", type=Path, help="Optional self-contained HTML summary.")
    args = parser.parse_args()
    result = build_report(args.report, args.trace, args.device_telemetry, args.microbench)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.html_output:
        args.html_output.parent.mkdir(parents=True, exist_ok=True)
        args.html_output.write_text(render_html(result), encoding="utf-8")
    print(json.dumps({"format": REPORT_FORMAT, "frames": result["summary"]["frameCount"], "output": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
