# Device Provider Port Acceptance

> Last updated: 2026-08-19; Applies to: 0.6.0-dev; Protocol: JFDP/1

This is the A2 handoff for a physical Device OS provider. It verifies the
host-process boundary used by `jellyframe_cli.py device`; it does not replace
the JFDP wire, Developer Image lifecycle, panel or touch acceptance gates.
The full A2 readiness boundary, including installed-App execution, is defined
in [device_os_a2_readiness.md](device_os_a2_readiness.md).

## Scope

The port/Device OS delivers one explicitly selected executable, provisionally
`jellyframe-device`. It owns endpoint discovery, USB/serial dependencies, JFDP
framing, device telemetry and all board-specific state. JellyFrame CLI and VS
Code never infer a COM port or run a serial fallback.

Every machine-readable invocation must accept a host-generated request ID:

```text
jellyframe-device --output json --request-id <id> discover
jellyframe-device --output json --request-id <id> --selector <endpoint> info
jellyframe-device --output jsonl --request-id <id> --selector <endpoint> install --bundle <absolute.jfapp>
jellyframe-device --output json --request-id <id> --selector <endpoint> cancel --transaction-id <id>
jellyframe-device --output jsonl --request-id <id> --selector <endpoint> logs --id <app-id> --limit <1..256>
```

The provider must implement the exact result/JSONL schema in
[device_tool_provider_contract.md](device_tool_provider_contract.md). It must
write protocol and operational diagnostics to stderr only. `stdout` is exactly
one JSON result or one bounded JSONL stream, with no banners or serial output.

## Required Fixtures

Provide deterministic provider fixtures, runnable on the host without a board,
for these cases:

| Fixture | Expected host result |
| --- | --- |
| no device | `discover` returns `ok` with an empty `devices` array |
| wrong image/profile | a device record differs from the selected manifest and host rejects it |
| transport unavailable | exit `3`, terminal `transport-unavailable` |
| install storage full | JSONL terminal `storage-full`; no new app is listed |
| interrupted transfer | JSONL reports a stable failure/cancellation; staging is not published |
| confirmed cancellation | `cancel` returns `cancellation.confirmed=true` only after the JFDP transaction is cancelled |
| unconfirmed cancellation | `cancel` returns `confirmed=false` or failure; host must treat it as failure |
| log bounds | `logs` emits at most the requested limit, never more than 256 records |

Fixtures must preserve the input request ID and operation in every response.
They may not use the desktop reference registry as a physical-device stand-in.

## WS147 Physical Acceptance

Run the provider against the published WS147 Developer Image. Record provider
version, Device OS commit, Runtime/Core provenance, manifest SHA-256, board,
USB endpoint identity, firmware hash and build configuration.

1. `discover` returns exactly the published board/profile/image/runtime,
   display, feature families, bundle limit and current available storage.
2. `info` for its returned opaque selector reports the same identity.
3. Install a checked `.jfapp`; retain JSONL progress and terminal result, then
   verify with `info`/device state that the app can launch.
4. Begin a second install, cancel it through the provider, and prove the
   prior committed App remains launchable after reconnect or reboot.
5. Read bounded app-scoped logs and demonstrate that diagnostics are not mixed
   into stdout JSONL.
6. Attempt a manifest mismatch and a storage-full/oversize bundle. Both must
   fail without publishing a partial App.

Do not claim cancellation from host-process termination, USB disconnect alone
or an MCU reset. Do not expose flash addresses, arbitrary files, raw serial
console data, private keys or JFDP handles in JSON.

## Evidence And Exit

Archive a versioned directory containing `report.md`, `summary.json`, raw
provider stdout/stderr for each case, CLI commands, provider fixture sources,
manifest, build/flash logs and the exact `.jfapp` hashes. `summary.json` must
separately state discovery, identity matching, install, cancellation, logs,
reconnect/reboot, watchdog/reset and transport/panel error counts.

This A2 provider handoff passes only when all fixture cases and the WS147 run
pass with the same published image identity. The next mainline step is then to
bind the provider session into the VS Code device view for deploy progress and
logs. It does not by itself open an external developer trial.
