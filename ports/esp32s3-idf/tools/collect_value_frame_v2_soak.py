#!/usr/bin/env python3
"""Capture a WS147 ScriptTaskAppFrame v2 soak across serial reconnects."""

from __future__ import annotations

import argparse
import hashlib
import re
import time
from pathlib import Path

import serial
from serial import SerialException


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
START_MARKER = "script_value_frame_v2_start"
RUNNING_MARKER = "script_value_frame_v2_telemetry status=running"
PASS_MARKER = "script_value_frame_v2_telemetry status=pass"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=660.0)
    parser.add_argument("--reset", action="store_true", help="Pulse RTS after opening the port.")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.timeout
    line_count = 0
    reconnects = 0
    saw_start = False
    saw_pass = False
    device: serial.Serial | None = None

    with args.output.open("wb") as raw:
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
            # The board can emit its startup line before a host opens COM19
            # after flashing. A periodic running telemetry line proves the
            # fixture was already started and is the reliable fallback.
            saw_start |= START_MARKER in line or RUNNING_MARKER in line
            saw_pass |= PASS_MARKER in line

    if device is not None:
        device.close()

    meta_path = args.output.with_suffix(args.output.suffix + ".meta.txt")
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    meta_path.write_text(
        f"port={args.port}\nbaud={args.baud}\ntimeout_seconds={args.timeout:.1f}\n"
        f"lines={line_count}\nreconnects={reconnects}\nsaw_start={int(saw_start)}\n"
        f"saw_pass={int(saw_pass)}\nserial_raw_sha256={digest}\n",
        encoding="utf-8",
    )
    return 0 if saw_start and saw_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
