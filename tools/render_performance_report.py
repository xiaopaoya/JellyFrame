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
MAX_COMMAND_OWNER_GROUPS = 128
MAX_TRACE_DIRTY_RECTS = 32
MAX_TRACE_STAGE_SPANS = 64
MAX_TRACE_COMMAND_SPANS = 256
MAX_TRACE_MUTATION_SOURCES = 16
TRACE_MUTATION_SOURCE_KINDS = frozenset(("input", "script", "animation", "scroll", "system", "host", "initial"))
SAFE_TRACE_OWNER = re.compile(r"^[A-Za-z0-9:_-]{1,64}$")
DEVICE_PROFILE_RECORD_KINDS = frozenset((
    "device_profile",
    "device_profile_timing",
    "device_profile_pipeline",
    "device_profile_present",
    "device_profile_counters",
))
DEVICE_PROFILE_RECORD_PATTERN = re.compile(
    r"\b(?P<kind>device_profile(?:_timing|_pipeline|_present|_counters)?)\s+(?P<body>.+)$"
)
ANSI_ESCAPE_PATTERN = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


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


def safe_integer(value: Any) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    if value < 0:
        return None
    return value


def normalize_trace_rect(value: Any) -> dict[str, int] | None:
    if not isinstance(value, dict):
        return None
    coordinates = {key: safe_integer(value.get(key)) for key in ("x", "y", "width", "height")}
    if any(item is None for item in coordinates.values()):
        return None
    return coordinates  # type: ignore[return-value]


def normalize_trace_spans(value: Any, *, command: bool) -> tuple[list[dict[str, Any]], bool]:
    if not isinstance(value, list):
        return [], False
    limit = MAX_TRACE_COMMAND_SPANS if command else MAX_TRACE_STAGE_SPANS
    spans: list[dict[str, Any]] = []
    for item in value[:limit]:
        if not isinstance(item, dict):
            continue
        name = item.get("type" if command else "name")
        start_us = safe_integer(item.get("startUs"))
        duration_us = safe_integer(item.get("durationUs"))
        if not isinstance(name, str) or not name.strip() or start_us is None or duration_us is None:
            continue
        normalized: dict[str, Any] = {
            ("type" if command else "name"): name.strip()[:96],
            "startUs": start_us,
            "durationUs": duration_us,
        }
        if command:
            owner = command_owner(item)
            pixels = safe_integer(item.get("pixels"))
            normalized["owner"] = owner
            normalized["pixels"] = pixels if pixels is not None else 0
            rect = normalize_trace_rect(item.get("rect"))
            if rect is not None:
                normalized["rect"] = rect
        spans.append(normalized)
    return spans, len(value) > limit


def normalize_trace_mutation_sources(value: Any) -> tuple[list[dict[str, Any]], bool]:
    if not isinstance(value, list):
        return [], False
    sources: list[dict[str, Any]] = []
    for item in value[:MAX_TRACE_MUTATION_SOURCES]:
        if not isinstance(item, dict):
            continue
        kind = item.get("kind")
        owner = item.get("owner")
        dirty_flags = safe_integer(item.get("dirtyFlags"))
        generation = safe_integer(item.get("mutationGeneration"))
        count = safe_integer(item.get("count"))
        dirty_rect_indexes = item.get("dirtyRectIndexes")
        if (not isinstance(kind, str) or kind not in TRACE_MUTATION_SOURCE_KINDS or
                not isinstance(owner, str) or not SAFE_TRACE_OWNER.fullmatch(owner) or
                dirty_flags is None or generation is None or count is None or count < 1 or
                not isinstance(item.get("mutation"), bool) or
                not isinstance(item.get("invalidation"), bool) or
                not isinstance(dirty_rect_indexes, list) or
                any(safe_integer(index) is None or index >= MAX_TRACE_DIRTY_RECTS
                    for index in dirty_rect_indexes)):
            continue
        normalized: dict[str, Any] = {
            "kind": kind,
            "owner": owner,
            "dirtyFlags": dirty_flags,
            "mutationGeneration": generation,
            "count": count,
            "mutation": item["mutation"],
            "invalidation": item["invalidation"],
            "dirtyRectIndexes": [safe_integer(index) for index in dirty_rect_indexes],
        }
        owner_bounds = normalize_trace_rect(item.get("ownerBounds"))
        if owner_bounds is not None:
            normalized["ownerBounds"] = owner_bounds
        sources.append(normalized)
    return sources, len(value) > MAX_TRACE_MUTATION_SOURCES


def trace_rect_intersects(left: dict[str, int], right: dict[str, int]) -> bool:
    return (min(left["x"] + left["width"], right["x"] + right["width"]) > max(left["x"], right["x"]) and
            min(left["y"] + left["height"], right["y"] + right["height"]) > max(left["y"], right["y"]))


def frame_mutation_source_evidence(frame: dict[str, Any]) -> list[dict[str, Any]]:
    sources = frame.get("mutationSources")
    if not isinstance(sources, list):
        return []
    dirty_rects = frame.get("dirtyRects") if isinstance(frame.get("dirtyRects"), list) else []
    normalized_dirty_rects = [normalize_trace_rect(rect) for rect in dirty_rects]
    normalized_dirty_rects = [rect for rect in normalized_dirty_rects if rect is not None]
    raw_commands = frame.get("commandSpans") if isinstance(frame.get("commandSpans"), list) else frame.get("commands", [])
    commands: list[dict[str, Any]] = []
    if isinstance(raw_commands, list):
        for item in raw_commands:
            if not isinstance(item, dict):
                continue
            owner = command_owner(item)
            duration = number(item.get("durationUs" if "durationUs" in item else "us"))
            if duration is None or duration < 0:
                continue
            commands.append({
                "owner": owner,
                "us": duration,
                "count": number(item.get("samples", 1)) or 1,
                "rect": normalize_trace_rect(item.get("rect")),
            })
    evidence: list[dict[str, Any]] = []
    for source in sources[:MAX_TRACE_MUTATION_SOURCES]:
        if not isinstance(source, dict):
            continue
        owner = source.get("owner") if isinstance(source.get("owner"), str) else "unattributed"
        dirty_indexes = {index for index in source.get("dirtyRectIndexes", []) if isinstance(index, int)}
        matching = [command for command in commands if command["owner"] == owner]
        command_us = sum(float(command["us"]) for command in matching)
        command_count = sum(float(command["count"]) for command in matching)
        dirty_commands = [
            command for command in matching
            if command["rect"] is not None and any(
                index in dirty_indexes and index < len(normalized_dirty_rects) and
                trace_rect_intersects(command["rect"], normalized_dirty_rects[index])
                for index in dirty_indexes
            )
        ]
        dirty_us = sum(float(command["us"]) for command in dirty_commands)
        dirty_hits = len(dirty_commands)
        evidence.append({
            "kind": source.get("kind", "unknown"),
            "owner": owner,
            "commandUs": round_number(command_us),
            "commandCount": round_number(command_count),
            "dirtyEvidenceUs": round_number(dirty_us),
            "dirtyEvidenceHits": dirty_hits,
            "evidence": "owner-command" if command_us > 0 else "owner-dirty" if dirty_us > 0 else "source-only",
        })
    return evidence


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(percent * len(ordered)) - 1))
    return ordered[index]


def round_number(value: float) -> int | float:
    rounded = round(value, 2)
    return int(rounded) if rounded.is_integer() else rounded


def command_owner(command: dict[str, Any]) -> str:
    raw = command.get("owner")
    if not isinstance(raw, str):
        legacy_node_id = command.get("nodeId")
        raw = f"id:{legacy_node_id}" if isinstance(legacy_node_id, str) else "unattributed"
    return raw if SAFE_TRACE_OWNER.fullmatch(raw) else "unattributed"


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
                "dirtyRectCount", "dirtyAreaPercent", "pipeline", "timingComplete",
                "commandsTruncated", "nodesTruncated", "commandInvalidSamples",
                "dirtyRectsTruncated", "stageSpansTruncated", "commandSpansTruncated"):
        if key in raw:
            result[key] = raw[key]
    if isinstance(raw.get("captureFile"), str) and raw["captureFile"].strip():
        result["captureFile"] = raw["captureFile"].strip()[:256]
    if isinstance(raw.get("dirtyRects"), list):
        result["dirtyRects"] = [
            rect for rect in (normalize_trace_rect(item) for item in raw["dirtyRects"][:MAX_TRACE_DIRTY_RECTS])
            if rect is not None
        ]
        if len(raw["dirtyRects"]) > MAX_TRACE_DIRTY_RECTS:
            result["dirtyRectsTruncated"] = True
    stage_spans, stage_spans_truncated = normalize_trace_spans(raw.get("stageSpans"), command=False)
    if isinstance(raw.get("stageSpans"), list):
        result["stageSpans"] = stage_spans
        if stage_spans_truncated:
            result["stageSpansTruncated"] = True
    command_spans, command_spans_truncated = normalize_trace_spans(raw.get("commandSpans"), command=True)
    if isinstance(raw.get("commandSpans"), list):
        result["commandSpans"] = command_spans
        if command_spans_truncated:
            result["commandSpansTruncated"] = True
    mutation_sources, mutation_sources_truncated = normalize_trace_mutation_sources(raw.get("mutationSources"))
    if isinstance(raw.get("mutationSources"), list):
        result["mutationSources"] = mutation_sources
        if mutation_sources_truncated:
            result["mutationSourcesTruncated"] = True
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
    average_pattern = re.compile(
        r"^(?P<name>[a-zA-Z0-9_]+)\s+iterations=(?P<iterations>\d+)\s+"
        r"avg_us=(?P<avg>[-+0-9.eE]+)\s*$"
    )
    stats_pattern = re.compile(
        r"^(?P<name>[a-zA-Z0-9_]+)\s+samples=(?P<samples>\d+)\s+"
        r"iterations_per_sample=(?P<iterations>\d+)\s+"
        r"p50_us=(?P<p50>[-+0-9.eE]+)\s+p95_us=(?P<p95>[-+0-9.eE]+)\s+"
        r"display_commands=(?P<commands>\d+)\s+"
        r"peak_surface_bytes=(?P<surface_bytes>\d+)\s*$"
    )
    results: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError as error:
        raise SystemExit(f"failed to read microbench output {path}: {error}") from error
    for line in lines:
        stripped = line.strip()
        match = average_pattern.match(stripped)
        if match:
            results.append({
                "name": match.group("name"),
                "iterations": int(match.group("iterations")),
                "avgUs": round_number(float(match.group("avg"))),
                "source": str(path),
            })
            continue
        match = stats_pattern.match(stripped)
        if match:
            results.append({
                "name": match.group("name"),
                "samples": int(match.group("samples")),
                "iterationsPerSample": int(match.group("iterations")),
                "p50Us": round_number(float(match.group("p50"))),
                "p95Us": round_number(float(match.group("p95"))),
                "displayCommands": int(match.group("commands")),
                "peakSurfaceBytes": int(match.group("surface_bytes")),
                "source": str(path),
            })
    return results


def compare_microbench(current: list[dict[str, Any]], baseline: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Compare same-shaped probes without treating different workloads as comparable."""
    baseline_by_key: dict[tuple[str, str], dict[str, Any]] = {}
    for probe in baseline:
        if "avgUs" in probe:
            kind = "average"
        elif "p50Us" in probe and "p95Us" in probe:
            kind = "stats"
        else:
            continue
        baseline_by_key[(str(probe.get("name", "")), kind)] = probe

    comparisons: list[dict[str, Any]] = []
    for probe in current:
        if "avgUs" in probe:
            kind = "average"
            metrics = (("avgUs", "average"),)
        elif "p50Us" in probe and "p95Us" in probe:
            kind = "stats"
            metrics = (("p50Us", "p50"), ("p95Us", "p95"))
        else:
            continue
        previous = baseline_by_key.get((str(probe.get("name", "")), kind))
        if previous is None:
            continue
        for field, metric in metrics:
            old = previous.get(field)
            new = probe.get(field)
            if not isinstance(old, (int, float)) or isinstance(old, bool) or old < 0:
                continue
            if not isinstance(new, (int, float)) or isinstance(new, bool) or new < 0:
                continue
            delta = None if old == 0 else round_number((new - old) * 100.0 / old)
            comparisons.append({
                "name": probe.get("name", ""),
                "kind": kind,
                "metric": metric,
                "baseline": round_number(float(old)),
                "current": round_number(float(new)),
                "deltaPercent": delta,
                "baselineSource": previous.get("source", ""),
                "currentSource": probe.get("source", ""),
            })
    return comparisons


def device_profile_records(lines: list[str], path: Path) -> dict[str, str] | None:
    """Return the sole complete Device Performance Profile V0 window, if any."""
    windows: dict[int, dict[str, dict[str, str]]] = {}
    for line_number, raw_line in enumerate(lines, 1):
        line = ANSI_ESCAPE_PATTERN.sub("", raw_line)
        match = DEVICE_PROFILE_RECORD_PATTERN.search(line)
        if not match:
            continue
        kind = match.group("kind")
        values: dict[str, str] = {}
        for token in match.group("body").split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            if key in values:
                raise SystemExit(f"{path}:{line_number}: duplicate telemetry key '{key}'")
            values[key] = value
        raw_window = values.get("window")
        if raw_window is None or not raw_window.isdecimal():
            raise SystemExit(
                f"{path}:{line_number}: {kind} requires window=<nonnegative integer>"
            )
        window = int(raw_window)
        records = windows.setdefault(window, {})
        if kind in records:
            raise SystemExit(f"{path}:{line_number}: duplicate {kind} record for device profile window {window}")
        records[kind] = values
    if not windows:
        return None
    if len(windows) != 1:
        found = ", ".join(str(window) for window in sorted(windows))
        raise SystemExit(
            f"{path}: Device Performance Profile V0 accepts exactly one complete window; found windows {found}"
        )
    window, records = next(iter(windows.items()))
    missing = sorted(DEVICE_PROFILE_RECORD_KINDS.difference(records))
    if missing:
        raise SystemExit(
            f"{path}: incomplete Device Performance Profile V0 window {window}; missing "
            + ", ".join(missing)
        )
    values: dict[str, str] = {}
    for kind in (
        "device_profile",
        "device_profile_timing",
        "device_profile_pipeline",
        "device_profile_present",
        "device_profile_counters",
    ):
        values.update(records[kind])
    return values


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
        "frame_us_p50": "frameP50Us", "frame_us_p95": "frameP95Us",
        "frame_us_max": "frameMaxUs", "paint_us_p50": "paintP50Us",
        "input_us_p50": "inputP50Us", "input_us_p95": "inputP95Us",
        "planning_us_p50": "planningP50Us", "planning_us_p95": "planningP95Us",
        "pipeline_us_p50": "pipelineP50Us", "pipeline_us_p95": "pipelineP95Us",
        "paint_us_p95": "paintP95Us", "present_us_p50": "presentP50Us",
        "present_us_p95": "presentP95Us", "convert_us_p50": "convertP50Us",
        "convert_us_p95": "convertP95Us", "dma_wait_us_p50": "dmaWaitP50Us",
        "dma_submit_us_p50": "dmaSubmitP50Us", "dma_submit_us_p95": "dmaSubmitP95Us",
        "dma_wait_us_p95": "dmaWaitP95Us", "dirty_pixels_avg": "dirtyPixelsAvg",
        "dirty_rects_avg_x100": "dirtyRectsAvgX100", "timing_complete": "timingComplete",
        "partial": "partial", "contaminated": "contaminated",
        "window_frames": "windowFrames", "warmup_frames": "warmupFrames",
        "present_frames": "presentFrames", "full_frames": "fullFrames",
        "dirty_frames": "dirtyFrames", "idle_frames": "idleFrames",
        "pipeline_frames": "pipelineFrames", "converted_pixels": "convertedPixels",
        "packed_bytes": "packedBytes", "present_failures": "presentFailures",
        "internal_free_min": "internalFreeMinBytes", "psram_free_min": "psramFreeMinBytes",
        "stack_free_words": "stackFreeWords",
    }
    profile_values = device_profile_records(lines, path)
    value_lines = [" ".join(f"{key}={value}" for key, value in profile_values.items())] if profile_values else lines
    for raw_line in value_lines:
        line = ANSI_ESCAPE_PATTERN.sub("", raw_line)
        for key, raw_value in re.findall(r"([a-zA-Z][a-zA-Z0-9_]*)=([^\s]+)", line):
            target = aliases.get(key)
            if target is None:
                continue
            try:
                parsed = number(float(raw_value))
            except ValueError:
                parsed = None
            if parsed is not None:
                values[target] = round_number(parsed)
    identity = {}
    if profile_values is not None:
        identity = {
            target: profile_values[source]
            for source, target in (
                ("case", "case"),
                ("profile", "profile"),
                ("board", "board"),
                ("viewport", "viewport"),
            )
            if profile_values.get(source)
        }
    return {
        "source": str(path),
        "format": "jellyframe.device.profile.v0"
        if profile_values is not None
        else "jellyframe.port.telemetry.metrics.v0",
        "identity": identity,
        "metrics": values,
    }


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
    command_owner_totals: dict[tuple[str, str], dict[str, float]] = {}
    command_owner_truncated = False
    mutation_source_totals: dict[tuple[str, str], dict[str, float]] = {}
    for frame in frames:
        for command in frame.get("commands", []):
            if not isinstance(command, dict):
                continue
            name = str(command.get("name", command.get("type", "unknown")))
            value = number(command.get("us", command.get("microseconds")))
            if value is not None:
                command_totals[name] = command_totals.get(name, 0.0) + value
                owner = command_owner(command)
                key = (owner, name)
                group = command_owner_totals.get(key)
                if group is None:
                    if len(command_owner_totals) >= MAX_COMMAND_OWNER_GROUPS:
                        command_owner_truncated = True
                        continue
                    group = {"us": 0.0, "pixels": 0.0, "samples": 0.0}
                    command_owner_totals[key] = group
                group["us"] += value
                pixels = number(command.get("pixels"))
                if pixels is not None:
                    group["pixels"] += pixels
                samples = number(command.get("samples"))
                if samples is not None:
                    group["samples"] += samples
        command_owner_truncated = command_owner_truncated or bool(frame.get("commandsTruncated"))
        for evidence in frame_mutation_source_evidence(frame):
            key = (str(evidence.get("kind", "unknown")), str(evidence.get("owner", "unattributed")))
            group = mutation_source_totals.setdefault(key, {
                "commandUs": 0.0, "commandCount": 0.0, "dirtyEvidenceUs": 0.0,
                "dirtyEvidenceHits": 0.0, "frames": 0.0,
            })
            group["commandUs"] += float(evidence.get("commandUs", 0))
            group["commandCount"] += float(evidence.get("commandCount", 0))
            group["dirtyEvidenceUs"] += float(evidence.get("dirtyEvidenceUs", 0))
            group["dirtyEvidenceHits"] += float(evidence.get("dirtyEvidenceHits", 0))
            group["frames"] += 1
    command_owner_rows = [
        {
            "owner": owner,
            "type": name,
            "us": round_number(values["us"]),
            "pixels": round_number(values["pixels"]),
            "samples": round_number(values["samples"]),
        }
        for (owner, name), values in sorted(
            command_owner_totals.items(), key=lambda entry: entry[1]["us"], reverse=True
        )
    ]
    mutation_source_rows = [
        {
            "kind": kind,
            "owner": owner,
            "commandUs": round_number(values["commandUs"]),
            "commandCount": round_number(values["commandCount"]),
            "dirtyEvidenceUs": round_number(values["dirtyEvidenceUs"]),
            "dirtyEvidenceHits": round_number(values["dirtyEvidenceHits"]),
            "frames": round_number(values["frames"]),
            "evidence": "owner-command" if values["commandUs"] > 0 else "owner-dirty" if values["dirtyEvidenceUs"] > 0 else "source-only",
        }
        for (kind, owner), values in sorted(
            mutation_source_totals.items(), key=lambda entry: entry[1]["commandUs"], reverse=True
        )
    ]
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
        "commandOwnerAttribution": command_owner_rows,
        "commandOwnerAttributionTruncated": command_owner_truncated,
        "mutationSourceEvidence": mutation_source_rows,
    }


def build_report(
    report_paths: list[Path],
    trace_paths: list[Path],
    telemetry_paths: list[Path],
    microbench_paths: list[Path],
    microbench_baseline_paths: list[Path] | None = None,
) -> dict[str, Any]:
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
    baseline = [
        entry for path in (microbench_baseline_paths or []) for entry in read_microbench(path)
    ]
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
        "microbenchComparisons": compare_microbench(microbench, baseline),
        "warnings": warnings,
        "limitations": limitations + ([
            "Microbench comparisons only match the same named probe and measurement shape; they do not compare different libraries or devices.",
        ] if baseline else []),
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
        observability = []
        if isinstance(frame.get("captureFile"), str):
            observability.append(f"capture={frame['captureFile']}")
        if isinstance(frame.get("stageSpans"), list):
            observability.append(f"stage spans={len(frame['stageSpans'])}")
        if isinstance(frame.get("commandSpans"), list):
            observability.append(f"command spans={len(frame['commandSpans'])}")
        if isinstance(frame.get("dirtyRects"), list):
            observability.append(f"dirty rects={len(frame['dirtyRects'])}")
        if isinstance(frame.get("mutationSources"), list):
            kinds = sorted({
                str(source.get("kind")) for source in frame["mutationSources"]
                if isinstance(source, dict) and isinstance(source.get("kind"), str)
            })
            observability.append("mutation sources=" + (",".join(kinds) if kinds else "none"))
        if frame.get("mutationSourcesTruncated"):
            observability.append("mutation sources truncated")
        frame_rows.append(
            f"<tr><td>{html.escape(str(frame.get('frame', '')))}</td><td>{html.escape(str(frame.get('totalUs', 0)))} us</td>"
            f"<td>{html.escape(str(frame.get('action', '')))}</td><td>{html.escape(str(frame.get('reason', '')))}</td>"
            f"<td>{html.escape(str(frame.get('dirtyRectCount', '')))}</td><td>{html.escape(str(frame.get('dirtyAreaPercent', '')))}</td>"
            f"<td>{html.escape('; '.join(observability) or '-')}</td></tr>"
        )
    command_rows = []
    for command in summary.get("commandOwnerAttribution", []):
        if not isinstance(command, dict):
            continue
        command_rows.append(
            f"<tr><td><code>{html.escape(str(command.get('owner', 'unattributed')))}</code></td>"
            f"<td><code>{html.escape(str(command.get('type', 'unknown')))}</code></td>"
            f"<td>{html.escape(str(command.get('us', 0)))} us</td>"
            f"<td>{html.escape(str(command.get('pixels', 0)))}</td>"
            f"<td>{html.escape(str(command.get('samples', 0)))}</td></tr>"
        )
    command_note = " Rows were truncated; ranking is incomplete." if summary.get("commandOwnerAttributionTruncated") else ""
    mutation_source_rows = []
    for source in summary.get("mutationSourceEvidence", []):
        if not isinstance(source, dict):
            continue
        mutation_source_rows.append(
            f"<tr><td><code>{html.escape(str(source.get('kind', 'unknown')))}</code></td>"
            f"<td><code>{html.escape(str(source.get('owner', 'unattributed')))}</code></td>"
            f"<td>{html.escape(str(source.get('commandUs', 0)))} us / {html.escape(str(source.get('commandCount', 0)))}</td>"
            f"<td>{html.escape(str(source.get('dirtyEvidenceUs', 0)))} us / {html.escape(str(source.get('dirtyEvidenceHits', 0)))}</td>"
            f"<td><code>{html.escape(str(source.get('evidence', 'source-only')))}</code></td>"
            f"<td>{html.escape(str(source.get('frames', 0)))}</td></tr>"
        )
    mutation_source_section = (
        "<h2>Mutation source evidence</h2>"
        "<p><small>Same-owner command and dirty-region correlation only; this is not proof of DOM mutation causality.</small></p>"
        "<table><tr><th>Kind</th><th>Owner</th><th>Linked commands</th><th>Linked dirty evidence</th><th>Evidence</th><th>Frames</th></tr>"
        + "".join(mutation_source_rows)
        + "</table>"
        if mutation_source_rows else ""
    )
    microbench_rows = []
    for probe in report.get("microbenchProbes", []):
        if not isinstance(probe, dict):
            continue
        if "avgUs" in probe:
            iterations = f"{probe.get('iterations', 0)} iterations"
            average = f"{probe.get('avgUs', 0)} us"
            p50 = p95 = "-"
            shape = "average"
        else:
            iterations = f"{probe.get('samples', 0)} x {probe.get('iterationsPerSample', 0)}"
            average = "-"
            p50 = f"{probe.get('p50Us', 0)} us"
            p95 = f"{probe.get('p95Us', 0)} us"
            shape = (
                f"commands={probe.get('displayCommands', 0)}, "
                f"surface={probe.get('peakSurfaceBytes', 0)} bytes"
            )
        microbench_rows.append(
            f"<tr><td><code>{html.escape(str(probe.get('name', 'unknown')))}</code></td>"
            f"<td>{html.escape(shape)}</td><td>{html.escape(iterations)}</td>"
            f"<td>{html.escape(average)}</td><td>{html.escape(p50)}</td>"
            f"<td>{html.escape(p95)}</td><td>{html.escape(str(probe.get('source', '')))}</td></tr>"
        )
    microbench_section = ""
    if report.get("microbenchProbes"):
        microbench_section = (
            "<h2>Render Core microbenchmarks</h2>"
            "<p><small>Isolated desktop probes; these values are not per-element application attribution "
            "and are not device FPS or DMA measurements.</small></p>"
            "<table><tr><th>Probe</th><th>Shape</th><th>Iterations</th><th>Average</th>"
            "<th>p50</th><th>p95</th><th>Source</th></tr>"
            + "".join(microbench_rows)
            + "</table>"
        )
    comparison_rows = []
    for comparison in report.get("microbenchComparisons", []):
        if not isinstance(comparison, dict):
            continue
        delta = comparison.get("deltaPercent")
        delta_text = "-" if delta is None else f"{float(delta):+.2f}%"
        comparison_rows.append(
            f"<tr><td><code>{html.escape(str(comparison.get('name', 'unknown')))}</code></td>"
            f"<td>{html.escape(str(comparison.get('metric', 'unknown')))}</td>"
            f"<td>{html.escape(str(comparison.get('baseline', 0)))} us</td>"
            f"<td>{html.escape(str(comparison.get('current', 0)))} us</td>"
            f"<td>{html.escape(delta_text)}</td></tr>"
        )
    comparison_section = ""
    if report.get("microbenchComparisons"):
        comparison_section = (
            "<h2>Microbench baseline comparison</h2>"
            "<p><small>Negative change means the current probe is faster. Only matching probe names and shapes are compared.</small></p>"
            "<table><tr><th>Probe</th><th>Metric</th><th>Baseline</th><th>Current</th><th>Change</th></tr>"
            + "".join(comparison_rows)
            + "</table>"
        )
    device_rows = []
    device_metric_columns = (
        ((("frameP95Us", "us"), ("p95FrameMs", "ms")), "Frame p95"),
        ((("paintP95Us", "us"), ("paintP95Ms", "ms")), "Paint p95"),
        ((("presentP95Us", "us"), ("p95PresentMs", "ms")), "Present p95"),
        ((("dmaWaitP95Us", "us"), ("averageDmaWaitMs", "ms")), "DMA wait"),
        ((("pipelineFrames", ""),), "Pipeline frames"),
        ((("partial", ""),), "Partial"),
        ((("contaminated", ""),), "Contaminated"),
    )
    for telemetry in report.get("deviceTelemetry", []):
        if not isinstance(telemetry, dict):
            continue
        metrics = telemetry.get("metrics", {})
        if not isinstance(metrics, dict):
            metrics = {}
        identity = telemetry.get("identity", {})
        if not isinstance(identity, dict):
            identity = {}
        identity_text = " · ".join(
            str(identity[key]) for key in ("case", "profile", "board", "viewport")
            if identity.get(key)
        ) or "-"
        metric_cells = []
        for alternatives, label in device_metric_columns:
            rendered = "-"
            for key, suffix in alternatives:
                if key in metrics:
                    rendered = html.escape(str(metrics[key])) + html.escape(suffix)
                    break
            metric_cells.append(f"<td title='{html.escape(label)}'>{rendered}</td>")
        device_rows.append(
            f"<tr><td><code>{html.escape(str(telemetry.get('format', 'unknown')))}</code></td>"
            f"<td>{html.escape(identity_text)}</td>"
            f"<td>{html.escape(str(telemetry.get('source', '')))}</td>{''.join(metric_cells)}</tr>"
        )
    device_section = ""
    if report.get("deviceTelemetry"):
        device_section = (
            "<h2>Device aggregate telemetry</h2>"
            "<p><small>Device windows are reported separately from desktop frames; p95 values are not combined or converted into FPS claims.</small></p>"
            "<table><tr><th>Format</th><th>Identity</th><th>Source</th>"
            + "".join(f"<th>{html.escape(label)}</th>" for _, label in device_metric_columns)
            + "</tr>"
            + "".join(device_rows)
            + "</table>"
        )
    return """<!doctype html>
<meta charset="utf-8"><title>JellyFrame Render Performance</title>
<style>body{{font:14px system-ui,sans-serif;max-width:1100px;margin:28px auto;color:#202124}}table{{border-collapse:collapse;width:100%;margin:12px 0 28px}}th,td{{border-bottom:1px solid #ddd;text-align:left;padding:7px}}meter{{width:180px;height:12px}}code{{font-family:ui-monospace,monospace}}small{{color:#5f6368}}</style>
<h1>JellyFrame Render Performance</h1>
<p><small>Source-aware report. Desktop timings, device aggregate telemetry and isolated microbenchmarks are not combined.</small></p>
<h2>Frame summary</h2>
<p>Frames: {frames} · average: {average} us · p50: {p50} us · p95: {p95} us · max: {maximum} us</p>
<h2>Stage share</h2><table><tr><th>Stage</th><th>Total</th><th>Share</th></tr>{stage_rows}</table>
<h2>Frames</h2><table><tr><th>Frame</th><th>Total</th><th>Action</th><th>Reason</th><th>Dirty rects</th><th>Dirty area %</th><th>Trace observability</th></tr>{frame_rows}</table>
<h2>Command / owner attribution</h2><p><small>Desktop raster invocation time only.{command_note}</small></p>
<table><tr><th>Owner</th><th>Command</th><th>Time</th><th>Candidate pixels</th><th>Samples</th></tr>{command_rows}</table>
{mutation_source_section}
{microbench_section}
{comparison_section}
{device_section}
<h2>Limits</h2><ul>{limits}</ul>
""".format(
        frames=html.escape(str(summary.get("frameCount", 0))),
        average=html.escape(str(summary.get("totalUs", {}).get("average", 0))),
        p50=html.escape(str(summary.get("totalUs", {}).get("p50", 0))),
        p95=html.escape(str(summary.get("totalUs", {}).get("p95", 0))),
        maximum=html.escape(str(summary.get("totalUs", {}).get("max", 0))),
        stage_rows="".join(rows) or "<tr><td colspan='3'>No stage timings</td></tr>",
        frame_rows="".join(frame_rows) or "<tr><td colspan='7'>No per-frame trace</td></tr>",
        command_rows="".join(command_rows) or "<tr><td colspan='5'>No command attribution</td></tr>",
        command_note=command_note,
        mutation_source_section=mutation_source_section,
        microbench_section=microbench_section,
        comparison_section=comparison_section,
        device_section=device_section,
        limits="".join(f"<li>{html.escape(item)}</li>" for item in report.get("limitations", [])),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="append", type=Path, default=[], help="Package/pipeline JSON report; may be repeated.")
    parser.add_argument("--trace", action="append", type=Path, default=[], help=f"Versioned JSONL frame trace ({TRACE_FORMAT}); may be repeated.")
    parser.add_argument("--device-telemetry", action="append", type=Path, default=[], help="Device aggregate telemetry log; may be repeated.")
    parser.add_argument("--microbench", action="append", type=Path, default=[], help="Render Core microbench stdout; may be repeated.")
    parser.add_argument("--microbench-baseline", action="append", type=Path, default=[], help="Baseline Render Core microbench stdout for same-shape comparison; may be repeated.")
    parser.add_argument("--output", type=Path, required=True, help="Output JSON report.")
    parser.add_argument("--html-output", type=Path, help="Optional self-contained HTML summary.")
    args = parser.parse_args()
    result = build_report(args.report, args.trace, args.device_telemetry, args.microbench, args.microbench_baseline)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.html_output:
        args.html_output.parent.mkdir(parents=True, exist_ok=True)
        args.html_output.write_text(render_html(result), encoding="utf-8")
    print(json.dumps({"format": REPORT_FORMAT, "frames": result["summary"]["frameCount"], "output": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
