#!/usr/bin/env python3
"""Regression coverage for the bounded Device OS provider client."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import device_provider_client


def result(operation: str, request_id: str, result_code: str = "ok") -> bytes:
    device = ""
    if operation != "discover" and result_code in {"ok", "accepted"}:
        device = (
            ',"device":{"endpointId":"fixture-endpoint","boardId":"ws147",'
            '"profileId":"rect-172x320","imageVersion":"0.1.0-dev",'
            '"runtimeVersion":"0.6.0-dev","protocol":"JFDP/1","connected":true,'
            '"capabilities":{"display":{"width":172,"height":320,"shape":"rect"},'
            '"featureFamilies":["core.document"],"maxBundleBytes":1048576,'
            '"availableStorageBytes":524288}}'
        )
    return (
        '{"format":"jellyframe.device-provider","formatVersion":0,'
        f'"kind":"result","operation":"{operation}","requestId":"{request_id}",'
        f'"resultCode":"{result_code}","provider":{{"id":"test","version":"0.1"}}{device}}}'
    ).encode("utf-8")


class DeviceProviderClientTests(unittest.TestCase):
    def test_requires_an_absolute_existing_provider_path(self) -> None:
        with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "absolute"):
            device_provider_client.invoke_provider(Path("provider"), "discover")

    def test_invocation_is_shell_free_and_correlated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "provider.exe"
            provider.write_bytes(b"")
            completed = subprocess.CompletedProcess([], 0, result("discover", "jf-test"), b"")
            with patch("device_provider_client.subprocess.run", return_value=completed) as run:
                response = device_provider_client.invoke_provider(
                    provider, "discover", request_id="jf-test"
                )
            self.assertEqual(response["resultCode"], "ok")
            command = run.call_args.args[0]
            self.assertEqual(command, [str(provider), "--output", "json", "--request-id", "jf-test", "discover"])
            self.assertFalse(run.call_args.kwargs["shell"])

    def test_rejects_a_mismatched_result_and_conflicting_exit_status(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "provider.exe"
            provider.write_bytes(b"")
            mismatched = subprocess.CompletedProcess([], 0, result("launch", "jf-test"), b"")
            with patch("device_provider_client.subprocess.run", return_value=mismatched):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "does not match"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")

    def test_stream_returns_ordered_progress_and_terminal_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "provider.exe"
            provider.write_bytes(b"")
            stream = (
                b'{"format":"jellyframe.device-provider","formatVersion":0,"kind":"progress",'
                b'"operation":"install","requestId":"jf-test","sequence":1,'
                b'"provider":{"id":"test","version":"0.1"},"progress":{"completedBytes":4,"totalBytes":8}}\n'
                + result("install", "jf-test").replace(b'"kind":"result",', b'"kind":"result","sequence":2,')
            )
            completed = subprocess.CompletedProcess([], 0, stream, b"")
            with patch("device_provider_client.subprocess.run", return_value=completed) as run:
                events = device_provider_client.invoke_provider(
                    provider, "install", stream=True, request_id="jf-test"
                )
            self.assertEqual([event["kind"] for event in events], ["progress", "result"])
            self.assertEqual(run.call_args.args[0][2], "jsonl")

    def test_stream_accepts_bounded_log_events(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            provider = Path(directory) / "provider.exe"
            provider.write_bytes(b"")
            stream = (
                b'{"format":"jellyframe.device-provider","formatVersion":0,"kind":"log",'
                b'"operation":"logs","requestId":"jf-test","sequence":1,'
                b'"provider":{"id":"test","version":"0.1"},'
                b'"log":{"level":"info","appId":"org.example.app","generation":7,'
                b'"timestampMs":"123456789","message":"started"}}\n'
                + result("logs", "jf-test").replace(
                    b'"kind":"result",',
                    b'"kind":"result","sequence":2,"logSummary":{"returnedRecords":1,"droppedRecords":0},')
            )
            completed = subprocess.CompletedProcess([], 0, stream, b"")
            with patch("device_provider_client.subprocess.run", return_value=completed):
                events = device_provider_client.invoke_provider(
                    provider, "logs", stream=True, request_id="jf-test"
                )
            self.assertEqual(events[0]["log"]["message"], "started")
            conflicting = subprocess.CompletedProcess([], 1, result("discover", "jf-test"), b"")
            with patch("device_provider_client.subprocess.run", return_value=conflicting):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "exit status"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")

            failed_with_success_exit = subprocess.CompletedProcess(
                [], 0, result("discover", "jf-test", "storage-full"), b""
            )
            with patch("device_provider_client.subprocess.run", return_value=failed_with_success_exit):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "storage-full"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")

            unavailable_with_wrong_exit = subprocess.CompletedProcess(
                [], 1, result("discover", "jf-test", "transport-unavailable"), b""
            )
            with patch("device_provider_client.subprocess.run", return_value=unavailable_with_wrong_exit):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "expected 3"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")


if __name__ == "__main__":
    unittest.main()
