# Device OS Tool Provider Contract

> Last updated: 2026-08-21; Applies to: 0.6.0-dev; Status: WS147 provider handoff passed; wider A2 pending

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
jellyframe-device --output json --request-id <host-id> discover
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> info
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> list
jellyframe-device --output jsonl --request-id <host-id> --selector <endpoint-id> install --bundle <absolute-jfapp-path>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> launch --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> stop --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> remove --id <app-id> [--keep-data]
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> rollback --id <app-id>
jellyframe-device --output jsonl --request-id <host-id> --selector <endpoint-id> logs --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> recovery
```

This is a provider contract. Runtime CLI exposes matching `device` commands
through a configured provider path; it ships no provider and never infers a
COM port, USB identity, network host or executable from `PATH`. `install`
also performs an update when the bundle identity is already installed. VS Code
will reuse this same client once provider fixtures pass. Diagnostics go to
stderr. `--output json` writes exactly one UTF-8 JSON document to stdout;
`--output jsonl` writes UTF-8 JSON Lines.

Exit status: `0` for `resultCode=ok` or `accepted`; `1` for device-operation
failure; `2` for invalid invocation; `3` for unavailable transport; `4` for
protocol/image incompatibility; `5` for provider failure.

`--request-id` is required for every machine-readable invocation. The Runtime
host generates it, passes it unchanged and rejects a result whose request ID or
operation differs. Providers must not synthesize or rewrite it.

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

`operation` is `discover`, `info`, `list`, `install`, `cancel`, `launch`, `stop`, `remove`,
`rollback`, `logs` or `recovery`. Reuse documented JFDP result-code names when
applicable; the only provider-specific values are `transport-unavailable`,
`protocol-mismatch` and `provider-failed`. `requestId` is host-generated ASCII
with a 64-byte maximum.

`discover` returns a bounded `devices` array. A usable record contains stable
opaque `endpointId`, board/profile/image/runtime identities, `JFDP/1`,
connection state, display shape/size, enabled feature families, maximum bundle
bytes and available storage. Feature-family IDs are unique lowercase ASCII
`[a-z0-9][a-z0-9.-]{0,95}` values, with at most 64 entries. Other operations return one selected device plus
optional typed transaction, progress, log summary or recovery data. A result is
at most 64 KiB. It must not expose raw bundle bytes, flash addresses,
filesystem paths, private keys or native handles.

`info` is backed by the typed `JFDP/1 Identity` response. In addition to the
device record used for discovery, it must attest `imageId`, `profileId`,
`imageVersion`, `renderCoreVersion`, a lowercase 40-character source revision,
nonzero Render Core ABI, and the complete feature-family set. The stable wire
bits map to `core.document`, `core.paint`, `css.flex-grid`,
`css.modern-paint`, `forms.advanced`, and `graphics.canvas2d`; document and
paint are mandatory. A successful `info` result carries both `device` and an
`identity` object with exactly those seven camel-case fields; its `profileId`
and `imageVersion` must match `device`. A provider must report these
device-derived values, not a host configuration guess.

`list` maps the JFDP AppList payload and returns `apps` with
`registryGeneration`; the two fields always appear together. There are at most
24 entries. Every entry is exactly `appId`, `versionName`, `versionCode`,
`bundleBytes`, `state` (`installed`, `disabled` or `failed`) and
`rollbackAvailable`. A successful `list` always includes both fields.

`recovery` maps the JFDP recovery-detail payload. A successful result carries
exactly `appId` (empty only for device-wide recovery), `registryGeneration`,
`recoverySequence`, `reason`, `launcherActive`, `appDisabled`, and
`rollbackAvailable`. `reason` is one of `none`, `registry-invalid`,
`staging-discarded`, `app-load-failure`, `app-runtime-failure`,
`app-budget-exceeded`, or `launcher-fallback`.

An optional `transaction` is exactly `id`, `receivedBytes`, `expectedBytes`,
`complete`, and `active`; all byte counts are uint32 and received bytes cannot
exceed expected bytes. An optional `progress` is exactly `completedBytes` and
`totalBytes` with the same bound. A successful terminal `logs` result contains
only `logSummary`, exactly `returnedRecords` and `droppedRecords`. Individual
records are JSONL events, never a duplicate terminal list. This mirrors the
typed JFDP Logs response: at most 11 records per response, each with `level`,
`appId`, uint32 `generation`, decimal-string uint64 `timestampMs`, and a
message of at most 255 UTF-8 bytes. The decimal string preserves timestamps in
JavaScript clients. Providers must not serialize registry or task-private
structures instead.

## JSONL And Adoption

For `--output jsonl`, the stream is at most 256 KiB and 1024 non-empty lines.
Every line carries `format`, `formatVersion`, `operation`, `requestId`,
`sequence` and `provider`; `sequence` is a positive, strictly increasing
uint32. Only `install` may emit `progress`, adding only
`progress.completedBytes` and `progress.totalBytes`. Only `logs` may emit a
`log` event. Its record contains exactly `level`, `appId`, `generation`,
`timestampMs`, and `message`; `level` is `debug`, `info`, `warn` or `error`.
The one final `result` uses the ordinary result envelope plus `sequence` and
must be the last line. For a successful `logs` stream,
`logSummary.returnedRecords` must exactly equal the number of emitted log
events. Missing, duplicate or out-of-order terminal events, or any identity
change inside a stream, are provider failures, never successful installs.
`cancel` returns `cancellation.confirmed` as a boolean. Runtime treats any
value other than `true` as a failed cancellation; killing a host process is not
proof that staging was cleared.

Before a physical provider is presented to authors or bound into the VS Code
deployment UI, Device OS must deliver the same-image/profile JFDP
wire-acceptance report, a validated
`device_image_manifest.md` record, deterministic JSON/JSONL fixtures for
no-device/protocol mismatch/storage full/interrupted transfer, and a versioned
discovery/install/update/rollback/remove/log/reconnect report. The host must
match discovery data against that manifest before deploying. The CLI commands
remain an explicit contract client until then; they are not evidence that a
physical provider exists.
