#!/usr/bin/env python3
"""Regression coverage for Device OS lifecycle command forwarding."""

from __future__ import annotations

import argparse
import contextlib
import io
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import jellyframe_cli  # noqa: E402


def provider_result(operation: str) -> dict[str, object]:
    return {
        "format": "jellyframe.device-provider",
        "formatVersion": 0,
        "kind": "result",
        "operation": operation,
        "requestId": "jf-test",
        "resultCode": "ok",
        "provider": {"id": "fixture", "version": "0.1"},
    }


class DeviceCliTests(unittest.TestCase):
    def command_args(self, command: str, **extra: object) -> argparse.Namespace:
        values: dict[str, object] = {
            "device_command": command,
            "provider": Path(r"C:\\fixtures\\jellyframe-device.exe"),
            "selector": "fixture-endpoint",
            "timeout": 30,
            "manifest": None,
            "app_id": "org.jellyframe.fixture",
            "keep_data": False,
            "allow_downgrade": False,
            "bundle": ROOT / "tests" / "fixtures" / "fixture.jfapp",
            "limit": 64,
            "transaction_id": 17,
        }
        values.update(extra)
        return argparse.Namespace(**values)

    def test_lifecycle_commands_forward_an_explicit_selector_and_app_id(self) -> None:
        for command in ("launch", "stop", "remove", "rollback"):
            with self.subTest(command=command):
                with patch("jellyframe_cli.device_provider_client.invoke_provider",
                           return_value=provider_result(command)) as invoke:
                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(jellyframe_cli.cmd_device(self.command_args(command)), 0)
                self.assertEqual(invoke.call_args.kwargs["selector"], "fixture-endpoint")
                self.assertEqual(invoke.call_args.kwargs["arguments"],
                                 ["--id", "org.jellyframe.fixture"])
                self.assertFalse(invoke.call_args.kwargs["stream"])

    def test_remove_forwards_keep_data_only_when_requested(self) -> None:
        with patch("jellyframe_cli.device_provider_client.invoke_provider",
                   return_value=provider_result("remove")) as invoke:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(jellyframe_cli.cmd_device(self.command_args("remove", keep_data=True)), 0)
        self.assertEqual(invoke.call_args.kwargs["arguments"],
                         ["--id", "org.jellyframe.fixture", "--keep-data"])

    def test_recovery_never_invents_an_app_or_transport_argument(self) -> None:
        with patch("jellyframe_cli.device_provider_client.invoke_provider",
                   return_value=provider_result("recovery")) as invoke:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(jellyframe_cli.cmd_device(self.command_args("recovery")), 0)
        self.assertEqual(invoke.call_args.kwargs["arguments"], [])
        self.assertEqual(invoke.call_args.kwargs["selector"], "fixture-endpoint")

    def test_help_exposes_the_full_lifecycle_without_a_provider(self) -> None:
        command = [sys.executable, str(ROOT / "tools" / "jellyframe_cli.py"), "device", "--help"]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        for operation in ("launch", "stop", "remove", "rollback", "recovery"):
            self.assertIn(operation, result.stdout)
        remove = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "jellyframe_cli.py"), "device", "--provider",
             r"C:\\fixtures\\jellyframe-device.exe", "remove", "--help"],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
        self.assertEqual(remove.returncode, 0, remove.stderr)
        self.assertIn("--keep-data", remove.stdout)


if __name__ == "__main__":
    unittest.main()
