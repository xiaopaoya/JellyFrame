#!/usr/bin/env python3
"""Prove installed-App load failure returns to the WS147 protected launcher."""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import device_image_lifecycle_probe as p


APP_ID = "org.jellyframe.device.load-failure"


def package_fixture(repo, output):
    source = output / "load-failure-source"
    if source.exists():
        shutil.rmtree(source)
    source.mkdir(parents=True)
    manifest = {
        "format": "jellyframe.app", "formatVersion": 0,
        "id": APP_ID, "name": "Load Failure", "version": {"name": "1.0.0", "code": 1},
        "entry": "/index.html", "runtime": {"minJellyFrame": "0.6.0", "minRenderCore": "0.6.2", "script": "none"},
        "viewport": {"designWidth": 172, "designHeight": 320, "shape": "rect"},
        "budgets": {"maxResourceBytes": 4096, "maxDomNodes": 16, "maxCssRules": 4,
                    "maxDisplayCommands": 16, "maxTimers": 0, "maxEventListeners": 0},
        "capabilities": [],
        "targets": {"ws147": {"viewport": {"width": 172, "height": 320, "shape": "rect"},
                              "fontProfile": "tiny-plus-symbols", "output": "jfapp"}},
    }
    (source / "jellyframe.app.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    # This is a valid bundle whose entry HTML deliberately asks the installed
    # resource bridge for a non-local stylesheet. Launch must fail safely at
    # the bounded resolver and route to the protected launcher.
    (source / "index.html").write_text(
        "<!doctype html><html><head><link rel='stylesheet' href='https://invalid.example/blocked.css'></head>"
        "<body><main>LOAD-FAILURE</main></body></html>\n", encoding="utf-8")
    bundle = output / "load-failure.jfapp"
    report = output / "load-failure.package.json"
    completed = subprocess.run([sys.executable, str(repo / "tools" / "package_app.py"), "--root", str(source),
                                "--output-bundle", str(bundle), "--report", str(report)], cwd=repo,
                               text=True, capture_output=True, check=False)
    (output / "load-failure.package.log").write_text(completed.stdout + completed.stderr, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError("fixture package failed: " + completed.stdout + completed.stderr)
    return bundle.read_bytes(), bundle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[3]
    wire = p.Wire(args.port, 115200)
    cases = {}
    try:
        bundle, bundle_path = package_fixture(repo, output)
        wire.open()
        before = p.decode_list(wire.request(p.APP_LIST, 0x9500, 1, label="before-list"))
        assert before["entries"] == []
        installed = p.install(wire, 10, 9501, APP_ID, bundle)
        assert installed["code"] == p.RESULT_ACCEPTED
        listed = p.decode_list(wire.request(p.APP_LIST, 0x9500, 11, label="installed-list"))
        assert len(listed["entries"]) == 1 and listed["entries"][0]["appId"] == APP_ID
        launch = p.decode_result(wire.request(p.LAUNCH, 0x9500, 12, p.app_id_payload(APP_ID), "load-failure-launch"))
        assert launch["code"] != p.RESULT_OK
        recovery = p.decode_recovery(wire.request(p.RECOVERY, 0x9500, 13, label="load-failure-recovery"))
        assert recovery["reason"] == 3 and recovery["appId"] == APP_ID
        assert recovery["flags"] & 1 and recovery["flags"] & 2
        removed = p.decode_result(wire.request(p.REMOVE, 0x9500, 14, p.app_id_payload(APP_ID), "load-failure-remove"))
        assert removed["code"] == p.RESULT_OK
        assert p.decode_list(wire.request(p.APP_LIST, 0x9500, 15, label="after-remove-list"))["entries"] == []
        cases["installed_app_load_failure_to_protected_launcher"] = {"result": "pass"}
    except Exception as error:
        cases["installed_app_load_failure_to_protected_launcher"] = {"result": "fail", "error": str(error)}
    finally:
        wire.close()
        (output / "jfdp_capture.json").write_text(json.dumps(wire.capture, indent=2), encoding="utf-8")
        hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in output.glob("*.jfapp")}
        summary = {"port": args.port, "fixtureSha256": hashes, "cases": cases, "hostCounters": wire.counters,
                   "result": "pass" if cases and all(case["result"] == "pass" for case in cases.values()) else "fail"}
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
