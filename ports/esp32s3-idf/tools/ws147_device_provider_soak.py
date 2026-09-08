#!/usr/bin/env python3
"""Run a real WS147 provider lifecycle soak through jellyframe_cli.py.

This is deliberately a host-process harness, rather than a raw JFDP vector
tool: every operation creates the configured provider process and exercises
its serial-open/close behavior as author tooling does.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "tools" / "jellyframe_cli.py"


def parse_result(stdout: str, operation: str) -> dict[str, Any]:
    try:
        decoded = json.loads(stdout)
        if isinstance(decoded, dict):
            result = decoded
        elif isinstance(decoded, list) and decoded and all(isinstance(value, dict) for value in decoded):
            result = decoded[-1]
        else:
            raise AssertionError(f"{operation}: provider emitted invalid JSON structure")
    except json.JSONDecodeError:
        decoder = json.JSONDecoder()
        position = 0
        values: list[dict[str, Any]] = []
        while position < len(stdout):
            while position < len(stdout) and stdout[position].isspace():
                position += 1
            if position == len(stdout):
                break
            value, position = decoder.raw_decode(stdout, position)
            if not isinstance(value, dict):
                raise AssertionError(f"{operation}: provider emitted non-object JSON")
            values.append(value)
        if not values:
            raise AssertionError(f"{operation}: provider emitted no JSON")
        result = values[-1]
    if result.get("operation") != operation or result.get("kind") != "result":
        raise AssertionError(f"{operation}: terminal result is malformed")
    return result


class ProviderHarness:
    def __init__(self, provider: Path, manifest: Path, selector: str, output: Path) -> None:
        self.provider = provider
        self.manifest = manifest
        self.selector = selector
        self.output = output
        self.sequence = 0
        self.records: list[dict[str, Any]] = []

    def run(self, operation: str, *arguments: str, allowed: set[str] | None = None) -> dict[str, Any]:
        self.sequence += 1
        command = [sys.executable, str(CLI), "device", "--provider", str(self.provider),
                   "--manifest", str(self.manifest), operation, "--selector", self.selector, *arguments]
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
        prefix = f"{self.sequence:04d}-{operation}"
        case_output = self.output / prefix
        case_output.mkdir(exist_ok=True)
        (case_output / "cli.command.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
        (case_output / "cli.stdout.json").write_text(completed.stdout, encoding="utf-8")
        (case_output / "cli.stderr.log").write_text(completed.stderr, encoding="utf-8")
        result = parse_result(completed.stdout, operation)
        expected = allowed or {"ok"}
        if result.get("resultCode") not in expected:
            raise AssertionError(f"{operation}: expected {sorted(expected)}, got {result.get('resultCode')}")
        if completed.returncode != 0 and result.get("resultCode") in {"ok", "accepted"}:
            raise AssertionError(f"{operation}: successful result has exit code {completed.returncode}")
        self.records.append({"sequence": self.sequence, "operation": operation,
                             "resultCode": result["resultCode"], "returnCode": completed.returncode})
        return result


def installed_version(result: dict[str, Any], app_id: str) -> int:
    apps = result.get("apps")
    if not isinstance(apps, list) or len(apps) != 1:
        raise AssertionError("list: expected exactly one installed test app")
    app = apps[0]
    if app.get("appId") != app_id or not isinstance(app.get("versionCode"), int):
        raise AssertionError("list: unexpected installed app identity")
    return app["versionCode"]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--provider", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--selector", required=True)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--v1", required=True, type=Path)
    parser.add_argument("--v2", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cycles", type=int, default=30)
    args = parser.parse_args()
    if args.cycles < 1:
        raise SystemExit("--cycles must be positive")

    provider = args.provider.resolve()
    manifest = args.manifest.resolve()
    v1, v2 = args.v1.resolve(), args.v2.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    harness = ProviderHarness(provider, manifest, args.selector, output)
    errors: list[str] = []
    generations: list[int] = []
    cycles_completed = 0
    try:
        # A previous manual run is a valid state; remove only the requested
        # test identity and accept not-found for a clean device.
        harness.run("remove", "--id", args.app_id, allowed={"ok", "not-found"})
        empty = harness.run("list")
        if empty.get("apps") != []:
            raise AssertionError("initial list is not empty after test-app removal")

        for cycle in range(1, args.cycles + 1):
            harness.run("install", "--bundle", str(v1), allowed={"accepted"})
            listed = harness.run("list")
            if installed_version(listed, args.app_id) != 1:
                raise AssertionError(f"cycle {cycle}: v1 publication mismatch")
            generations.append(listed["registryGeneration"])
            harness.run("launch", "--id", args.app_id)
            harness.run("stop", "--id", args.app_id)

            harness.run("install", "--bundle", str(v2), allowed={"accepted"})
            listed = harness.run("list")
            if installed_version(listed, args.app_id) != 2:
                raise AssertionError(f"cycle {cycle}: v2 publication mismatch")
            generations.append(listed["registryGeneration"])
            harness.run("launch", "--id", args.app_id)
            harness.run("rollback", "--id", args.app_id)
            listed = harness.run("list")
            if installed_version(listed, args.app_id) != 1:
                raise AssertionError(f"cycle {cycle}: rollback did not restore v1")
            generations.append(listed["registryGeneration"])
            harness.run("remove", "--id", args.app_id)
            listed = harness.run("list")
            if listed.get("apps") != []:
                raise AssertionError(f"cycle {cycle}: remove did not clear the test app")
            generations.append(listed["registryGeneration"])
            cycles_completed += 1
    except (AssertionError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        errors.append(str(error))

    monotonic = all(later > earlier for earlier, later in zip(generations, generations[1:]))
    summary = {
        "format": "jellyframe.ws147-device-provider-soak",
        "provider": str(provider),
        "manifestSha256": sha256(manifest),
        "selector": args.selector,
        "appId": args.app_id,
        "cyclesRequested": args.cycles,
        "cyclesCompleted": cycles_completed,
        "fixtureSha256": {"v1": sha256(v1), "v2": sha256(v2)},
        "registryGenerations": generations,
        "registryGenerationStrictlyIncreasing": monotonic,
        "operations": harness.records,
        "errors": errors,
        "result": "pass" if not errors and monotonic and len(generations) == args.cycles * 4 else "fail",
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
