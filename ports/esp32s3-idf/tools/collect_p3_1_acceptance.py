#!/usr/bin/env python3
"""Interactive WS147 P3-1 collector.

The operator supplies the physical reset and touch actions. This tool only
captures serial values and never injects input or toggles reset lines.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

try:
    import serial
    from serial import SerialException
except ImportError as exc:  # pragma: no cover - depends on the host environment
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
TELEMETRY_RE = re.compile(r"script_app_telemetry\s+(.*)")
KV_RE = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=([^\s\r\n]+)")


def parse_kv(line: str) -> dict[str, str]:
    match = TELEMETRY_RE.search(line)
    source = match.group(1) if match else line
    return {key: value for key, value in KV_RE.findall(source)}


def as_int(values: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(values.get(key, default))
    except ValueError:
        return default


def clean_line(line: str) -> str:
    return ANSI_RE.sub("", line).replace("\r", "")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def git_value(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def firmware_sha(build_dir: Path) -> str:
    image = build_dir / "jellyframe_esp32s3_bench.bin"
    if not image.exists():
        return "unknown"
    digest = hashlib.sha256()
    with image.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


class SerialCapture:
    def __init__(self, port: str, baud: int, reconnect_seconds: float) -> None:
        self.port = port
        self.baud = baud
        self.reconnect_seconds = reconnect_seconds
        self.device: Optional[serial.Serial] = None

    def close(self) -> None:
        if self.device is not None:
            try:
                self.device.close()
            except SerialException:
                pass
            self.device = None

    def _open(self) -> bool:
        try:
            device = serial.Serial()
            device.port = self.port
            device.baudrate = self.baud
            device.timeout = 0.25
            device.dtr = False
            device.rts = False
            device.open()
            self.device = device
            return True
        except (OSError, SerialException):
            self.close()
            return False

    def lines_until(self, deadline: float):
        while time.monotonic() < deadline:
            if self.device is None and not self._open():
                time.sleep(self.reconnect_seconds)
                continue
            try:
                raw = self.device.readline() if self.device is not None else b""
            except (OSError, SerialException):
                self.close()
                continue
            if raw:
                yield raw.decode("utf-8", errors="replace")


def round_metrics(lines: list[str], touches: int) -> dict:
    clean = [clean_line(line) for line in lines]
    posted = sum("script_p3_1_input_posted" in line for line in clean)
    rejected = sum("script_p3_1_input_rejected" in line for line in clean)
    physical_downs = sum("waveshare 1.47 touch down" in line for line in clean)
    physical_releases = sum("waveshare 1.47 touch released" in line for line in clean)
    worker_mutations = sum(
        "script_p3_1_worker" in line and "dom_mutated=1" in line for line in clean
    )
    worker_frames = sum(
        "script_p3_1_worker" in line and "frame_published=1" in line for line in clean
    )
    ui_frames = sum(
        "script_p3_1_ui" in line and "present_ok=1" in line for line in clean
    )
    present_failures = sum(
        "script_p3_1_ui" in line and "present_ok=0" in line for line in clean
    )
    telemetry: dict[str, str] = {}
    for line in clean:
        if "script_app_telemetry" in line:
            telemetry = parse_kv(line)

    touch_summary = {}
    for line in clean:
        if "waveshare 1.47 touch summary" in line:
            touch_summary = parse_kv(line)

    sequence_values = {
        "inputPacket": [],
        "jsMutation": [],
        "publishedFrame": [],
        "acceptedFramePacket": [],
    }
    sequence_fields = {
        "inputPacket": "input_packet_seq=",
        "jsMutation": "js_mutation_seq=",
        "publishedFrame": "published_frame_seq=",
        "acceptedFramePacket": "accepted_frame_packet_seq=",
    }
    sequence_sources = {
        "inputPacket": "script_p3_1_worker",
        "jsMutation": "script_p3_1_worker",
        "publishedFrame": "script_p3_1_worker",
        "acceptedFramePacket": "script_p3_1_ui",
    }
    for line in clean:
        for name, field in sequence_fields.items():
            if sequence_sources[name] not in line:
                continue
            if name == "publishedFrame" and "frame_published=1" not in line:
                continue
            if name == "jsMutation" and "dom_mutated=1" not in line:
                continue
            match = re.search(re.escape(field) + r"(\d+)", line)
            if match:
                sequence_values[name].append(int(match.group(1)))

    def sequence_summary(values: list[int]) -> dict[str, object]:
        return {
            "count": len(values),
            "first": values[0] if values else None,
            "last": values[-1] if values else None,
            "strictlyIncreasing": all(a < b for a, b in zip(values, values[1:])),
        }

    sequence_summary_values = {
        name: sequence_summary(values) for name, values in sequence_values.items()
    }

    error_patterns = {
        "panic": r"panic|Guru Meditation|abort\(",
        "watchdog": r"watchdog|task wdt|Interrupt wdt",
        "dma": r"DMA.*(error|fail)|dma.*(error|fail)",
        "spi": r"SPI.*(error|fail)|spi.*(error|fail)",
        "reset": r"rst:0x|mcuReset|brownout",
    }
    errors = {
        name: sum(bool(re.search(pattern, line, re.IGNORECASE)) for line in clean)
        for name, pattern in error_patterns.items()
    }
    pass_reasons = {
        "physicalTouchTargetMet": physical_downs >= touches,
        "physicalReleaseTargetMet": physical_releases >= touches,
        "inputEventsObserved": posted > 0,
        "noInputRejection": rejected == 0,
        "noPresentFailure": present_failures == 0,
        "noRuntimeError": all(value == 0 for value in errors.values()),
    }
    return {
        "touchTarget": touches,
        "physicalTouchDowns": physical_downs,
        "physicalTouchReleases": physical_releases,
        "inputPosted": posted,
        "inputRejected": rejected,
        "jsMutations": worker_mutations,
        "publishedFrames": worker_frames,
        "presentedFrames": ui_frames,
        "presentFailures": present_failures,
        "touchSummary": touch_summary,
        "telemetry": telemetry,
        "sequences": sequence_summary_values,
        "errors": errors,
        "passReasons": pass_reasons,
        "status": "pass" if all(pass_reasons.values()) else "partial",
    }


def write_round(round_dir: Path, lines: list[str], metrics: dict) -> None:
    round_dir.mkdir(parents=True, exist_ok=True)
    (round_dir / "serial.raw.log").write_text("".join(lines), encoding="utf-8")
    (round_dir / "serial.clean.log").write_text(
        "".join(clean_line(line) + "\n" for line in lines), encoding="utf-8"
    )
    (round_dir / "summary.json").write_text(
        json.dumps(metrics, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )


def collect_round(
    capture: SerialCapture,
    round_number: int,
    rounds: int,
    touches: int,
    boot_timeout: float,
    touch_timeout: float,
    settle_seconds: float,
) -> tuple[list[str], dict]:
    print(
        f"\nRound {round_number}/{rounds}: press RST now. "
        "Waiting for script-app boot and first presented frame.",
        flush=True,
    )
    lines: list[str] = []
    boot_deadline = time.monotonic() + boot_timeout
    first_frame = False
    for line in capture.lines_until(boot_deadline):
        lines.append(line)
        clean = clean_line(line)
        if "script_p3_1_ui" in clean and "present_ok=1" in clean:
            first_frame = True
            break
        if "script_app_telemetry" in clean and "frames_presented=1" in clean:
            first_frame = True
            break
    if not first_frame:
        metrics = round_metrics(lines, touches)
        metrics["status"] = "timeout-before-first-frame"
        return lines, metrics

    print(
        f"First frame seen. Perform {touches} deliberate taps/releases now; "
        "keep a short pause between taps.",
        flush=True,
    )
    target_deadline = time.monotonic() + touch_timeout
    physical_touches = 0
    for line in capture.lines_until(target_deadline):
        lines.append(line)
        clean = clean_line(line)
        if "waveshare 1.47 touch down" in clean:
            physical_touches += 1
            print(
                f"  Physical touches: {min(physical_touches, touches)}/{touches} "
                "(one touch may produce multiple JS mutations)",
                flush=True,
            )
        if physical_touches >= touches:
            break

    if physical_touches >= touches:
        settle_deadline = time.monotonic() + settle_seconds
        lines.extend(capture.lines_until(settle_deadline))
    metrics = round_metrics(lines, touches)
    return lines, metrics


def write_report(output: Path, metadata: dict, rounds: list[dict]) -> None:
    passed = sum(item.get("status") == "pass" for item in rounds)
    report = [
        "# P3-1 Automated WS147 Collection",
        "",
        "This artifact records operator-supplied RST and physical touch actions.",
        "No reset or touch event is injected by the collector.",
        "The touch target counts Waveshare touch-down/release pairs; JS mutation count is reported separately and is not used as the touch count.",
        "",
        "## Environment",
        "",
    ]
    for key, value in metadata.items():
        report.append(f"- {key}: `{value}`")
    report += [
        "",
        "## Round summary",
        "",
        "| round | status | physical touches | JS mutations | input rejected | frames presented | present failures | stack words worker/ui/supervisor |",
        "|---:|---|---:|---:|---:|---:|---:|---|",
    ]
    for item in rounds:
        telemetry = item.get("telemetry", {})
        report.append(
            "| {round} | {status} | {touches} | {mutations} | {rejected} | {presented} | {failures} | {worker}/{ui}/{supervisor} |".format(
                round=item["round"],
                status=item["status"],
                touches=item["physicalTouchDowns"],
                mutations=item["jsMutations"],
                rejected=item["inputRejected"],
                presented=item["presentedFrames"],
                failures=item["presentFailures"],
                worker=telemetry.get("worker_stack_low_water_words", "n/a"),
                ui=telemetry.get("ui_stack_low_water_words", "n/a"),
                supervisor=telemetry.get("supervisor_stack_low_water_words", "n/a"),
            )
        )
    report += [
        "",
        f"Completed rounds: {passed}/{len(rounds)}.",
        "The P3-1 final gate requires 30/30 rounds, no rejection, no present failure,",
        "and a physical observer checklist. A partial or interrupted run is not promoted to pass.",
        "",
    ]
    (output / "report.md").write_text("\n".join(report), encoding="utf-8")


def main() -> int:
    root = repo_root()
    port_root = root / "ports" / "esp32s3-idf"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--touches", type=int, default=10)
    parser.add_argument("--boot-timeout", type=float, default=35)
    parser.add_argument("--touch-timeout", type=float, default=90)
    # The firmware emits cumulative telemetry every 5 seconds. Wait past one
    # interval after the final touch so the per-round snapshot includes it.
    parser.add_argument("--settle-seconds", type=float, default=6)
    parser.add_argument("--build-dir", default="p3-script-app-20260807-current")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 1 or args.touches < 1:
        parser.error("rounds and touches must be positive")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output = args.output or port_root / "test_artifacts" / f"p3-1-automated-{stamp}"
    output.mkdir(parents=True, exist_ok=True)
    build_dir = port_root / args.build_dir
    metadata = {
        "commit": git_value(root, "rev-parse", "HEAD"),
        "worktree": "dirty" if git_value(root, "status", "--porcelain") else "clean",
        "firmwareSha256": firmware_sha(build_dir),
        "profile": "ws147-script-app-acceptance",
        "board": "Waveshare ESP32-S3-Touch-LCD-1.47",
        "panel": "JD9853 172x320",
        "port": args.port,
        "baud": args.baud,
        "roundsTarget": args.rounds,
        "touchesPerRound": args.touches,
        "startedAt": datetime.now().isoformat(timespec="seconds"),
    }
    rounds: list[dict] = []
    capture = SerialCapture(args.port, args.baud, 0.5)
    try:
        for number in range(1, args.rounds + 1):
            lines, metrics = collect_round(
                capture,
                number,
                args.rounds,
                args.touches,
                args.boot_timeout,
                args.touch_timeout,
                args.settle_seconds,
            )
            metrics["round"] = number
            write_round(output / f"round-{number:02d}", lines, metrics)
            rounds.append(metrics)
            print(
                f"Round {number} result: {metrics['status']} "
                f"touches={metrics['physicalTouchDowns']} mutations={metrics['jsMutations']} "
                f"rejected={metrics['inputRejected']} "
                f"present_failures={metrics['presentFailures']}",
                flush=True,
            )
    except KeyboardInterrupt:
        print("Interrupted; preserving completed rounds.", file=sys.stderr)
    finally:
        capture.close()

    metadata["completedRounds"] = len(rounds)
    metadata["finishedAt"] = datetime.now().isoformat(timespec="seconds")
    status = "pass" if len(rounds) == args.rounds and all(item["status"] == "pass" for item in rounds) else "partial"
    summary = {**metadata, "rounds": rounds, "status": status}
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    write_report(output, metadata, rounds)
    print(f"\nSaved P3-1 collection to {output}")
    print(f"Overall status: {status}")
    return 0 if status == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
