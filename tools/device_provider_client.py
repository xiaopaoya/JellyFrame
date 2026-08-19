"""Bounded host-side invocation for an explicitly configured Device OS provider."""

from __future__ import annotations

import subprocess
import uuid
from pathlib import Path
from typing import Any

from device_provider_contract import ProviderContractError, parse_provider_jsonl, parse_provider_result


DEFAULT_TIMEOUT_SECONDS = 30
MAX_PROVIDER_OUTPUT_BYTES = 256 * 1024


class DeviceProviderClientError(RuntimeError):
    pass


def new_request_id() -> str:
    return f"jf-{uuid.uuid4().hex}"


def _provider_path(value: Path) -> Path:
    path = value.expanduser()
    if not path.is_absolute() or not path.is_file():
        raise DeviceProviderClientError("provider must be an existing absolute file path")
    return path


def _expected_exit_status(result_code: str) -> int:
    if result_code in {"ok", "accepted"}:
        return 0
    if result_code == "invalid-request":
        return 2
    if result_code == "transport-unavailable":
        return 3
    if result_code == "protocol-mismatch":
        return 4
    if result_code == "provider-failed":
        return 5
    return 1


def invoke_provider(
    provider: Path,
    operation: str,
    *,
    selector: str | None = None,
    arguments: list[str] | None = None,
    stream: bool = False,
    timeout_seconds: int = DEFAULT_TIMEOUT_SECONDS,
    request_id: str | None = None,
) -> dict[str, Any] | list[dict[str, Any]]:
    """Invoke one provider operation without shell parsing or endpoint inference."""
    executable = _provider_path(provider)
    if not isinstance(timeout_seconds, int) or not 1 <= timeout_seconds <= 300:
        raise DeviceProviderClientError("provider timeout must be between 1 and 300 seconds")
    request = request_id or new_request_id()
    command = [str(executable), "--output", "jsonl" if stream else "json", "--request-id", request]
    if selector:
        command.extend(["--selector", selector])
    command.append(operation)
    command.extend(arguments or [])
    try:
        completed = subprocess.run(command, shell=False, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=timeout_seconds, check=False)
    except OSError as error:
        raise DeviceProviderClientError(f"provider failed to start: {error}") from error
    except subprocess.TimeoutExpired as error:
        raise DeviceProviderClientError("provider timed out") from error
    if len(completed.stdout) > MAX_PROVIDER_OUTPUT_BYTES:
        raise DeviceProviderClientError("provider stdout exceeds the host budget")
    try:
        result = parse_provider_jsonl(completed.stdout) if stream else parse_provider_result(completed.stdout)
    except ProviderContractError as error:
        raise DeviceProviderClientError(f"invalid provider output: {error}") from error
    terminal = result[-1] if stream else result
    if terminal["operation"] != operation or terminal["requestId"] != request:
        raise DeviceProviderClientError("provider response does not match the requested operation")
    expected_returncode = _expected_exit_status(terminal["resultCode"])
    if completed.returncode != expected_returncode:
        raise DeviceProviderClientError(
            "provider exit status conflicts with resultCode "
            f"{terminal['resultCode']}: expected {expected_returncode}, got {completed.returncode}"
        )
    return result
