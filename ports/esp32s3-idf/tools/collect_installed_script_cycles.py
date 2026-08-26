#!/usr/bin/env python3
"""Collect physical-tap installed ScriptTask lifecycle evidence over JFDP/1."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path


STOP_TELEMETRY = re.compile(
    r"initialized=(?P<initialized>[01]).*input_seq=(?P<input>\d+).*"
    r"mutation_seq=(?P<mutation>\d+).*published_seq=(?P<published>\d+).*"
    r"accepted_seq=(?P<accepted>\d+).*presents_failed=(?P<failed>\d+).*fatal=(?P<fatal>[01])"
)


def invoke(provider: Path, output: str, request_id: str, args: list[str], directory: Path) -> tuple[int, str, str]:
    completed = subprocess.run(
        [str(provider), "--output", output, "--request-id", request_id, *args],
        text=True, capture_output=True, check=False,
    )
    (directory / f"{request_id}.stdout.{output}").write_text(completed.stdout, encoding="utf-8")
    (directory / f"{request_id}.stderr.log").write_text(completed.stderr, encoding="utf-8")
    return completed.returncode, completed.stdout, completed.stderr


def result_code(text: str) -> str:
    try:
        return str(json.loads(text).get("resultCode", "invalid-json"))
    except json.JSONDecodeError:
        return "invalid-json"


def stop_snapshot(text: str) -> dict[str, int] | None:
    for line in text.splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        message = record.get("log", {}).get("message", "")
        match = STOP_TELEMETRY.search(message) if isinstance(message, str) and "script stopped" in message else None
        if match:
            return {key: int(value) for key, value in match.groupdict().items()}
    return None


def passed(snapshot: dict[str, int] | None) -> bool:
    return snapshot is not None and snapshot["initialized"] == 1 and snapshot["input"] >= 1 and \
        snapshot["mutation"] >= 1 and snapshot["published"] >= snapshot["mutation"] and \
        snapshot["accepted"] >= snapshot["published"] and snapshot["failed"] == 0 and snapshot["fatal"] == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", required=True, type=Path)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cycles", type=int, default=30)
    parser.add_argument("--tap-window-seconds", type=float, default=5.0)
    parser.add_argument("--settle-seconds", type=float, default=0.2)
    args = parser.parse_args()
    if (not args.provider.is_file() or not 1 <= args.cycles <= 100 or
            not 2.0 <= args.tap_window_seconds <= 30.0 or not 0.0 <= args.settle_seconds <= 5.0):
        raise SystemExit("invalid provider, cycle count, or tap window")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases: list[dict[str, object]] = []
    print("Start tapping only when the screen displays Ready: 0. One complete tap per cycle.", flush=True)
    for number in range(1, args.cycles + 1):
        prefix = f"cycle-{number:02d}"
        launch_rc, launch_text, _ = invoke(args.provider, "json", f"{prefix}-launch",
                                            ["--selector", args.endpoint, "launch", "--id", args.app_id], output)
        launch_code = result_code(launch_text)
        if launch_rc == 0 and launch_code == "ok":
            print(f"Cycle {number}/{args.cycles}: tap now ({args.tap_window_seconds:.1f}s)", flush=True)
            time.sleep(args.tap_window_seconds)
        stop_rc, stop_text, _ = invoke(args.provider, "json", f"{prefix}-stop",
                                        ["--selector", args.endpoint, "stop", "--id", args.app_id], output)
        logs_rc, logs_text, _ = invoke(args.provider, "jsonl", f"{prefix}-logs",
                                        ["--selector", args.endpoint, "logs", "--id", args.app_id, "--limit", "11"], output)
        snapshot = stop_snapshot(logs_text)
        cases.append({"cycle": number, "launch": launch_code, "stop": result_code(stop_text),
                      "logsExitCode": logs_rc, "snapshot": snapshot, "pass": passed(snapshot)})
        if number != args.cycles:
            time.sleep(args.settle_seconds)

    successful = sum(1 for case in cases if case["pass"])
    summary = {"format": "jellyframe.ws147.installed-script-cycles", "formatVersion": 0,
               "endpoint": args.endpoint, "appId": args.app_id, "cycles": args.cycles,
               "tapWindowSeconds": args.tap_window_seconds, "successfulCycles": successful,
               "failedCycles": args.cycles - successful, "cases": cases,
               "result": "pass" if successful == args.cycles else "fail"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: summary[key] for key in ("cycles", "successfulCycles", "failedCycles", "result")}), flush=True)
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
