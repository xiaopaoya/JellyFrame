#!/usr/bin/env python3
"""Run the physical JFDP/1 byte-stream acceptance matrix on one serial port."""

import argparse
import hashlib
import json
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError as error:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from error

HEADER = 24
MAX_PAYLOAD = 4096
RESPONSE = 1
TYPES = set(range(1, 13))


def frame(message_type, session, request, payload=b"", flags=0):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds JFDP/1 limit")
    return (b"JFDP" + bytes((1, message_type)) + struct.pack("<HIII", flags, session, request, len(payload)) +
            struct.pack("<I", zlib.crc32(payload) & 0xffffffff) + payload)


def decode(value):
    if len(value) < HEADER or value[:4] != b"JFDP" or value[4] != 1 or value[5] not in TYPES:
        raise AssertionError("invalid JFDP/1 header")
    flags, session, request, size, expected_crc = struct.unpack_from("<HIIII", value, 6)
    if size > MAX_PAYLOAD or len(value) != HEADER + size:
        raise AssertionError("invalid JFDP/1 length")
    payload = value[HEADER:]
    if (zlib.crc32(payload) & 0xffffffff) != expected_crc:
        raise AssertionError("invalid JFDP/1 crc")
    return {"type": value[5], "flags": flags, "session": session, "request": request, "payload": payload}


def vectors(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            name, hex_value = line.split("=", 1)
            result[name] = bytes.fromhex(hex_value)
    return result


def begin(transaction, bundle_size=4):
    app_id = b"org.jellyframe.transport"
    return bytes((1, 0, len(app_id), 0)) + struct.pack("<III", transaction, bundle_size, 0) + app_id


def chunk(transaction, offset=0, data=b"abcd"):
    return bytes((1, 0, len(data), 0)) + struct.pack("<II", transaction, offset) + data


def transaction(transaction_id):
    return bytes((1, 0, 0, 0)) + struct.pack("<I", transaction_id)


class Wire:
    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.serial = None
        self.pending = bytearray()
        self.capture = []
        self.counters = {"txFrames": 0, "rxFrames": 0, "timeouts": 0, "reconnects": 0}

    def open(self):
        # Configure modem lines before opening. Some USB-to-serial paths
        # interpret the default DTR/RTS transition as reset, which would mix
        # ROM output with a response capture.
        self.serial = serial.Serial()
        self.serial.port = self.port
        self.serial.baudrate = self.baud
        self.serial.timeout = 0.03
        self.serial.write_timeout = 2
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()
        time.sleep(0.3)
        self.pending.clear()
        # Discard reset-time ROM output before beginning a binary-only capture.
        # Once a case starts, receive() still rejects any non-JFDP byte.
        self.serial.reset_input_buffer()

    def close(self):
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def reconnect(self):
        self.close()
        time.sleep(0.08)
        self.open()
        self.counters["reconnects"] += 1

    def send(self, data, label, whole_frame=False):
        self.serial.write(data)
        self.serial.flush()
        self.capture.append({"direction": "tx", "label": label, "hex": bytes(data).hex()})
        if whole_frame:
            self.counters["txFrames"] += 1

    def receive(self, label, timeout=1.5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.serial.read(512)
            if data:
                self.pending.extend(data)
            if len(self.pending) >= HEADER:
                if self.pending[:4] != b"JFDP":
                    raise AssertionError(
                        "native USB endpoint contains non-JFDP bytes: " + bytes(self.pending[:64]).hex())
                length = struct.unpack_from("<I", self.pending, 16)[0]
                if length > MAX_PAYLOAD:
                    raise AssertionError("device emitted oversized JFDP/1 frame")
                total = HEADER + length
                if len(self.pending) >= total:
                    result = bytes(self.pending[:total])
                    del self.pending[:total]
                    self.capture.append({"direction": "rx", "label": label, "hex": result.hex()})
                    try:
                        decode(result)
                    except AssertionError as error:
                        raise AssertionError(f"{error}: {result.hex()}") from error
                    self.counters["rxFrames"] += 1
                    return result
        self.counters["timeouts"] += 1
        raise TimeoutError(label)

    def expect_no_response(self, label, timeout=0.04):
        try:
            self.receive(label, timeout)
        except TimeoutError:
            return
        raise AssertionError(f"unexpected response: {label}")

    def request(self, request, label):
        expected = decode(request)
        self.send(request, label, whole_frame=True)
        response = decode(self.receive(label))
        assert response["flags"] == RESPONSE
        assert response["type"] == expected["type"]
        assert response["session"] == expected["session"]
        assert response["request"] == expected["request"]
        return response


def run(results, name, callback):
    try:
        callback()
        results[name] = {"result": "pass"}
    except Exception as error:
        results[name] = {"result": "fail", "error": str(error)}
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fixture", default="tests/fixtures/jfdp_v1_wire_vectors.txt")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    fixture = Path(args.fixture)
    v = vectors(fixture)
    wire = Wire(args.port, args.baud)
    results = {}
    try:
        wire.open()
        discovery = v["frame-discovery"]
        capability = v["frame-capabilities-response"]

        def exact_vectors():
            assert wire.request(discovery, "discovery") ["payload"]
            assert wire.capture[-1]["hex"] == capability.hex()
            commit = frame(5, 0x0A0B0C0D, 0x01020306, transaction(0x11223344))
            wire.request(commit, "canonical-commit-response")
            assert wire.capture[-1]["hex"] == v["frame-install-commit-response"].hex()
        run(results, "exact_vectors", exact_vectors)

        def header_fragmentation():
            for boundary in (1, 4, 5, 6, 8, 12, 16, 20, 24):
                wire.send(discovery[:boundary], f"header-{boundary}-first")
                if boundary == len(discovery):
                    assert wire.receive(f"header-{boundary}") == capability
                    continue
                wire.expect_no_response(f"header-{boundary}-pending")
                wire.send(discovery[boundary:], f"header-{boundary}-last")
                assert wire.receive(f"header-{boundary}") == capability
            for index, byte in enumerate(discovery):
                wire.send(bytes((byte,)), f"one-byte-{index}")
                if index + 1 != len(discovery):
                    wire.expect_no_response(f"one-byte-{index}-pending", 0.01)
            assert wire.receive("one-byte-complete") == capability
        run(results, "header_fragmentation", header_fragmentation)

        def payload_fragmentation():
            for boundary in range(1, len(begin(1))):
                tx = 1000 + boundary
                request = frame(3, 0x2001, boundary, begin(tx))
                cut = HEADER + boundary
                wire.send(request[:cut], f"begin-{boundary}-first")
                wire.expect_no_response(f"begin-{boundary}-pending", 0.01)
                wire.send(request[cut:], f"begin-{boundary}-last")
                wire.receive(f"begin-{boundary}")
                wire.request(frame(6, 0x2001, 10000 + boundary, transaction(tx)), f"begin-abort-{boundary}")
            for boundary in range(1, len(chunk(1))):
                tx = 2000 + boundary
                wire.request(frame(3, 0x2002, 1, begin(tx)), f"chunk-begin-{boundary}")
                request = frame(4, 0x2002, 100 + boundary, chunk(tx))
                cut = HEADER + boundary
                wire.send(request[:cut], f"chunk-{boundary}-first")
                wire.expect_no_response(f"chunk-{boundary}-pending", 0.01)
                wire.send(request[cut:], f"chunk-{boundary}-last")
                wire.receive(f"chunk-{boundary}")
                wire.request(frame(6, 0x2002, 20000 + boundary, transaction(tx)), f"chunk-abort-{boundary}")
        run(results, "payload_fragmentation", payload_fragmentation)

        def coalescing():
            second = frame(3, 0x3001, 2, begin(3001))
            wire.send(frame(1, 0x3001, 1) + second, "coalesced-two-frames")
            assert decode(wire.receive("coalesced-discovery"))["request"] == 1
            assert decode(wire.receive("coalesced-begin"))["request"] == 2
            wire.request(frame(6, 0x3001, 3, transaction(3001)), "coalesced-abort")
        run(results, "coalescing", coalescing)

        def malformed():
            wire.request(frame(3, 0x4001, 1, begin(4001)), "bad-crc-begin")
            corrupt = bytearray(frame(4, 0x4001, 2, chunk(4001))); corrupt[-1] ^= 1
            wire.send(corrupt, "bad-crc")
            wire.expect_no_response("bad-crc-no-response", 0.2)
            wire.request(discovery, "bad-crc-recovery")
            oversize = b"JFDP" + bytes((1, 1)) + struct.pack("<HIII", 0, 5, 6, 4097) + struct.pack("<I", 0)
            wire.send(oversize, "oversize-length")
            wire.expect_no_response("oversize-no-response", 0.2)
            wire.request(discovery, "oversize-recovery")
            for offset, value in ((0, ord("X")), (4, 2), (5, 99)):
                invalid = bytearray(discovery); invalid[offset] = value
                wire.send(invalid, f"invalid-header-{offset}")
                wire.expect_no_response(f"invalid-header-{offset}-none", 0.2)
                wire.request(discovery, f"invalid-header-{offset}-recovery")
        run(results, "bad_crc_oversize_invalid_header", malformed)

        def truncation():
            wire.send(discovery[:23], "truncated-header")
            wire.reconnect(); time.sleep(0.6)
            wire.request(discovery, "truncated-header-recovery")
            wire.request(frame(3, 0x5001, 1, begin(5001)), "truncated-chunk-begin")
            partial = frame(4, 0x5001, 2, chunk(5001))[:-2]
            wire.send(partial, "truncated-chunk")
            wire.reconnect(); time.sleep(0.6)
            wire.request(discovery, "truncated-chunk-recovery")
        run(results, "truncation_reconnect", truncation)

        def repetition():
            for index in range(100):
                if index:
                    wire.reconnect()
                response = wire.request(frame(1, 0x6000 + index, index + 1), f"repeat-{index}")
                assert response["session"] == 0x6000 + index and response["request"] == index + 1
        run(results, "correlation_and_100_connect_cycles", repetition)
    finally:
        wire.close()
        (output / "wire_capture.json").write_text(json.dumps(wire.capture, indent=2), encoding="utf-8")
        summary = {"fixtureSha256": hashlib.sha256(fixture.read_bytes()).hexdigest(), "port": args.port,
                   "baud": args.baud, "cases": results, "hostCounters": wire.counters,
                   "result": "pass" if results and all(value["result"] == "pass" for value in results.values()) else "fail"}
        (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
