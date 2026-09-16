#!/usr/bin/env python3
"""Capture one complete Device Performance Profile V0 serial window."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from pathlib import Path

import serial
from serial import SerialException


ANSI_ESCAPE = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")
PROFILE_RECORD = re.compile(r"\b(device_profile(?:_timing|_pipeline|_present|_counters)?)\b.*?\bwindow=(\d+)\b")
REQUIRED_RECORDS = frozenset((
    "device_profile",
    "device_profile_timing",
    "device_profile_pipeline",
    "device_profile_present",
    "device_profile_counters",
))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--reset", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    raw_path = args.output / "console.raw.log"
    clean_path = args.output / "console.log"
    deadline = time.monotonic() + args.timeout
    records_by_window: dict[int, set[str]] = {}
    lines = 0
    reconnects = 0
    completed_window: int | None = None
    device: serial.Serial | None = None
    pending = bytearray()

    def process_line(data: bytes) -> None:
        nonlocal completed_window
        line = ANSI_ESCAPE.sub(b"", data).decode("utf-8", errors="replace").replace("\r", "")
        clean.write(line + "\n")
        match = PROFILE_RECORD.search(line)
        if match is None:
            return
        kind, raw_window = match.groups()
        window = int(raw_window)
        records = records_by_window.setdefault(window, set())
        records.add(kind)
        if records == REQUIRED_RECORDS:
            completed_window = window

    with raw_path.open("wb") as raw, clean_path.open("w", encoding="utf-8", newline="") as clean:
        while time.monotonic() < deadline and completed_window is None:
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
                    time.sleep(0.25)
                    continue
            try:
                data = device.read(4096)
            except (OSError, SerialException):
                device.close()
                device = None
                continue
            if not data:
                continue
            raw.write(data)
            raw.flush()
            pending.extend(data)
            while b"\n" in pending:
                raw_line, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                lines += 1
                process_line(bytes(raw_line))
            clean.flush()

        if pending:
            lines += 1
            process_line(bytes(pending))
            clean.flush()

    if device is not None:
        device.close()

    summary = {
        "format": "jellyframe.device-profile-window-capture.v0",
        "port": args.port,
        "baud": args.baud,
        "timeoutSeconds": args.timeout,
        "resetRequested": args.reset,
        "reconnects": reconnects,
        "lines": lines,
        "windows": {str(key): sorted(value) for key, value in sorted(records_by_window.items())},
        "completedWindow": completed_window,
        "consoleRawSha256": sha256_file(raw_path),
        "status": "pass" if completed_window is not None else "partial",
    }
    (args.output / "capture.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if completed_window is not None else 1


if __name__ == "__main__":
    raise SystemExit(main())
