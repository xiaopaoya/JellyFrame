# Device OS Tool Provider Contract

> Last updated: 2026-08-18; Applies to: 0.6.0-dev; Status: draft; physical provider not implemented

This host-process contract separates Runtime author tools from physical board
dependencies. It is not `JFDP/1`, does not change its wire bytes and does not
make the desktop reference endpoint a device transport.

## Ownership

`jellyframe-device-os` owns physical discovery, USB/serial dependencies,
endpoint selection, JFDP framing and device telemetry. JellyFrame Runtime owns
package preflight, desktop debugging and editor UX. Render Core owns none.

`jellyframe_cli.py device-reference` remains a desktop control-semantics
fixture. It must not become a serial fallback or dynamically load a hardware
plugin. A physical provider is configured explicitly by Device OS tooling.

`tools/device_provider_contract.py` already validates bounded JSON result
envelopes for future Runtime tooling. It opens no transport and cannot discover
or control a board; the physical provider remains Device OS work.

## Provider Invocation

The first Device OS release should ship one executable, provisionally
`jellyframe-device`, with a machine-readable mode:

```text
jellyframe-device --output json discover
jellyframe-device --output json --selector <endpoint-id> install --bundle <absolute-jfapp-path>
jellyframe-device --output json --selector <endpoint-id> logs --id <app-id>
```

This is a provider contract, not a current Runtime command. Runtime CLI and VS
Code invoke only a configured absolute provider path. They never infer a COM
port, USB identity, network host or executable from PATH. Diagnostics go to
stderr. `--output json` writes exactly one UTF-8 JSON document to stdout;
`--output jsonl` writes UTF-8 JSON Lines.

Exit status: `0` for `resultCode=ok` or `accepted`; `1` for device-operation
failure; `2` for invalid invocation; `3` for unavailable transport; `4` for
protocol/image incompatibility; `5` for provider failure.

## Result Envelope

Every result has this bounded envelope. The first revision rejects unknown
top-level fields so the host cannot silently ignore a contract change.

```json
{
  "format": "jellyframe.device-provider",
  "formatVersion": 0,
  "kind": "result",
  "operation": "discover",
  "requestId": "host-000042",
  "resultCode": "ok",
  "provider": { "id": "jellyframe-device", "version": "0.1.0-dev" },
  "devices": []
}
```

`operation` is `discover`, `info`, `install`, `launch`, `stop`, `remove`,
`rollback`, `logs` or `recovery`. Reuse documented JFDP result-code names when
applicable; the only provider-specific values are `transport-unavailable`,
`protocol-mismatch` and `provider-failed`. `requestId` is host-generated ASCII
with a 64-byte maximum.

`discover` returns a bounded `devices` array. A usable record contains stable
opaque `endpointId`, board/profile/image/runtime identities, `JFDP/1`,
connection state, display shape/size, enabled feature families, maximum bundle
bytes and available storage. Other operations return one selected device plus
optional typed transaction, progress, logs or recovery data. A result is at
most 64 KiB; a log result has at most 256 records. It must not expose raw bundle
bytes, flash addresses, filesystem paths, private keys or native handles.

## JSONL And Adoption

For `--output jsonl`, every line retains format, operation and request id, has a
strictly increasing `sequence`, and is `progress`, `log` or one final `result`.
Missing, duplicate or out-of-order terminal events are provider failures, never
successful installs. Cancellation must report whether the provider confirmed
the JFDP transaction cancellation; killing a host process is not proof that
staging was cleared.

Before Runtime CLI or VS Code consumes a provider, Device OS must deliver the
same-image/profile JFDP wire-acceptance report, deterministic JSON/JSONL
fixtures for no-device/protocol mismatch/storage full/interrupted transfer, and
a versioned discovery/install/update/rollback/remove/log/reconnect report.
Only then may Runtime add a real `device` command or VS Code add a selector.
