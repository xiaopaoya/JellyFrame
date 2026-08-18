#!/usr/bin/env python3
"""Verify one preconfigured Developer Image lifecycle fault point on hardware."""

import argparse
import json
import sys
import time
from pathlib import Path

import device_image_lifecycle_probe as p


APP_ID = "org.jellyframe.device.lifecycle"


def write_summary(output, wire, point, cases):
    summary = {
        "faultPoint": point,
        "hostCounters": wire.counters,
        "cases": cases,
        "result": "pass" if all(item["result"] == "pass" for item in cases.values()) else "fail",
    }
    (output / "jfdp_capture.json").write_text(json.dumps(wire.capture, indent=2), encoding="utf-8")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def expect_restart(wire, message_type, request_id, payload, label):
    try:
        wire.request(message_type, 0x9300, request_id, payload, label)
    except (TimeoutError, OSError):
        wire.close()
        time.sleep(0.8)
        wire.open()
        return
    raise AssertionError(label + " unexpectedly returned a response")


def assert_active(wire, request_id, version_code):
    listed = p.decode_list(wire.request(p.APP_LIST, 0x9300, request_id, label="post-fault-list"))
    assert len(listed["entries"]) == 1 and listed["entries"][0]["versionCode"] == version_code
    launched = p.decode_result(wire.request(p.LAUNCH, 0x9300, request_id + 1,
                                             p.app_id_payload(APP_ID), "post-fault-launch"))
    assert launched["code"] == p.RESULT_OK


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--point", required=True, type=int, choices=range(1, 9))
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[3]
    first = p.package_fixture(repo, output, 1, "1.0.0")
    second = p.package_fixture(repo, output, 2, "2.0.0")
    wire = p.Wire(args.port, 115200)
    cases = {}
    try:
        wire.open()
        if args.point == 7:
            entries = p.decode_list(wire.request(p.APP_LIST, 0x9300, 1, label="corrupt-registry-list"))["entries"]
            recovery = p.decode_recovery(wire.request(p.RECOVERY, 0x9300, 2, label="corrupt-registry-recovery"))
            assert entries == [] and recovery["reason"] == 1
            cases["corrupt_registry_protected_launcher"] = {"result": "pass"}
        else:
            assert_active(wire, 1, 1)
            begin = p.begin_payload(8000 + args.point, APP_ID, second)
            if args.point == 1:
                expect_restart(wire, p.INSTALL_BEGIN, 10, begin, "fault-after-begin")
                assert_active(wire, 20, 1)
            else:
                accepted = p.decode_result(wire.request(p.INSTALL_BEGIN, 0x9300, 10, begin, "fault-begin"))
                assert accepted["code"] == p.RESULT_ACCEPTED
                request = 11
                chunks = [(offset, second[offset:offset + 512]) for offset in range(0, len(second), 512)]
                if args.point == 2:
                    expect_restart(wire, p.INSTALL_CHUNK, request,
                                   p.chunk_payload(8000 + args.point, *chunks[0]), "fault-after-first-chunk")
                    assert_active(wire, 20, 1)
                else:
                    for offset, bytes_ in chunks[:-1]:
                        result = p.decode_result(wire.request(p.INSTALL_CHUNK, 0x9300, request,
                                                              p.chunk_payload(8000 + args.point, offset, bytes_),
                                                              "fault-chunk"))
                        assert result["code"] == p.RESULT_ACCEPTED
                        request += 1
                    if args.point == 3:
                        expect_restart(wire, p.INSTALL_CHUNK, request,
                                       p.chunk_payload(8000 + args.point, *chunks[-1]), "fault-after-last-chunk")
                        assert_active(wire, 20, 1)
                    else:
                        offset, bytes_ = chunks[-1]
                        result = p.decode_result(wire.request(p.INSTALL_CHUNK, 0x9300, request,
                                                              p.chunk_payload(8000 + args.point, offset, bytes_),
                                                              "fault-last-chunk"))
                        assert result["code"] == p.RESULT_ACCEPTED
                        request += 1
                        commit = p.transaction_payload(8000 + args.point)
                        if args.point in (4, 5, 6):
                            expect_restart(wire, p.INSTALL_COMMIT, request, commit, "fault-commit")
                            assert_active(wire, 20, 2 if args.point == 6 else 1)
                            if args.point == 6:
                                rollback = p.decode_result(wire.request(p.ROLLBACK, 0x9300, 30,
                                                                        p.app_id_payload(APP_ID), "restore-a"))
                                assert rollback["code"] == p.RESULT_OK
                        else:
                            result = p.decode_result(wire.request(p.INSTALL_COMMIT, 0x9300, request,
                                                                  commit, "fault-reject-publish"))
                            assert result["code"] == p.RESULT_INTEGRITY_FAILED or result["code"] == 14
                            assert_active(wire, 20, 1)
            cases["fault_point_%d" % args.point] = {"result": "pass"}
    except Exception as error:
        cases["fault_point_%d" % args.point] = {"result": "fail", "error": str(error)}
    finally:
        wire.close()
        summary = write_summary(output, wire, args.point, cases)
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
