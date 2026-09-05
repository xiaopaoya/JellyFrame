#!/usr/bin/env python3
"""Collect WS147 security/robustness R1 and R2 evidence through the real provider.

The rollback corruption is deliberately limited to a saved 4 KiB sector in the
rollback slot. The active slot and both registry copies are never modified.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path


STORAGE_OFFSET = 0xE10000
STORAGE_BYTES = 0x100000
REGISTRY_BYTES = 4096
REGISTRY_COPIES = 2
BUNDLE_SLOTS = 3
SLOT_BYTES = ((STORAGE_BYTES - REGISTRY_BYTES * REGISTRY_COPIES) // BUNDLE_SLOTS) & ~0xFFF
ACTIVE_SLOT_OFFSET = 12
ROLLBACK_SLOT_OFFSET = ACTIVE_SLOT_OFFSET + 176


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run_logged(output: Path, label: str, command: list[str], expected: int = 0) -> dict:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (output / f"{label}.stdout.log").write_text(completed.stdout, encoding="utf-8")
    (output / f"{label}.stderr.log").write_text(completed.stderr, encoding="utf-8")
    result = {"label": label, "command": command, "returnCode": completed.returncode}
    try:
        result["json"] = json.loads(completed.stdout.splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        pass
    if completed.returncode != expected:
        raise RuntimeError(f"{label} returned {completed.returncode}, expected {expected}")
    return result


def package_fixture(repo: Path, output: Path, stem: str, app_id: str, version_code: int,
                    version_name: str, marker: str, entry_bytes: int) -> Path:
    root = output / "fixtures" / stem
    root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "jellyframe.app", "formatVersion": 0,
        "id": app_id, "name": stem,
        "version": {"name": version_name, "code": version_code}, "entry": "/index.html",
        "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.6.2", "script": "none"},
        "viewport": {"designWidth": 172, "designHeight": 320, "shape": "rect"},
        "budgets": {"maxResourceBytes": 16384, "maxDomNodes": 32, "maxCssRules": 8,
                    "maxDisplayCommands": 32, "maxTimers": 0, "maxEventListeners": 0},
        "capabilities": [],
        "targets": {"ws147": {"viewport": {"width": 172, "height": 320, "shape": "rect"},
                               "fontProfile": "tiny-plus-symbols", "output": "jfapp"}},
    }
    filler = (" entry-resource-boundary" * ((entry_bytes // 24) + 1))[:entry_bytes]
    entry = ("<!doctype html><html><head><style>body{margin:0;padding:12px;background:#101820;"
             "color:#f5f7fa;font-size:16px}main{border:1px solid #4ecdc4;padding:8px}</style></head><body>"
             f"<main id='launch-marker'>{marker}{filler}</main></body></html>\n")
    (root / "jellyframe.app.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (root / "index.html").write_text(entry, encoding="utf-8")
    bundle = output / "fixtures" / f"{stem}.jfapp"
    report = output / "fixtures" / f"{stem}.package.json"
    run_logged(output, f"package-{stem}", [sys.executable, str(repo / "tools" / "package_app.py"),
                                            "--root", str(root), "--output-bundle", str(bundle),
                                            "--report", str(report)])
    actual_entry = (root / "index.html").stat().st_size
    if actual_entry <= entry_bytes:
        raise RuntimeError(f"{stem} entry did not reach requested resource boundary")
    return bundle


def provider_command(provider: Path, config: Path, request_id: str, operation: str, *args: str) -> list[str]:
    # argparse keeps provider-wide options before the operation subcommand.
    tail = list(args)
    selector: list[str] = []
    if "--selector" in tail:
        index = tail.index("--selector")
        selector = tail[index:index + 2]
        del tail[index:index + 2]
    output_mode = "jsonl" if operation == "install" else "json"
    return [sys.executable, str(provider), "--output", output_mode, "--request-id", request_id,
            "--config", str(config), *selector, operation, *tail]


def provider_result(output: Path, label: str, provider: Path, config: Path, request_id: str,
                    operation: str, *args: str, expected: int = 0) -> dict:
    result = run_logged(output, label, provider_command(provider, config, request_id, operation, *args), expected)
    payload = result.get("json", {})
    if payload.get("resultCode") not in {"ok", "accepted"}:
        raise RuntimeError(f"{label} provider result was {payload.get('resultCode')}")
    return payload


def provider_rejection(output: Path, label: str, provider: Path, config: Path, request_id: str,
                       operation: str, *args: str) -> dict:
    result = run_logged(output, label, provider_command(provider, config, request_id, operation, *args), 1)
    payload = result.get("json", {})
    if payload.get("resultCode") != "integrity-failed":
        raise RuntimeError(f"{label} expected integrity-failed, got {payload.get('resultCode')}")
    return payload


def hard_reset(output: Path, port: str, label: str) -> None:
    run_logged(output, label, [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                               "--before", "default_reset", "--after", "hard_reset", "chip_id"])
    time.sleep(1.2)


def read_registry(output: Path, port: str, label: str) -> tuple[int, int]:
    capture = output / f"{label}.bin"
    run_logged(output, f"{label}-read", [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                           "read_flash", hex(STORAGE_OFFSET), hex(REGISTRY_BYTES), str(capture)])
    raw = capture.read_bytes()
    if len(raw) != REGISTRY_BYTES:
        raise RuntimeError("registry capture is truncated")
    active, rollback = raw[ACTIVE_SLOT_OFFSET], raw[ROLLBACK_SLOT_OFFSET]
    if active >= BUNDLE_SLOTS or rollback >= BUNDLE_SLOTS or active == rollback:
        raise RuntimeError(f"invalid active/rollback slots: {active}/{rollback}")
    return active, rollback


def corrupt_rollback_sector(output: Path, port: str, rollback_slot: int) -> tuple[Path, int]:
    sector = STORAGE_OFFSET + REGISTRY_BYTES * REGISTRY_COPIES + rollback_slot * SLOT_BYTES
    backup = output / "r2-rollback-sector-original.bin"
    run_logged(output, "r2-rollback-sector-read", [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                                      "read_flash", hex(sector), "0x1000", str(backup)])
    raw = backup.read_bytes()
    if len(raw) != 4096 or raw[:8] != b"JFAPPV0\0":
        raise RuntimeError("rollback sector does not start with the expected JFAPPV0 header")
    corrupt = bytes((raw[0] & 0xFE,))
    if corrupt == raw[:1]:
        corrupt = bytes((raw[0] & 0xFD,))
    byte_file = output / "r2-rollback-header-corrupt.bin"
    byte_file.write_bytes(corrupt)
    run_logged(output, "r2-rollback-corrupt", [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                                 "write_flash", hex(sector), str(byte_file)])
    return backup, sector


def restore_rollback_sector(output: Path, port: str, backup: Path, sector: int) -> None:
    run_logged(output, "r2-rollback-sector-erase", [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                                       "erase_region", hex(sector), "0x1000"])
    run_logged(output, "r2-rollback-sector-restore", [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port,
                                                         "write_flash", hex(sector), str(backup)])


def listed_version(value: dict, app_id: str) -> int:
    apps = value.get("apps", [])
    if len(apps) != 1 or apps[0].get("appId") != app_id:
        raise RuntimeError("app library did not contain exactly the expected installed app")
    return int(apps[0]["versionCode"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    output.mkdir(parents=True)
    provider = repo / "ports" / "esp32s3-idf" / "tools" / "device_provider" / "jellyframe_device.py"
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    firmware = args.firmware.resolve()
    manifest = {
        "format": "jellyframe.device-image", "formatVersion": 0,
        "imageId": "org.jellyframe.ws147.developer", "imageVersion": "0.6.2-ws147.1",
        "runtimeVersion": "0.6.0-dev", "renderCore": {"version": "0.6.2", "abi": 1},
        "source": {"revision": revision, "firmwareSha256": sha256(firmware)},
        "board": {"id": "ws147", "display": {"width": 172, "height": 320, "shape": "rect"}},
        "profile": {"id": "rect-172x320", "featureFamilies": ["core.document", "core.paint",
                    "css.flex-grid", "css.modern-paint", "forms.advanced", "graphics.canvas2d"]},
        "transport": {"protocol": "JFDP/1", "kind": "usb-serial-jtag"},
        "storage": {"maxBundleBytes": 327680},
        "recovery": {"procedureId": "ws147-usb-recovery-v1", "factoryImageSha256": "0" * 64},
    }
    manifest_path = output / "ws147-security-developer-image.manifest.json"
    config_path = output / "jellyframe-device.config.json"
    write_json(manifest_path, manifest)
    write_json(config_path, {"endpointId": "ws147-security-local", "port": args.port, "baud": 115200,
                             "manifest": str(manifest_path)})
    large_768 = package_fixture(repo, output, "large-entry-768", "org.jellyframe.security.large768", 1,
                                "1.0.0", "SECURITY-LARGE-768", 768)
    large_3k = package_fixture(repo, output, "large-entry-3072", "org.jellyframe.security.large3072", 1,
                               "1.0.0", "SECURITY-LARGE-3072", 3072)
    rollback_a = package_fixture(repo, output, "rollback-A", "org.jellyframe.security.rollback", 1,
                                 "1.0.0", "SECURITY-ROLLBACK-A", 1024)
    rollback_b = package_fixture(repo, output, "rollback-B", "org.jellyframe.security.rollback", 2,
                                 "2.0.0", "SECURITY-ROLLBACK-B", 2048)
    fixture_data = {path.name: {"sha256": sha256(path), "bytes": path.stat().st_size}
                    for path in (large_768, large_3k, rollback_a, rollback_b)}
    write_json(output / "fixture_hashes.json", fixture_data)

    checks: dict[str, object] = {}
    provider_result(output, "r1-discover", provider, config_path, "r1-discover", "discover")
    provider_result(output, "r1-info", provider, config_path, "r1-info", "info", "--selector", "ws147-security-local")
    for index, bundle in enumerate((large_768, large_3k), 1):
        app_id = "org.jellyframe.security.large768" if index == 1 else "org.jellyframe.security.large3072"
        provider_result(output, f"r1-{index}-install", provider, config_path, f"r1-{index}-install", "install",
                        "--selector", "ws147-security-local", "--bundle", str(bundle))
        provider_result(output, f"r1-{index}-launch", provider, config_path, f"r1-{index}-launch", "launch",
                        "--selector", "ws147-security-local", "--id", app_id)
        provider_result(output, f"r1-{index}-stop", provider, config_path, f"r1-{index}-stop", "stop",
                        "--selector", "ws147-security-local", "--id", app_id)
        hard_reset(output, args.port, f"r1-{index}-hard-reset")
        listed = provider_result(output, f"r1-{index}-list-after-reset", provider, config_path,
                                 f"r1-{index}-list-after-reset", "list", "--selector", "ws147-security-local")
        if not any(app.get("appId") == app_id and app.get("versionCode") == 1 for app in listed.get("apps", [])):
            raise RuntimeError(f"R1 fixture {app_id} was not persisted across reset")
        provider_result(output, f"r1-{index}-launch-after-reset", provider, config_path,
                        f"r1-{index}-launch-after-reset", "launch", "--selector", "ws147-security-local",
                        "--id", app_id)
        provider_result(output, f"r1-{index}-remove", provider, config_path, f"r1-{index}-remove", "remove",
                        "--selector", "ws147-security-local", "--id", app_id)
    checks["largeEntryLaunch"] = {"status": "pass", "cases": 2}

    rollback_id = "org.jellyframe.security.rollback"
    provider_result(output, "r2-install-a", provider, config_path, "r2-install-a", "install",
                    "--selector", "ws147-security-local", "--bundle", str(rollback_a))
    provider_result(output, "r2-launch-a", provider, config_path, "r2-launch-a", "launch",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    provider_result(output, "r2-stop-a", provider, config_path, "r2-stop-a", "stop",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    provider_result(output, "r2-install-b", provider, config_path, "r2-install-b", "install",
                    "--selector", "ws147-security-local", "--bundle", str(rollback_b))
    provider_result(output, "r2-launch-b", provider, config_path, "r2-launch-b", "launch",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    provider_result(output, "r2-stop-b", provider, config_path, "r2-stop-b", "stop",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    before = provider_result(output, "r2-list-before-corruption", provider, config_path, "r2-list-before-corruption",
                             "list", "--selector", "ws147-security-local")
    if listed_version(before, rollback_id) != 2:
        raise RuntimeError("R2 active version was not B before corruption")
    _active_slot, rollback_slot = read_registry(output, args.port, "r2-registry-before-corruption")
    backup, sector = corrupt_rollback_sector(output, args.port, rollback_slot)
    hard_reset(output, args.port, "r2-hard-reset-after-corruption")
    provider_rejection(output, "r2-corrupt-rollback", provider, config_path, "r2-corrupt-rollback", "rollback",
                       "--selector", "ws147-security-local", "--id", rollback_id)
    preserved = provider_result(output, "r2-list-active-preserved", provider, config_path, "r2-list-active-preserved",
                                "list", "--selector", "ws147-security-local")
    if listed_version(preserved, rollback_id) != 2:
        raise RuntimeError("corrupt rollback replaced active B")
    provider_result(output, "r2-launch-active-b-preserved", provider, config_path, "r2-launch-active-b-preserved",
                    "launch", "--selector", "ws147-security-local", "--id", rollback_id)
    provider_result(output, "r2-stop-active-b-preserved", provider, config_path, "r2-stop-active-b-preserved",
                    "stop", "--selector", "ws147-security-local", "--id", rollback_id)
    restore_rollback_sector(output, args.port, backup, sector)
    hard_reset(output, args.port, "r2-hard-reset-after-restore")
    provider_result(output, "r2-normal-rollback", provider, config_path, "r2-normal-rollback", "rollback",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    restored = provider_result(output, "r2-list-normal-rollback", provider, config_path, "r2-list-normal-rollback",
                               "list", "--selector", "ws147-security-local")
    if listed_version(restored, rollback_id) != 1:
        raise RuntimeError("normal rollback did not restore A")
    provider_result(output, "r2-launch-restored-a", provider, config_path, "r2-launch-restored-a", "launch",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    provider_result(output, "r2-stop-restored-a", provider, config_path, "r2-stop-restored-a", "stop",
                    "--selector", "ws147-security-local", "--id", rollback_id)
    checks["rollbackIntegrity"] = {"status": "pass", "corruptRejected": 1, "activePreserved": 1}
    write_json(output / "r1_r2_summary.json", {"checks": checks, "fixtureSha256": fixture_data,
                                                  "slotBytes": SLOT_BYTES, "rollbackSector": hex(sector)})
    print(json.dumps({"result": "pass", **checks}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
