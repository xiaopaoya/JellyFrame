#!/usr/bin/env python3
"""Capture the WS147 mixed script-service soak, reconnecting across USB resets."""

from __future__ import annotations

import argparse
import hashlib
import re
import time
from pathlib import Path

import serial
from serial import SerialException


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=1800.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    raw_path = args.output / "serial.raw.log"
    clean_path = args.output / "serial.clean.log"
    host_path = args.output / "capture_host.log"
    deadline = time.monotonic() + args.duration
    line_count = 0
    reconnects = 0
    device = None

    with raw_path.open("wb") as raw, clean_path.open("w", encoding="utf-8") as clean, host_path.open(
        "w", encoding="utf-8"
    ) as host:
        while time.monotonic() < deadline:
            if device is None:
                try:
                    device = serial.Serial()
                    device.port = args.port
                    device.baudrate = args.baud
                    device.timeout = 0.25
                    device.dtr = False
                    device.rts = False
                    device.open()
                    reconnects += 1
                    host.write(f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} open reconnect={reconnects}\n")
                    host.flush()
                except (OSError, SerialException):
                    time.sleep(0.5)
                    continue
            try:
                data = device.readline()
            except (OSError, SerialException):
                try:
                    device.close()
                except Exception:
                    pass
                device = None
                continue
            if not data:
                continue
            raw.write(data)
            raw.flush()
            line = data.decode("utf-8", errors="replace").replace("\r", "")
            clean.write(ANSI_RE.sub("", line))
            clean.flush()
            line_count += 1

        if device is not None:
            device.close()

    digest = hashlib.sha256(raw_path.read_bytes()).hexdigest()
    (args.output / "capture_meta.txt").write_text(
        f"port={args.port}\nbaud={args.baud}\nduration_seconds={args.duration:.1f}\n"
        f"lines={line_count}\nreconnects={reconnects}\nserial_raw_sha256={digest}\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
