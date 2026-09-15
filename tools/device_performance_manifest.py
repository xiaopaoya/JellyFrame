#!/usr/bin/env python3
"""Create a Device Performance Profile V0 comparison-side manifest.

The collector understands the repeat layout used by hardware A/B archives and
only records paths and identities. It does not parse raw frames or invent
missing device measurements.
"""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
from typing import Any


FORMAT = "jellyframe.device.performance.run.v0"
MIN_REPEATS = 3
VISUAL_STATUSES = ("exact-readback", "visual-equivalent-only", "missing", "fail")
ACCEPTANCE_MODES = ("target-improvement", "non-regression")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"failed to read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise SystemExit(f"JSON root must be an object: {path}")
    return value


def recorded_sha256(path: Path) -> str:
    try:
        text = path.read_text(encoding="ascii")
    except OSError as error:
        raise SystemExit(f"failed to read firmware hash record {path}: {error}") from error
    hashes = {match.upper() for match in re.findall(r"\b[0-9a-fA-F]{64}\b", text)}
    if len(hashes) != 1:
        raise SystemExit(f"{path}: expected exactly one recorded SHA-256 value")
    return next(iter(hashes))


def collect_reports(root: Path, workload: str, side: str) -> tuple[list[Path], str]:
    reports = sorted(root.glob(f"{workload}/repeat-*/{side}/on/result.json"))
    if len(reports) < MIN_REPEATS:
        raise SystemExit(
            f"{root}: {workload}/{side} has {len(reports)} repeats; at least {MIN_REPEATS} are required"
        )
    firmware_hashes: set[str] = set()
    for report in reports:
        firmware = report.parent / "firmware.sha256"
        if not firmware.is_file():
            raise SystemExit(f"missing firmware.sha256 beside {report}")
        firmware_hashes.add(recorded_sha256(firmware))
    if len(firmware_hashes) != 1:
        raise SystemExit(f"{root}: {workload}/{side} repeats use different firmware hashes")
    return reports, next(iter(firmware_hashes))


def relative_report_path(output: Path, report: Path) -> str:
    return Path(os.path.relpath(report, output.parent)).as_posix()


def build_manifest(args: argparse.Namespace) -> dict[str, Any]:
    root = args.root.resolve()
    output = args.output.resolve()
    conditions = read_json(args.conditions_json)
    reports, firmware_hash = collect_reports(root, args.workload, args.side)
    return {
        "format": FORMAT,
        "workload": args.workload,
        "identity": {
            "commit": args.commit,
            "firmwareSha256": firmware_hash,
            "side": args.side,
        },
        "conditions": conditions,
        "visualEvidence": {"status": args.visual_status},
        "stability": {"status": args.stability_status},
        "acceptance": {
            "mode": args.acceptance_mode,
            "targetMetric": args.target_metric,
            "minimumImprovementPercent": args.minimum_improvement_percent,
            "maxFrameRegressionPercent": args.max_frame_regression_percent,
            "maxMemoryRegressionPercent": args.max_memory_regression_percent,
        },
        "reports": [relative_report_path(output, report) for report in reports],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--workload", required=True)
    parser.add_argument("--side", required=True, choices=("baseline", "candidate"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--conditions-json", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--visual-status", choices=VISUAL_STATUSES, default="missing")
    parser.add_argument("--stability-status", choices=("pass", "fail"), default="fail")
    parser.add_argument("--target-metric", default="frameP95Us")
    parser.add_argument("--acceptance-mode", choices=ACCEPTANCE_MODES, default="target-improvement")
    parser.add_argument("--minimum-improvement-percent", type=float, default=5.0)
    parser.add_argument("--max-frame-regression-percent", type=float, default=3.0)
    parser.add_argument("--max-memory-regression-percent", type=float, default=5.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = build_manifest(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
