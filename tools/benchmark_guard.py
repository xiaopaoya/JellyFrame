#!/usr/bin/env python3
"""Run JellyFrame microbenchmarks and fail on catastrophic regressions.

The thresholds here are intentionally broad. They are CI smoke guards, not
release performance claims. Real device baselines belong in port documents.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


RESULT_RE = re.compile(
    r"^(?P<name>[A-Za-z0-9_]+)\s+iterations=(?P<iterations>\d+)\s+avg_us=(?P<avg_us>[0-9]+(?:\.[0-9]+)?)$"
)


@dataclass(frozen=True)
class SuiteConfig:
    executable: str
    args: tuple[str, ...]
    thresholds_us: dict[str, float]


SUITES: dict[str, SuiteConfig] = {
    "render-core": SuiteConfig(
        executable="jellyframe_render_core_microbench",
        args=("40", "20"),
        thresholds_us={
            "custom_property_style_resolve": 60000.0,
            "full_pipeline": 100000.0,
            "dirty_rect_replay_contained": 30000.0,
            "scroll_blit_plan": 50.0,
            "canvas2d_path_stroke": 15000.0,
            "canvas2d_linear_gradient_fill_rect": 60000.0,
        },
    ),
    "app-runtime": SuiteConfig(
        executable="jellyframe_app_runtime_microbench",
        args=("500", "16"),
        thresholds_us={
            "app_runtime_completion_queue": 2000.0,
            "app_runtime_handle_table_churn": 5000.0,
            "app_runtime_system_event_queue": 2000.0,
            "app_runtime_font_family_measure": 5000.0,
            "app_runtime_service_worker_group_pump": 10000.0,
        },
    ),
}


def executable_candidates(build_dir: Path, executable: str) -> Iterable[Path]:
    suffix = ".exe" if os.name == "nt" else ""
    names = [executable + suffix]
    if suffix:
        names.append(executable)
    for name in names:
        yield build_dir / name
        yield build_dir / "Release" / name
        yield build_dir / "RelWithDebInfo" / name
        yield build_dir / "Debug" / name


def find_executable(build_dir: Path, executable: str) -> Path:
    for candidate in executable_candidates(build_dir, executable):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"could not find {executable} under {build_dir}")


def parse_results(output: str) -> dict[str, dict[str, float | int]]:
    results: dict[str, dict[str, float | int]] = {}
    for line in output.splitlines():
        match = RESULT_RE.match(line.strip())
        if not match:
            continue
        results[match.group("name")] = {
            "iterations": int(match.group("iterations")),
            "avg_us": float(match.group("avg_us")),
        }
    return results


def run_suite(name: str, config: SuiteConfig, build_dir: Path) -> dict[str, object]:
    executable = find_executable(build_dir, config.executable)
    command = [str(executable), *config.args]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    if completed.returncode != 0:
        raise RuntimeError(f"{name} benchmark failed with exit code {completed.returncode}")

    results = parse_results(completed.stdout)
    checks: list[dict[str, object]] = []
    failures: list[str] = []
    for metric, threshold in config.thresholds_us.items():
        result = results.get(metric)
        if result is None:
            failures.append(f"{name}:{metric} missing from benchmark output")
            checks.append({"metric": metric, "status": "missing", "threshold_us": threshold})
            continue
        avg_us = float(result["avg_us"])
        status = "pass" if avg_us <= threshold else "fail"
        if status == "fail":
            failures.append(f"{name}:{metric} avg_us={avg_us:.3f} > threshold_us={threshold:.3f}")
        checks.append(
            {
                "metric": metric,
                "status": status,
                "avg_us": avg_us,
                "threshold_us": threshold,
                "iterations": int(result["iterations"]),
            }
        )

    return {
        "suite": name,
        "command": command,
        "checks": checks,
        "failures": failures,
    }


def write_report(path: Path, suites: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "format": "jellyframe.benchmark_guard",
        "policy": "broad CI smoke thresholds; not a release performance baseline",
        "suites": suites,
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run JellyFrame microbenchmarks and check broad CI regression thresholds."
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory or configuration output directory containing benchmark executables.",
    )
    parser.add_argument(
        "--suite",
        choices=["all", *SUITES.keys()],
        default="all",
        help="Benchmark suite to run.",
    )
    parser.add_argument(
        "--report",
        help="Optional JSON report path.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Print configured thresholds and exit.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    selected = list(SUITES.keys()) if args.suite == "all" else [args.suite]

    if args.list:
        for name in selected:
            config = SUITES[name]
            print(f"{name}: {config.executable} {' '.join(config.args)}")
            for metric, threshold in config.thresholds_us.items():
                print(f"  {metric} <= {threshold:g} us")
        return 0

    build_dir = Path(args.build_dir)
    suite_reports: list[dict[str, object]] = []
    failures: list[str] = []
    try:
        for name in selected:
            report = run_suite(name, SUITES[name], build_dir)
            suite_reports.append(report)
            failures.extend(str(item) for item in report["failures"])
    except (FileNotFoundError, RuntimeError) as error:
        print(f"benchmark guard error: {error}", file=sys.stderr)
        return 2

    if args.report:
        write_report(Path(args.report), suite_reports)

    if failures:
        print("benchmark guard failed:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("benchmark guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
