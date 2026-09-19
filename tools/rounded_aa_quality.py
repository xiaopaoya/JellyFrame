#!/usr/bin/env python3
"""Experimental geometric coverage quality gate, deliberately not a timing tool."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re


FORMAT = "jellyframe.rounded.quality.v0"
INPUT_FORMAT = "jellyframe.rounded.coverage.v0"
WIDTH, HEIGHT = 172, 320
PGM_HEADER = b"P5\n172 320\n255\n"
DEPTH = 6
MAX_ERROR = 1 / 4
MAX_EDGE_RMS = 1 / 8
FIXTURES = [
    {"name": "card", "rect": [14, 20, 144, 96], "radius": 24},
    {"name": "rectangle", "rect": [14, 20, 144, 96], "radius": 0},
    {"name": "small-radius", "rect": [23, 29, 12, 10], "radius": 1},
    {"name": "odd-dimensions", "rect": [35, 37, 55, 31], "radius": 11},
    {"name": "maximum-radius", "rect": [14, 20, 144, 96], "radius": 48},
    {"name": "clipped-left", "rect": [-9, 23, 70, 53], "radius": 20},
    {"name": "clipped-bottom-right", "rect": [101, 281, 90, 62], "radius": 24},
    {"name": "tiny", "rect": [3, 3, 2, 2], "radius": 1},
]


def area_bounds(fixture: dict, x: int, y: int, depth: int = DEPTH) -> tuple[float, float]:
    """Enclose continuous area in [x,x+1] x [y,y+1], not a point-sample estimate."""
    if not 0 <= depth <= 8:
        raise ValueError("oracle depth must be between 0 and 8")
    rx, ry, width, height = fixture["rect"]
    radius = fixture["radius"]
    if not rx <= x < rx + width or not ry <= y < ry + height:
        return 0.0, 0.0
    scale = 1 << depth
    left, top = (rx + radius) * scale, (ry + radius) * scale
    right, bottom = (rx + width - radius) * scale, (ry + height - radius) * scale
    squared_radius = (radius * scale) ** 2

    def classify(cx: int, cy: int, size: int) -> tuple[int, int]:
        # Distance extrema over a square to the inset rectangle. Entirely
        # inside/outside cells give exact area; only boundary cells subdivide.
        dx_max = max(left - cx, cx + size - right, 0)
        dy_max = max(top - cy, cy + size - bottom, 0)
        if dx_max * dx_max + dy_max * dy_max <= squared_radius:
            return size * size, size * size
        dx_min = max(left - cx - size, cx - right, 0)
        dy_min = max(top - cy - size, cy - bottom, 0)
        if dx_min * dx_min + dy_min * dy_min >= squared_radius:
            return 0, 0
        if size == 1:
            return 0, 1
        half = size // 2
        lower = upper = 0
        for oy in (0, half):
            for ox in (0, half):
                lo, hi = classify(cx + ox, cy + oy, half)
                lower += lo
                upper += hi
        return lower, upper

    lo, hi = classify(x * scale, y * scale, scale)
    return lo / (scale * scale), hi / (scale * scale)


def evaluate(mask: bytes, bounds: list[tuple[float, float]]) -> dict:
    if len(mask) != len(bounds) or not bounds:
        raise ValueError("mask and oracle dimensions must match")
    inside_errors = outside_errors = edges = 0
    squared_lower = squared_upper = max_lower = max_upper = 0.0
    area_lower = area_upper = 0.0
    for value, (lo, hi) in zip(mask, bounds):
        observed = value / 255
        area_lower += observed - hi
        area_upper += observed - lo
        if lo == hi == 1:
            inside_errors += value != 255
        elif lo == hi == 0:
            outside_errors += value != 0
        else:
            edges += 1
            lower = max(lo - observed, observed - hi, 0)
            upper = max(abs(observed - lo), abs(observed - hi))
            squared_lower += lower * lower
            squared_upper += upper * upper
            max_lower = max(max_lower, lower)
            max_upper = max(max_upper, upper)
    rms_lower = math.sqrt(squared_lower / edges) if edges else 0.0
    rms_upper = math.sqrt(squared_upper / edges) if edges else 0.0
    reasons = []
    if inside_errors:
        reasons.append("interior-not-opaque")
    if outside_errors:
        reasons.append("exterior-not-empty")
    if max_lower > MAX_ERROR:
        reasons.append("edge-max-error")
    if rms_lower > MAX_EDGE_RMS:
        reasons.append("edge-rms-error")
    status = "fail" if reasons else (
        "pass" if max_upper <= MAX_ERROR and rms_upper <= MAX_EDGE_RMS else "indeterminate")
    if status == "indeterminate":
        reasons.append("oracle-interval-crosses-limit")
    return {"status": status, "reasons": reasons, "interiorErrors": inside_errors,
            "exteriorErrors": outside_errors, "edgePixels": edges,
            "edgeMaxError": [max_lower, max_upper], "edgeRmsError": [rms_lower, rms_upper],
            "signedAreaErrorPixels": [area_lower, area_upper]}


def bounded_read(path: Path, limit: int) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"input exceeds size limit: {path}")
    return data


def load_masks(path: Path) -> tuple[dict, set[Path]]:
    raw = bounded_read(path, 64 * 1024)
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get("format") != INPUT_FORMAT:
        raise ValueError("unsupported coverage manifest")
    if manifest.get("fixtureSet") != "rounded-uniform-v0" or json.dumps(manifest.get("fixtures"), sort_keys=True, allow_nan=False) != json.dumps(FIXTURES, sort_keys=True):
        raise ValueError("frozen fixture geometry changed")
    if json.dumps(manifest.get("viewport"), sort_keys=True) != json.dumps({"width": WIDTH, "height": HEIGHT}, sort_keys=True) or manifest.get("performanceMeasured") is not False:
        raise ValueError("coverage input must use the fixed viewport and contain no timing")
    backends = manifest.get("backends")
    if not isinstance(backends, list) or not 1 <= len(backends) <= 4:
        raise ValueError("expected 1-4 coverage backends")
    root = path.resolve().parent
    paths = {path.resolve()}
    ids = set()
    loaded = []
    for backend in backends:
        if not isinstance(backend, dict):
            raise ValueError("backend must be an object")
        name, method, cases = backend.get("id"), backend.get("method"), backend.get("cases")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9-]{1,64}", name) or name in ids:
            raise ValueError("backend id invalid or duplicate")
        ids.add(name)
        if not isinstance(method, str) or not 1 <= len(method) <= 512:
            raise ValueError("coverage method must be declared")
        if not isinstance(cases, list) or len(cases) != len(FIXTURES):
            raise ValueError("incomplete fixture coverage")
        masks = []
        for fixture, case in zip(FIXTURES, cases):
            if not isinstance(case, dict) or case.get("name") != fixture["name"]:
                raise ValueError("fixture order/name changed")
            relative = case.get("mask")
            if not isinstance(relative, str) or not re.fullmatch(r"[a-zA-Z0-9_./-]{1,128}", relative):
                raise ValueError("invalid mask path")
            target = (root / relative).resolve()
            if not target.is_relative_to(root) or target in paths:
                raise ValueError("mask escapes input directory or reuses an input")
            paths.add(target)
            data = bounded_read(target, len(PGM_HEADER) + WIDTH * HEIGHT)
            if not data.startswith(PGM_HEADER) or len(data) != len(PGM_HEADER) + WIDTH * HEIGHT:
                raise ValueError("expected fixed-size canonical P5 coverage mask")
            masks.append((data[len(PGM_HEADER):], hashlib.sha256(data).hexdigest()))
        loaded.append({"id": name, "method": method, "masks": masks})
    return {"sha256": hashlib.sha256(raw).hexdigest(), "backends": loaded}, paths


def make_report(loaded: dict) -> dict:
    oracles = [[area_bounds(fixture, x, y) for y in range(HEIGHT) for x in range(WIDTH)]
               for fixture in FIXTURES]
    backends = []
    for backend in loaded["backends"]:
        cases = []
        for fixture, bounds, (mask, digest) in zip(FIXTURES, oracles, backend["masks"]):
            cases.append({"name": fixture["name"], "maskSha256": digest, **evaluate(mask, bounds)})
        statuses = {case["status"] for case in cases}
        status = "fail" if "fail" in statuses else "indeterminate" if "indeterminate" in statuses else "pass"
        backends.append({"id": backend["id"], "method": backend["method"], "status": status, "cases": cases})
    return {"format": FORMAT, "performanceMeasured": False, "performanceComparable": False,
            "inputSha256": loaded["sha256"], "viewport": {"width": WIDTH, "height": HEIGHT},
            "fixtures": FIXTURES, "policy": {"id": "rounded-area-quarter-v0", "status": "experimental",
                "reference": "continuous-area-in-unit-pixel", "oracle": "integer-quadtree-area-interval",
                "depth": DEPTH, "maxCoverageError": MAX_ERROR, "maxEdgeRms": MAX_EDGE_RMS,
                "interiorExteriorErrors": 0}, "backends": backends}


def markdown(report: dict) -> str:
    lines = ["# Rounded AA Quality", "", "Experimental quality policy; no timing or speed ranking.", "",
             "Policy: rounded-area-quarter-v0. Exact interior/exterior; maximum edge error <= 1/4; edge RMS <= 1/8.", "",
             "Coverage errors are fractions of one pixel (0-1), not RGB-byte errors.",
             "Intervals enclose oracle uncertainty; indeterminate is not a pass.", "",
             "| Backend | Case | Status | Interior / exterior errors | Edge RMS interval | Max interval |",
             "| --- | --- | --- | ---: | ---: | ---: |"]
    for backend in report["backends"]:
        for case in backend["cases"]:
            rms = " - ".join(f"{value:.5f}" for value in case["edgeRmsError"])
            maximum = " - ".join(f"{value:.5f}" for value in case["edgeMaxError"])
            lines.append(f"| {backend['id']} | {case['name']} | {case['status']} | "
                         f"{case['interiorErrors']} / {case['exteriorErrors']} | {rms} | {maximum} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()
    try:
        loaded, inputs = load_masks(args.input)
        outputs = [path.resolve() for path in (args.output, args.markdown_output) if path is not None]
        if len(set(outputs)) != len(outputs) or any(path in inputs for path in outputs):
            raise ValueError("outputs must be distinct and cannot overwrite inputs")
        report = make_report(loaded)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        if args.markdown_output:
            args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
            args.markdown_output.write_text(markdown(report), encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("rounded quality report written; performance_measured=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
