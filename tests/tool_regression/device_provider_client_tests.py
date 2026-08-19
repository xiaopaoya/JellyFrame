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
    return (
        '{"format":"jellyframe.device-provider","formatVersion":0,'
        f'"kind":"result","operation":"{operation}","requestId":"{request_id}",'
        f'"resultCode":"{result_code}","provider":{{"id":"test","version":"0.1"}}}}'
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
            mismatched = subprocess.CompletedProcess([], 0, result("info", "jf-test"), b"")
            with patch("device_provider_client.subprocess.run", return_value=mismatched):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "does not match"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")
            conflicting = subprocess.CompletedProcess([], 1, result("discover", "jf-test"), b"")
            with patch("device_provider_client.subprocess.run", return_value=conflicting):
                with self.assertRaisesRegex(device_provider_client.DeviceProviderClientError, "exit status"):
                    device_provider_client.invoke_provider(provider, "discover", request_id="jf-test")


if __name__ == "__main__":
    unittest.main()
