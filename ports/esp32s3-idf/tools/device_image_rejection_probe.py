#!/usr/bin/env python3
"""Exercise bounded JFDP/1 rejection paths against the WS147 lifecycle image.

This is deliberately a raw protocol probe.  It complements provider fixtures
by preserving the actual device frames and by asserting that every rejected
request leaves the device's persistent AppList empty.
"""

import argparse
import hashlib
import json
import struct
import sys
import time
from pathlib import Path

import device_image_lifecycle_probe as p


APP_ID = "org.jellyframe.device.lifecycle"
MAX_BUNDLE_BYTES = 327680
RESULT_INVALID_REQUEST = 3


def expect_no_response(wire, label):
    try:
        wire.receive(label, timeout=0.25)
    except TimeoutError:
        return
    raise AssertionError(label + " unexpectedly received a response")


def raw_send(wire, bytes_, label):
    wire.serial.write(bytes_)
    wire.serial.flush()
    wire.capture.append({"direction": "tx", "label": label, "hex": bytes(bytes_).hex()})
    wire.counters["txFrames"] += 1


def assert_empty(wire, request_id, label):
    listed = p.decode_list(wire.request(p.APP_LIST, 0x9400, request_id, label=label))
    assert listed["entries"] == []
    return listed["generation"]


def begin_with_size(transaction_id, bundle_bytes):
    app_id = APP_ID.encode("utf-8")
    return struct.pack("<BBBBIII", 1, 0, len(app_id), 0, transaction_id, bundle_bytes, 0) + app_id


def run_case(cases, name, callback):
    try:
        callback()
        cases[name] = {"result": "pass"}
    except Exception as error:
        cases[name] = {"result": "fail", "error": str(error)}
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[3]
    fixture = p.package_fixture(repo, output, 1, "1.0.0")
    malformed = bytearray(fixture)
    malformed[-1] ^= 0x80
    malformed_path = output / "lifecycle-malformed.jfapp"
    malformed_path.write_bytes(malformed)

    wire = p.Wire(args.port, args.baud)
    cases = {}
    try:
        wire.open()
        generation = assert_empty(wire, 1, "initial-empty")

        def bad_transport_crc():
            good = p.frame(p.INSTALL_BEGIN, 0x9400, 10, begin_with_size(9001, 1))
            corrupt = bytearray(good)
            corrupt[-1] ^= 1
            raw_send(wire, corrupt, "bad-transport-crc")
            expect_no_response(wire, "bad-transport-crc-no-response")
            assert assert_empty(wire, 11, "bad-transport-crc-recovery") == generation
        run_case(cases, "bad_transport_crc_no_publish", bad_transport_crc)

        def oversized_frame():
            oversized = (b"JFDP" + bytes((1, p.APP_LIST)) +
                         struct.pack("<HIII", 0, 0x9400, 20, 4097) + struct.pack("<I", 0))
            raw_send(wire, oversized, "oversized-jfdp-length")
            expect_no_response(wire, "oversized-jfdp-no-response")
            assert assert_empty(wire, 21, "oversized-jfdp-recovery") == generation
        run_case(cases, "oversized_frame_no_publish", oversized_frame)

        def oversized_bundle():
            result = p.decode_result(wire.request(
                p.INSTALL_BEGIN, 0x9400, 30, begin_with_size(9002, MAX_BUNDLE_BYTES + 1),
                "oversized-bundle-begin"))
            # This protocol-level size violation is rejected by the shared
            # DeviceInstallTransaction limit gate before storage is touched.
            # It is therefore InvalidRequest, not StorageFull.
            assert result["code"] == RESULT_INVALID_REQUEST
            assert assert_empty(wire, 31, "oversized-bundle-recovery") == generation
        run_case(cases, "oversized_bundle_typed_rejection", oversized_bundle)

        def malformed_bundle():
            result = p.install(wire, 40, 9003, APP_ID, bytes(malformed))
            assert result["code"] == p.RESULT_INTEGRITY_FAILED
            assert assert_empty(wire, 41, "malformed-bundle-recovery") == generation
        run_case(cases, "malformed_bundle_integrity_no_publish", malformed_bundle)

        def abort_partial():
            begin = p.decode_result(wire.request(
                p.INSTALL_BEGIN, 0x9400, 50, p.begin_payload(9004, APP_ID, fixture), "partial-begin"))
            assert begin["code"] == p.RESULT_ACCEPTED
            accepted = p.decode_result(wire.request(
                p.INSTALL_CHUNK, 0x9400, 51, p.chunk_payload(9004, 0, fixture[:128]), "partial-chunk"))
            assert accepted["code"] == p.RESULT_ACCEPTED
            aborted = p.decode_result(wire.request(
                p.INSTALL_ABORT, 0x9400, 52, p.transaction_payload(9004), "partial-abort"))
            assert aborted["code"] == p.RESULT_CANCELLED
            assert assert_empty(wire, 53, "partial-abort-recovery") == generation
        run_case(cases, "explicit_abort_no_publish", abort_partial)

        def truncated_reconnect():
            partial = p.frame(p.INSTALL_BEGIN, 0x9400, 60, p.begin_payload(9005, APP_ID, fixture))[:-3]
            raw_send(wire, partial, "truncated-install-begin")
            wire.reconnect()
            time.sleep(0.6)
            assert assert_empty(wire, 61, "truncated-install-recovery") == generation
        run_case(cases, "truncated_reconnect_no_publish", truncated_reconnect)
    finally:
        wire.close()
        (output / "jfdp_capture.json").write_text(json.dumps(wire.capture, indent=2), encoding="utf-8")
        hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in output.glob("*.jfapp")}
        summary = {"port": args.port, "baud": args.baud, "fixtureSha256": hashes,
                   "cases": cases, "hostCounters": wire.counters,
                   "result": "pass" if cases and all(case["result"] == "pass" for case in cases.values()) else "fail"}
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
