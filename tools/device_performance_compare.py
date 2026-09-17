#!/usr/bin/env python3
"""Compare repeated Device Performance Profile V0 runs.

This is deliberately separate from benchmark_compare.py. A device profile
report contains aggregate window percentiles, so the tool summarizes the
percentiles across repeats instead of treating them as raw frame samples.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import statistics
from pathlib import Path
from typing import Any


FORMAT = "jellyframe.device.performance.run.v0"
COMPARISON_FORMAT = "jellyframe.device.performance.comparison.v0"
PROFILE_FORMAT = "jellyframe.device.profile.v0"
MIN_REPEATS = 3
VISUAL_STATUSES = {"exact-readback", "visual-equivalent-only", "missing", "fail"}
ACCEPTANCE_MODES = {"target-improvement", "non-regression"}
METRIC_UNITS = {
    "frameP50Us": "us",
    "frameP95Us": "us",
    "frameMaxUs": "us",
    "paintP50Us": "us",
    "paintP95Us": "us",
    "presentP50Us": "us",
    "presentP95Us": "us",
    "convertP50Us": "us",
    "convertP95Us": "us",
    "dmaWaitP50Us": "us",
    "dmaWaitP95Us": "us",
    "internalFreeMinBytes": "bytes",
    "psramFreeMinBytes": "bytes",
    "stackFreeWords": "words",
    "dirtyPixelsAvg": "pixels",
    "packedBytes": "bytes",
    "presentFailures": "count",
}
REQUIRED_METRICS = ("frameP95Us", "presentP95Us", "presentFailures")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise SystemExit(f"JSON root must be an object: {path}")
    return value


def finite_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if not math.isfinite(float(value)) or float(value) < 0:
        return None
    return float(value)


def rounded(value: float) -> int | float:
    result = round(value, 2)
    return int(result) if result.is_integer() else result


def percentile_summary(values: list[float]) -> dict[str, int | float]:
    ordered = sorted(values)
    return {
        "median": rounded(statistics.median(ordered)),
        "min": rounded(ordered[0]),
        "max": rounded(ordered[-1]),
    }


def load_manifest(path: Path) -> dict[str, Any]:
    value = read_json(path)
    if value.get("format") != FORMAT:
        raise SystemExit(f"unsupported device performance manifest format in {path}")
    if not isinstance(value.get("workload"), str) or not value["workload"].strip():
        raise SystemExit(f"device performance workload is required: {path}")
    if not isinstance(value.get("conditions"), dict) or not value["conditions"]:
        raise SystemExit(f"device performance conditions must be a non-empty object: {path}")
    if not isinstance(value.get("identity"), dict) or not value["identity"]:
        raise SystemExit(f"device performance identity must be a non-empty object: {path}")
    reports = value.get("reports")
    if not isinstance(reports, list) or len(reports) < MIN_REPEATS:
        raise SystemExit(f"device performance reports must contain at least {MIN_REPEATS} repeats: {path}")
    if any(not isinstance(report, str) or not report.strip() for report in reports):
        raise SystemExit(f"device performance reports must contain paths: {path}")
    evidence = value.get("visualEvidence", {})
    if not isinstance(evidence, dict) or evidence.get("status", "missing") not in VISUAL_STATUSES:
        raise SystemExit(f"visualEvidence.status must be one of {sorted(VISUAL_STATUSES)}: {path}")
    stability = value.get("stability")
    if not isinstance(stability, dict) or stability.get("status") not in ("pass", "fail"):
        raise SystemExit(f"stability.status must be 'pass' or 'fail': {path}")
    return value


def load_sample(path: Path) -> dict[str, Any]:
    report = read_json(path)
    telemetry = report.get("deviceTelemetry")
    if not isinstance(telemetry, list) or len(telemetry) != 1:
        raise SystemExit(f"{path}: expected exactly one complete device profile telemetry record")
    entry = telemetry[0]
    if not isinstance(entry, dict) or entry.get("format") != PROFILE_FORMAT:
        raise SystemExit(f"{path}: expected {PROFILE_FORMAT}")
    metrics = entry.get("metrics")
    if not isinstance(metrics, dict):
        raise SystemExit(f"{path}: device profile metrics are required")
    for name in REQUIRED_METRICS:
        if finite_number(metrics.get(name)) is None:
            raise SystemExit(f"{path}: device profile metric {name} is missing or invalid")
    for name in ("windowFrames", "warmupFrames", "frames", "presentFrames", "partial", "contaminated"):
        if finite_number(metrics.get(name)) is None:
            raise SystemExit(f"{path}: device profile metric {name} is missing or invalid")
    if metrics["frames"] <= 0 or metrics["presentFrames"] <= 0:
        raise SystemExit(f"{path}: device profile must contain active frames and presents")
    if metrics["partial"] != 0 or metrics["contaminated"] != 0:
        raise SystemExit(f"{path}: partial or contaminated device profile window")
    normalized = {
        name: finite_number(metrics[name])
        for name in METRIC_UNITS
        if finite_number(metrics.get(name)) is not None
    }
    return {"path": str(path), "metrics": normalized}


def fixed_condition_mismatches(baseline: dict[str, Any], candidate: dict[str, Any]) -> list[str]:
    mismatches: list[str] = []
    if baseline.get("workload") != candidate.get("workload"):
        mismatches.append("workload")
    if baseline.get("conditions") != candidate.get("conditions"):
        mismatches.append("conditions")
    baseline_reports = len(baseline.get("reports", []))
    candidate_reports = len(candidate.get("reports", []))
    if baseline_reports != candidate_reports:
        mismatches.append("repeatCount")
    return mismatches


def load_side(manifest_path: Path) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    samples = [load_sample((manifest_path.parent / report).resolve()) for report in manifest["reports"]]
    metric_names = sorted(set().union(*(sample["metrics"] for sample in samples)))
    summary = {
        name: percentile_summary([sample["metrics"][name] for sample in samples])
        for name in metric_names
        if all(name in sample["metrics"] for sample in samples)
    }
    return {"manifest": manifest, "samples": samples, "summary": summary}


def delta_percent(candidate: float, baseline: float) -> float | None:
    if baseline == 0:
        return None
    return rounded((candidate - baseline) * 100.0 / baseline)


def compare_sides(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    base_manifest = baseline["manifest"]
    candidate_manifest = candidate["manifest"]
    mismatches = fixed_condition_mismatches(base_manifest, candidate_manifest)
    names = sorted(set(baseline["summary"]) | set(candidate["summary"]))
    metrics: list[dict[str, Any]] = []
    for name in names:
        old = baseline["summary"].get(name)
        new = candidate["summary"].get(name)
        row: dict[str, Any] = {"name": name, "unit": METRIC_UNITS.get(name, "value")}
        if old is None or new is None:
            row.update({"status": "not-comparable", "reason": "metric-missing-on-one-side"})
        elif mismatches:
            row.update({"status": "not-comparable", "reason": "fixed-condition-mismatch"})
        else:
            row.update({
                "status": "comparable",
                "baseline": old,
                "candidate": new,
                "deltaPercent": delta_percent(float(new["median"]), float(old["median"])),
            })
        metrics.append(row)

    acceptance = base_manifest.get("acceptance", {})
    if not isinstance(acceptance, dict):
        acceptance = {}
    mode = acceptance.get("mode", "target-improvement")
    if mode not in ACCEPTANCE_MODES:
        mode = "target-improvement"
    target = acceptance.get("targetMetric", "frameP95Us")
    minimum_improvement = float(acceptance.get("minimumImprovementPercent", 5))
    max_frame_regression = float(acceptance.get("maxFrameRegressionPercent", 3))
    max_memory_regression = float(acceptance.get("maxMemoryRegressionPercent", 5))
    checks: list[dict[str, Any]] = []
    target_row = next((row for row in metrics if row["name"] == target), None)
    if target_row and target_row.get("status") == "comparable":
        target_delta = float(target_row["deltaPercent"]) if target_row["deltaPercent"] is not None else None
        check = {
            "name": "targetImprovement" if mode == "target-improvement" else "targetNonRegression",
            "metric": target,
            "actualDeltaPercent": target_delta,
            "pass": target_delta is not None and target_delta <= (-minimum_improvement if mode == "target-improvement" else max_frame_regression),
        }
        if mode == "target-improvement":
            check["requiredImprovementPercent"] = rounded(minimum_improvement)
        else:
            check["maximumDeltaPercent"] = rounded(max_frame_regression)
        checks.append(check)
    else:
        checks.append({
            "name": "targetImprovement" if mode == "target-improvement" else "targetNonRegression",
            "metric": target,
            "pass": False,
            "reason": "target-metric-unavailable",
        })

    frame_row = next((row for row in metrics if row["name"] == "frameP95Us"), None)
    if frame_row and frame_row.get("status") == "comparable":
        frame_delta = frame_row["deltaPercent"]
        checks.append({
            "name": "frameRegression",
            "metric": "frameP95Us",
            "maximumDeltaPercent": rounded(max_frame_regression),
            "actualDeltaPercent": frame_delta,
            "pass": frame_delta is not None and frame_delta <= max_frame_regression,
        })
    else:
        checks.append({"name": "frameRegression", "metric": "frameP95Us", "pass": False, "reason": "frame-metric-unavailable"})

    base_memory = baseline["summary"].get("internalFreeMinBytes")
    candidate_memory = candidate["summary"].get("internalFreeMinBytes")
    if base_memory and candidate_memory:
        memory_delta = delta_percent(float(candidate_memory["median"]), float(base_memory["median"]))
        checks.append({
            "name": "internalMemoryLowWater",
            "metric": "internalFreeMinBytes",
            "minimumDeltaPercent": rounded(-max_memory_regression),
            "actualDeltaPercent": memory_delta,
            "pass": memory_delta is not None and memory_delta >= -max_memory_regression,
        })
    else:
        checks.append({"name": "internalMemoryLowWater", "metric": "internalFreeMinBytes", "pass": False, "reason": "memory-metric-unavailable"})

    reasons: list[str] = []
    if mismatches:
        reasons.append("baseline and candidate fixed conditions do not match")
    if any(float(sample["metrics"].get("presentFailures", 0)) != 0 for side in (baseline, candidate) for sample in side["samples"]):
        reasons.append("presentFailures is non-zero")
    if base_manifest["stability"]["status"] != "pass" or candidate_manifest["stability"]["status"] != "pass":
        reasons.append("stability evidence is not passing")
    visual_statuses = {
        base_manifest.get("visualEvidence", {}).get("status", "missing"),
        candidate_manifest.get("visualEvidence", {}).get("status", "missing"),
    }
    if "fail" in visual_statuses:
        reasons.append("visual evidence failed")
    elif "missing" in visual_statuses:
        reasons.append("visual evidence is missing")
    if not all(check.get("pass") for check in checks):
        reasons.append("one or more acceptance checks failed")

    if mismatches:
        status = "INVALID"
    elif any(float(sample["metrics"].get("presentFailures", 0)) != 0 for side in (baseline, candidate) for sample in side["samples"]):
        status = "FAIL"
    elif base_manifest["stability"]["status"] != "pass" or candidate_manifest["stability"]["status"] != "pass":
        status = "FAIL"
    elif "fail" in visual_statuses:
        status = "FAIL"
    elif not all(check.get("pass") for check in checks):
        status = "FAIL"
    elif "missing" in visual_statuses:
        status = "PARTIAL"
    else:
        status = "PASS"

    return {
        "format": COMPARISON_FORMAT,
        "status": status,
        "statusReasons": reasons,
        "workload": base_manifest["workload"],
        "fixedConditionMismatches": mismatches,
        "baseline": {
            "identity": base_manifest["identity"],
            "repeatCount": len(baseline["samples"]),
            "visualEvidence": base_manifest.get("visualEvidence", {}),
            "summary": baseline["summary"],
        },
        "candidate": {
            "identity": candidate_manifest["identity"],
            "repeatCount": len(candidate["samples"]),
            "visualEvidence": candidate_manifest.get("visualEvidence", {}),
            "summary": candidate["summary"],
        },
        "acceptance": {
            "mode": mode,
            "targetMetric": target,
            "minimumImprovementPercent": minimum_improvement,
            "maxFrameRegressionPercent": max_frame_regression,
            "maxMemoryRegressionPercent": max_memory_regression,
            "checks": checks,
        },
        "metrics": metrics,
        "limitations": [
            "Device profile inputs are aggregate window percentiles; repeat summaries do not reconstruct raw frame distributions.",
            "visual-equivalent-only evidence can pass the behavioral gate but is not pixel equivalence.",
            "This comparison does not attribute time to individual DOM elements or commands.",
        ],
    }


def render_html(comparison: dict[str, Any]) -> str:
    rows = []
    for metric in comparison["metrics"]:
        old = metric.get("baseline", {}).get("median", "-")
        new = metric.get("candidate", {}).get("median", "-")
        delta = metric.get("deltaPercent", "-")
        rows.append(
            "<tr><td><code>{}</code></td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>".format(
                html.escape(str(metric["name"])), html.escape(str(metric["unit"])),
                html.escape(str(old)), html.escape(str(new)), html.escape(str(delta)),
                html.escape(str(metric["status"])),
            )
        )
    reasons = "".join(f"<li>{html.escape(reason)}</li>" for reason in comparison["statusReasons"])
    return """<!doctype html><meta charset='utf-8'><title>Device performance comparison</title>
<style>body{{font-family:system-ui,sans-serif;margin:24px}}table{{border-collapse:collapse}}th,td{{border:1px solid #bbb;padding:6px;text-align:left}}.status{{font-weight:700}}</style>
<h1>Device performance comparison: <span class='status'>{status}</span></h1>
<p>Workload: <code>{workload}</code></p><ul>{reasons}</ul>
<table><tr><th>Metric</th><th>Unit</th><th>Baseline median</th><th>Candidate median</th><th>Delta %</th><th>Status</th></tr>{rows}</table>
<h2>Limitations</h2><ul>{limitations}</ul>
""".format(
        status=html.escape(str(comparison["status"])),
        workload=html.escape(str(comparison["workload"])),
        reasons=reasons or "<li>none</li>",
        rows="".join(rows),
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
    comparison = compare_sides(load_side(args.baseline), load_side(args.candidate))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(comparison, indent=2) + "\n", encoding="utf-8")
    if args.html_output:
        args.html_output.parent.mkdir(parents=True, exist_ok=True)
        args.html_output.write_text(render_html(comparison), encoding="utf-8")
    print(json.dumps(comparison, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
