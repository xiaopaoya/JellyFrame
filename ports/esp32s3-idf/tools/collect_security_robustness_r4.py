"""Collect the WS147 R4 mixed install/touch/stop/reboot/rollback regression."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


STOP_INPUT = re.compile(
    r"posted=(?P<input>\d+).*rejected=(?P<rejected>\d+).*unsupported=(?P<unsupported>\d+).*"
    r"queue_dropped=(?P<dropped>\d+).*worker_seq=(?P<worker>\d+).*mutation_seq=(?P<mutation>\d+).*"
    r"published_seq=(?P<published>\d+).*accepted_seq=(?P<accepted>\d+).*presents_failed=(?P<failed>\d+)"
)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def last_result(text: str) -> dict:
    for line in reversed(text.splitlines()):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if value.get("kind") == "result":
            return value
    raise RuntimeError("provider returned no terminal result")


def invoke(output: Path, label: str, provider: Path, config: Path, operation: str, *args: str,
           jsonl: bool = False) -> dict:
    command = [sys.executable, str(provider), "--output", "jsonl" if jsonl else "json",
               "--request-id", label, "--config", str(config), "--selector", "ws147-security-local",
               operation, *args]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (output / f"{label}.provider.stdout.{'jsonl' if jsonl else 'json'}").write_text(completed.stdout, encoding="utf-8")
    (output / f"{label}.provider.stderr.log").write_text(completed.stderr, encoding="utf-8")
    result = last_result(completed.stdout)
    if completed.returncode != 0 or result.get("resultCode") not in {"ok", "accepted"}:
        raise RuntimeError(f"{label}: {result.get('resultCode')} exit={completed.returncode}")
    return {"result": result, "stdout": completed.stdout}


def hard_reset(output: Path, label: str, port: str) -> None:
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
               "--before", "default_reset", "--after", "hard_reset", "chip_id"]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (output / f"{label}.stdout.log").write_text(completed.stdout, encoding="utf-8")
    (output / f"{label}.stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(f"{label}: esptool exit={completed.returncode}")
    time.sleep(1.5)


def stop_metrics(stdout: str) -> dict[str, int] | None:
    for line in stdout.splitlines():
        try:
            message = json.loads(line).get("log", {}).get("message", "")
        except json.JSONDecodeError:
            continue
        match = STOP_INPUT.search(message) if "script-input stopped" in message else None
        if match:
            return {name: int(value) for name, value in match.groupdict().items()}
    return None


def touch_pass(metrics: dict[str, int] | None) -> bool:
    return metrics is not None and metrics["input"] >= 1 and metrics["worker"] >= 1 and metrics["mutation"] >= 1 and \
        metrics["published"] >= metrics["mutation"] and metrics["accepted"] >= metrics["published"] and \
        all(metrics[name] == 0 for name in ("rejected", "unsupported", "dropped", "failed"))


def make_fixture(repo: Path, output: Path) -> Path:
    source = output / "fixture-source"
    shutil.copytree(repo / "tests" / "fixtures" / "apps" / "jelly_installed_script_input", source)
    manifest_path = source / "jellyframe.app.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["id"] = "org.jellyframe.security.r4-large-touch"
    manifest["name"] = "Security R4 Large Touch"
    manifest["version"] = {"name": "1.0.0", "code": 1}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    entry_path = source / "index.html"
    entry_path.write_text(entry_path.read_text(encoding="utf-8") + "\n<!-- " + ("R4-LARGE-ENTRY " * 220) + "-->\n",
                          encoding="utf-8")
    bundle = output / "r4-large-touch.jfapp"
    report = output / "r4-large-touch.package.json"
    completed = subprocess.run([sys.executable, str(repo / "tools" / "package_app.py"), "--root", str(source),
                                "--output-bundle", str(bundle), "--report", str(report)],
                               text=True, capture_output=True, check=False)
    (output / "package.stdout.log").write_text(completed.stdout, encoding="utf-8")
    (output / "package.stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode or entry_path.stat().st_size <= 3072:
        raise RuntimeError("failed to build the large installed touch fixture")
    write_json(output / "fixture_hash.json", {"bundle": str(bundle), "bytes": bundle.stat().st_size,
                                               "sha256": sha256(bundle), "entryBytes": entry_path.stat().st_size})
    return bundle


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cycles", type=int, default=30)
    parser.add_argument("--touch-window-seconds", type=float, default=7.0)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 30 or not 3 <= args.touch_window_seconds <= 30 or args.output.exists():
        raise SystemExit("invalid cycle count, touch window, or existing output directory")
    repo = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    output.mkdir(parents=True)
    provider = repo / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe_device.py"
    config = args.config.resolve()
    bundle = make_fixture(repo, output)
    app_id = "org.jellyframe.security.r4-large-touch"
    cases: list[dict[str, object]] = []
    print("For every Ready: 0 screen, tap Tap once. Hold Tap through shutdown for cycles 26-30.", flush=True)
    for number in range(1, args.cycles + 1):
        prefix = f"cycle-{number:02d}"
        try:
            invoke(output, f"{prefix}-install", provider, config, "install", "--bundle", str(bundle), jsonl=True)
            invoke(output, f"{prefix}-launch", provider, config, "launch", "--id", app_id)
            print(f"Cycle {number}/{args.cycles}: touch now ({args.touch_window_seconds:.1f}s)", flush=True)
            time.sleep(args.touch_window_seconds)
            invoke(output, f"{prefix}-stop", provider, config, "stop", "--id", app_id)
            logs = invoke(output, f"{prefix}-logs", provider, config, "logs", "--id", app_id, "--limit", "11", jsonl=True)
            metrics = stop_metrics(logs["stdout"])
            hard_reset(output, f"{prefix}-reboot", args.port)
            listed = invoke(output, f"{prefix}-list", provider, config, "list")
            apps = listed["result"].get("apps", [])
            listed_ok = len(apps) == 1 and apps[0].get("appId") == app_id
            invoke(output, f"{prefix}-launch-after-reboot", provider, config, "launch", "--id", app_id)
            invoke(output, f"{prefix}-rollback", provider, config, "rollback", "--id", app_id)
            after = invoke(output, f"{prefix}-list-after-rollback", provider, config, "list")
            generation = int(after["result"].get("registryGeneration", 0))
            cases.append({"cycle": number, "touch": metrics, "listed": listed_ok, "generation": generation,
                          "pass": touch_pass(metrics) and listed_ok})
        except RuntimeError as error:
            cases.append({"cycle": number, "error": str(error), "pass": False})
            break
    successes = sum(case["pass"] for case in cases)
    summary = {"format": "jellyframe.ws147.security-r4", "formatVersion": 0, "appId": app_id,
               "cyclesRequested": args.cycles, "cyclesCompleted": len(cases), "successfulCycles": successes,
               "failedCycles": len(cases) - successes, "cases": cases,
               "result": "pass" if len(cases) == args.cycles and successes == args.cycles else "fail"}
    write_json(output / "summary.json", summary)
    print(json.dumps({key: summary[key] for key in ("cyclesCompleted", "successfulCycles", "failedCycles", "result")}), flush=True)
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
