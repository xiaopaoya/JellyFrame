#!/usr/bin/env python3
"""Compare two compatible CPU/embedded benchmark run manifests.

The tool compares measured runs, not libraries by name. It rejects mismatched
workloads, viewport, pixel format, antialiasing and repaint mode so a result
cannot silently become a cross-library claim for different work.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import statistics
from pathlib import Path
from typing import Any


FORMAT = "jellyframe.benchmark.run.v0"
COMPARISON_FORMAT = "jellyframe.benchmark.comparison.v0"
COMPARISON_FIELDS = (
    "workload",
    "viewport",
    "pixelFormat",
    "antialiasing",
    "mode",
    "environment",
    "warmupIterations",
)
METRIC_UNITS = {
    "frame_us": "us",
    "cpu_us": "us",
    "paint_us": "us",
    "present_us": "us",
    "dma_wait_us": "us",
    "pixels": "pixels",
    "dirty_pixels": "pixels",
    "peak_bytes": "bytes",
}
MAX_WORKLOAD_PARAMETERS_BYTES = 16 * 1024
MAX_WORKLOAD_PARAMETERS_DEPTH = 6


def validate_workload_parameter(value: Any, path: str, depth: int = 0) -> None:
    if depth > MAX_WORKLOAD_PARAMETERS_DEPTH:
        raise ValueError(f"{path} exceeds maximum nesting depth")
    if value is None or isinstance(value, (str, bool)):
        return
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if not math.isfinite(float(value)):
            raise ValueError(f"{path} contains a non-finite number")
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            validate_workload_parameter(item, f"{path}[{index}]", depth + 1)
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if not isinstance(key, str) or not key:
                raise ValueError(f"{path} keys must be non-empty strings")
            validate_workload_parameter(item, f"{path}.{key}", depth + 1)
        return
    raise ValueError(f"{path} contains an unsupported value")


def load_run(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"failed to read benchmark run {path}: {error}") from error
    if not isinstance(value, dict):
        raise SystemExit(f"benchmark run root must be an object: {path}")
    if value.get("format") != FORMAT:
        raise SystemExit(f"unsupported benchmark run format in {path}: {value.get('format')!r}")
    if not isinstance(value.get("library"), str) or not value["library"].strip():
        raise SystemExit(f"benchmark run library is required: {path}")
    for field in COMPARISON_FIELDS:
        if field not in value:
            raise SystemExit(f"benchmark run field {field!r} is required: {path}")
    if not isinstance(value.get("workload"), str) or not value["workload"].strip():
        raise SystemExit(f"benchmark run workload is required: {path}")
    if not isinstance(value.get("pixelFormat"), str) or not value["pixelFormat"].strip():
        raise SystemExit(f"benchmark run pixelFormat is required: {path}")
    if not isinstance(value.get("antialiasing"), bool):
        raise SystemExit(f"benchmark run antialiasing must be boolean: {path}")
    if value.get("mode") not in ("full", "dirty"):
        raise SystemExit(f"benchmark run mode must be 'full' or 'dirty': {path}")
    measurements = value.get("measurements")
    if not isinstance(measurements, dict) or not measurements:
        raise SystemExit(f"benchmark run measurements must be a non-empty object: {path}")
    normalized: dict[str, list[float]] = {}
    for name, raw_values in measurements.items():
        if not isinstance(name, str) or not name.strip() or not isinstance(raw_values, list) or not raw_values:
            raise SystemExit(f"benchmark metric {name!r} must contain a non-empty array: {path}")
        if name not in METRIC_UNITS:
            raise SystemExit(f"benchmark metric {name!r} has no standardized unit: {path}")
        values: list[float] = []
        for raw_value in raw_values:
            if isinstance(raw_value, bool) or not isinstance(raw_value, (int, float)) or not math.isfinite(float(raw_value)) or float(raw_value) < 0:
                raise SystemExit(f"benchmark metric {name!r} contains an invalid value: {path}")
            values.append(float(raw_value))
        normalized[name] = values
    result = dict(value)
    result["measurements"] = normalized
    viewport = value.get("viewport")
    if not isinstance(viewport, dict) or any(
        isinstance(viewport.get(field), bool) or not isinstance(viewport.get(field), int) or viewport[field] <= 0
        for field in ("width", "height")
    ):
        raise SystemExit(f"benchmark run viewport requires positive integer width/height: {path}")
    if not isinstance(value.get("environment"), dict) or not value["environment"]:
        raise SystemExit(f"benchmark run environment must be a non-empty object: {path}")
    if isinstance(value.get("warmupIterations"), bool) or not isinstance(value.get("warmupIterations"), int) or value["warmupIterations"] < 0:
        raise SystemExit(f"benchmark run warmupIterations must be a non-negative integer: {path}")
    operations_per_sample = value.get("operationsPerSample", 1)
    if isinstance(operations_per_sample, bool) or not isinstance(operations_per_sample, int) or operations_per_sample <= 0:
        raise SystemExit(f"benchmark run operationsPerSample must be a positive integer: {path}")
    result["operationsPerSample"] = operations_per_sample
    if "workloadParameters" in value:
        workload_parameters = value["workloadParameters"]
        if not isinstance(workload_parameters, dict) or not workload_parameters:
            raise SystemExit(f"benchmark run workloadParameters must be a non-empty object: {path}")
        try:
            validate_workload_parameter(workload_parameters, "workloadParameters")
        except ValueError as error:
            raise SystemExit(f"benchmark run {error}: {path}") from error
        encoded_parameters = json.dumps(workload_parameters, ensure_ascii=False, separators=(",", ":"))
        if len(encoded_parameters.encode("utf-8")) > MAX_WORKLOAD_PARAMETERS_BYTES:
            raise SystemExit(f"benchmark run workloadParameters exceeds byte limit: {path}")
    validation = value.get("outputValidation")
    if not isinstance(validation, dict) or validation.get("status") not in ("pass", "fail") or not isinstance(validation.get("method"), str) or not validation["method"].strip() or not isinstance(validation.get("reference"), str) or not validation["reference"].strip():
        raise SystemExit(f"benchmark run outputValidation requires status pass/fail, method and reference: {path}")
    tolerance = validation.get("tolerance", 0)
    if isinstance(tolerance, bool) or not isinstance(tolerance, (int, float)) or not math.isfinite(float(tolerance)) or tolerance < 0:
        raise SystemExit(f"benchmark run outputValidation tolerance must be non-negative: {path}")
    return result


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(len(ordered) * fraction) - 1))
    return ordered[index]


def summarize(values: list[float]) -> dict[str, float | int]:
    return {
        "count": len(values),
        "average": round(statistics.fmean(values), 2),
        "p50": round(percentile(values, 0.50), 2),
        "p95": round(percentile(values, 0.95), 2),
        "max": round(max(values), 2),
    }


def comparable_fields(baseline: dict[str, Any], candidate: dict[str, Any]) -> list[str]:
    mismatches = [
        field for field in COMPARISON_FIELDS
        if baseline.get(field) != candidate.get(field)
    ]
    baseline_validation = baseline["outputValidation"]
    candidate_validation = candidate["outputValidation"]
    if baseline_validation.get("method") != candidate_validation.get("method"):
        mismatches.append("outputValidation.method")
    if baseline_validation.get("reference") != candidate_validation.get("reference"):
        mismatches.append("outputValidation.reference")
    if baseline_validation.get("tolerance", 0) != candidate_validation.get("tolerance", 0):
        mismatches.append("outputValidation.tolerance")
    if baseline_validation.get("status") != "pass" or candidate_validation.get("status") != "pass":
        mismatches.append("outputValidation.status")
    if baseline.get("operationsPerSample", 1) != candidate.get("operationsPerSample", 1):
        mismatches.append("operationsPerSample")
    if baseline.get("workloadParameters") != candidate.get("workloadParameters"):
        mismatches.append("workloadParameters")
    return mismatches


def compare_runs(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    mismatches = comparable_fields(baseline, candidate)
    baseline_metrics = baseline["measurements"]
    candidate_metrics = candidate["measurements"]
    metric_names = sorted(set(baseline_metrics) | set(candidate_metrics))
    metrics: list[dict[str, Any]] = []
    for name in metric_names:
        old_values = baseline_metrics.get(name)
        new_values = candidate_metrics.get(name)
        metric: dict[str, Any] = {"name": name, "unit": METRIC_UNITS.get(name, "value")}
        if old_values is None or new_values is None:
            metric["status"] = "not-comparable"
            metric["reason"] = "metric-missing-on-one-side"
        elif mismatches:
            metric["status"] = "not-comparable"
            metric["reason"] = "fixed-condition-mismatch"
        elif len(old_values) != len(new_values):
            metric["status"] = "not-comparable"
            metric["reason"] = "sample-count-mismatch"
        else:
            old = summarize(old_values)
            new = summarize(new_values)
            metric["status"] = "comparable"
            metric["baseline"] = old
            metric["candidate"] = new
            metric["deltaPercent"] = None if old["p95"] == 0 else round((new["p95"] - old["p95"]) * 100.0 / old["p95"], 2)
        metrics.append(metric)
    if not mismatches and len(baseline_metrics.get("pixels", ())) == len(candidate_metrics.get("pixels", ())) and "pixels" in baseline_metrics and "pixels" in candidate_metrics:
        for duration_name in ("frame_us", "cpu_us", "paint_us"):
            if duration_name not in baseline_metrics or duration_name not in candidate_metrics:
                continue
            if len(baseline_metrics["pixels"]) != len(baseline_metrics[duration_name]) or len(candidate_metrics["pixels"]) != len(candidate_metrics[duration_name]):
                continue
            if any(duration <= 0 for duration in baseline_metrics[duration_name] + candidate_metrics[duration_name]):
                continue
            baseline_rate = [pixels / duration for pixels, duration in zip(baseline_metrics["pixels"], baseline_metrics[duration_name])]
            candidate_rate = [pixels / duration for pixels, duration in zip(candidate_metrics["pixels"], candidate_metrics[duration_name])]
            old_rate = summarize(baseline_rate)
            new_rate = summarize(candidate_rate)
            metrics.append({
                "name": f"mpix_per_s_using_{duration_name}",
                "unit": "MPix/s",
                "status": "comparable",
                "baseline": old_rate,
                "candidate": new_rate,
                "deltaPercent": None if old_rate["p95"] == 0 else round((new_rate["p95"] - old_rate["p95"]) * 100.0 / old_rate["p95"], 2),
            })
            break
    comparable_count = sum(metric["status"] == "comparable" for metric in metrics)
    if mismatches or comparable_count == 0:
        status = "not-comparable"
    elif comparable_count < len(metrics):
        status = "partially-comparable"
    else:
        status = "comparable"
    return {
        "format": COMPARISON_FORMAT,
        "status": status,
        "baseline": {"library": baseline["library"], "version": baseline.get("version")},
        "candidate": {"library": candidate["library"], "version": candidate.get("version")},
        "fixedConditions": {
            "workload": baseline.get("workload"),
            "viewport": baseline.get("viewport"),
            "pixelFormat": baseline.get("pixelFormat"),
            "antialiasing": baseline.get("antialiasing"),
            "mode": baseline.get("mode"),
            "warmupIterations": baseline.get("warmupIterations"),
            "operationsPerSample": baseline.get("operationsPerSample", 1),
            "workloadParameters": baseline.get("workloadParameters"),
        },
        "candidateFixedConditions": {
            "workload": candidate.get("workload"),
            "viewport": candidate.get("viewport"),
            "pixelFormat": candidate.get("pixelFormat"),
            "antialiasing": candidate.get("antialiasing"),
            "mode": candidate.get("mode"),
            "warmupIterations": candidate.get("warmupIterations"),
            "operationsPerSample": candidate.get("operationsPerSample", 1),
            "workloadParameters": candidate.get("workloadParameters"),
        },
        "fixedConditionMismatches": mismatches,
        "metrics": metrics,
        "limitations": [
            "This comparison is valid only for the declared fixed conditions.",
            "It does not convert desktop timings into device FPS or DMA timing.",
            "A missing metric or fixed-condition mismatch is not a performance win or loss.",
        ],
    }


def render_html(comparison: dict[str, Any]) -> str:
    rows = []
    for metric in comparison["metrics"]:
        baseline = metric.get("baseline", {})
        candidate = metric.get("candidate", {})
        rows.append(
            "<tr><td><code>{}</code></td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>".format(
                html.escape(str(metric["name"])),
                html.escape(str(metric["unit"])),
                html.escape(str(baseline.get("p95", "-"))),
                html.escape(str(candidate.get("p95", "-"))),
                html.escape(str(metric.get("deltaPercent", "-"))),
                html.escape(str(metric["status"])),
            )
        )
    mismatch = "" if not comparison["fixedConditionMismatches"] else (
        "<p class='error'>Fixed-condition mismatch: " +
        html.escape(", ".join(comparison["fixedConditionMismatches"])) + "</p>"
    )
    fixed_conditions = html.escape(json.dumps(
        comparison.get("fixedConditions", {}), ensure_ascii=False, indent=2
    ))
    candidate_fixed_conditions = html.escape(json.dumps(
        comparison.get("candidateFixedConditions", {}), ensure_ascii=False, indent=2
    ))
    return """<!doctype html><meta charset='utf-8'><title>Benchmark comparison</title>
<style>body{{font-family:system-ui,sans-serif;margin:24px}}table{{border-collapse:collapse}}th,td{{border:1px solid #bbb;padding:6px;text-align:left}}.error{{color:#a00}}</style>
<h1>Benchmark comparison: {status}</h1>{mismatch}
<p>{baseline} -> {candidate}</p>
<details open><summary>Baseline fixed conditions</summary><pre>{fixed_conditions}</pre></details>
<details><summary>Candidate fixed conditions</summary><pre>{candidate_fixed_conditions}</pre></details>
<table><tr><th>Metric</th><th>Unit</th><th>Baseline p95</th><th>Candidate p95</th><th>Delta %</th><th>Status</th></tr>{rows}</table>
<h2>Limitations</h2><ul>{limitations}</ul>
""".format(
        status=html.escape(str(comparison["status"])),
        baseline=html.escape(str(comparison["baseline"]["library"])),
        candidate=html.escape(str(comparison["candidate"]["library"])),
        mismatch=mismatch,
        rows="".join(rows),
        fixed_conditions=fixed_conditions,
        candidate_fixed_conditions=candidate_fixed_conditions,
        limitations="".join(f"<li>{html.escape(item)}</li>" for item in comparison["limitations"]),
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--html-output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    comparison = compare_runs(load_run(args.baseline), load_run(args.candidate))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(comparison, indent=2) + "\n", encoding="utf-8")
    if args.html_output:
        args.html_output.parent.mkdir(parents=True, exist_ok=True)
        args.html_output.write_text(render_html(comparison), encoding="utf-8")
    print(json.dumps(comparison, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
