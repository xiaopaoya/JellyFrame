#!/usr/bin/env python3
"""Capture a WS147 ScriptTaskAppFrame v2 soak across serial reconnects."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from pathlib import Path
from typing import Any

import serial
from serial import SerialException


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
START_MARKER = "script_value_frame_v2_start"
RUNNING_MARKER = "script_value_frame_v2_telemetry status=running"
PASS_MARKER = "script_value_frame_v2_telemetry status=pass"
TELEMETRY_MARKER = "script_value_frame_v2_telemetry"
KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>[^\s]+)")
INTEGER_RE = re.compile(r"-?[0-9]+$")
FLOAT_RE = re.compile(r"-?(?:[0-9]+\.[0-9]*|[0-9]*\.[0-9]+)$")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clean_log_path(raw_path: Path) -> Path:
    if raw_path.name.endswith(".raw.log"):
        return raw_path.with_name(raw_path.name[: -len(".raw.log")] + ".clean.log")
    return raw_path.with_suffix(".clean.log")


def parse_value(value: str) -> str | int | float:
    if INTEGER_RE.fullmatch(value):
        return int(value)
    if FLOAT_RE.fullmatch(value):
        return float(value)
    return value


def parse_telemetry(line: str) -> dict[str, str | int | float] | None:
    marker = line.find(TELEMETRY_MARKER)
    if marker < 0:
        return None
    fields: dict[str, str | int | float] = {}
    for match in KEY_VALUE_RE.finditer(line[marker + len(TELEMETRY_MARKER) :]):
        fields[match.group("key")] = parse_value(match.group("value"))
    return fields or None


def scan_log(raw_path: Path, clean_path: Path) -> tuple[int, bool, bool, dict[str, str | int | float]]:
    line_count = 0
    saw_start = False
    saw_pass = False
    final_telemetry: dict[str, str | int | float] = {}
    with raw_path.open("rb") as raw, clean_path.open("w", encoding="utf-8") as clean:
        for data in raw:
            line_count += 1
            line = ANSI_RE.sub("", data.decode("utf-8", errors="replace").replace("\r", ""))
            clean.write(line)
            saw_start |= START_MARKER in line or RUNNING_MARKER in line
            saw_pass |= PASS_MARKER in line
            telemetry = parse_telemetry(line)
            if telemetry is not None:
                final_telemetry = telemetry
    return line_count, saw_start, saw_pass, final_telemetry


def write_summary(summary_path: Path,
                  raw_path: Path,
                  clean_path: Path,
                  port: str | None,
                  baud: int | None,
                  timeout: float | None,
                  reconnects: int | None,
                  line_count: int,
                  saw_start: bool,
                  saw_pass: bool,
                  final_telemetry: dict[str, str | int | float]) -> None:
    status = "pass" if saw_start and saw_pass and final_telemetry.get("status") == "pass" else "incomplete"
    summary: dict[str, Any] = {
        "schemaVersion": 1,
        "case": "script_task_value_frame_v2_soak",
        "status": status,
        "capture": {
            "port": port,
            "baud": baud,
            "timeoutSeconds": timeout,
            "reconnects": reconnects,
            "lines": line_count,
            "sawStart": saw_start,
            "sawPass": saw_pass,
        },
        "files": {
            "rawLog": raw_path.name,
            "rawLogSha256": sha256_file(raw_path),
            "cleanLog": clean_path.name,
            "cleanLogSha256": sha256_file(clean_path),
        },
        "finalTelemetry": final_telemetry,
    }
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=660.0)
    parser.add_argument("--reset", action="store_true", help="Pulse RTS after opening the port.")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--output", type=Path, help="Raw serial-log path for a live capture.")
    source.add_argument("--from-log", type=Path, help="Create clean log and summary from an existing raw serial log.")
    parser.add_argument("--clean-output", type=Path, help="Clean-log path; defaults beside the raw log.")
    parser.add_argument("--summary", type=Path, help="Summary JSON path; defaults to summary.json beside the raw log.")
    args = parser.parse_args()

    raw_path = args.from_log if args.from_log is not None else args.output
    assert raw_path is not None
    clean_path = args.clean_output or clean_log_path(raw_path)
    summary_path = args.summary or raw_path.with_name("summary.json")
    if args.from_log is not None:
        if not raw_path.is_file():
            parser.error(f"raw serial log does not exist: {raw_path}")
        line_count, saw_start, saw_pass, final_telemetry = scan_log(raw_path, clean_path)
        write_summary(summary_path,
                      raw_path,
                      clean_path,
                      None,
                      None,
                      None,
                      None,
                      line_count,
                      saw_start,
                      saw_pass,
                      final_telemetry)
        return 0 if saw_start and saw_pass and final_telemetry.get("status") == "pass" else 1

    raw_path.parent.mkdir(parents=True, exist_ok=True)
    clean_path.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.timeout
    line_count = 0
    reconnects = 0
    saw_start = False
    saw_pass = False
    final_telemetry: dict[str, str | int | float] = {}
    device: serial.Serial | None = None

    with raw_path.open("wb") as raw, clean_path.open("w", encoding="utf-8") as clean:
        while time.monotonic() < deadline and not saw_pass:
            if device is None:
                try:
                    device = serial.Serial()
                    device.port = args.port
                    device.baudrate = args.baud
                    device.timeout = 0.25
                    device.dtr = False
                    device.rts = False
                    device.open()
                    if args.reset:
                        device.rts = True
                        time.sleep(0.12)
                        device.rts = False
                        time.sleep(0.35)
                    reconnects += 1
                except (OSError, SerialException):
                    time.sleep(0.5)
                    continue
            try:
                data = device.readline()
            except (OSError, SerialException):
                try:
                    device.close()
                except SerialException:
                    pass
                device = None
                continue
            if not data:
                continue
            raw.write(data)
            raw.flush()
            line_count += 1
            line = ANSI_RE.sub("", data.decode("utf-8", errors="replace").replace("\r", ""))
            clean.write(line)
            clean.flush()
            # The board can emit its startup line before a host opens COM19
            # after flashing. A periodic running telemetry line proves the
            # fixture was already started and is the reliable fallback.
            saw_start |= START_MARKER in line or RUNNING_MARKER in line
            saw_pass |= PASS_MARKER in line
            telemetry = parse_telemetry(line)
            if telemetry is not None:
                final_telemetry = telemetry

    if device is not None:
        device.close()

    meta_path = raw_path.with_suffix(raw_path.suffix + ".meta.txt")
    digest = sha256_file(raw_path)
    meta_path.write_text(
        f"port={args.port}\nbaud={args.baud}\ntimeout_seconds={args.timeout:.1f}\n"
        f"lines={line_count}\nreconnects={reconnects}\nsaw_start={int(saw_start)}\n"
        f"saw_pass={int(saw_pass)}\nserial_raw_sha256={digest}\n",
        encoding="utf-8",
    )
    write_summary(summary_path,
                  raw_path,
                  clean_path,
                  args.port,
                  args.baud,
                  args.timeout,
                  reconnects,
                  line_count,
                  saw_start,
                  saw_pass,
                  final_telemetry)
    return 0 if saw_start and saw_pass and final_telemetry.get("status") == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
