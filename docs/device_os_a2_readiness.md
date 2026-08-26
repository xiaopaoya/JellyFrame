# Device OS A2 Readiness And Implementation Requirements

> Last updated: 2026-08-25; Applies to: 0.6.0-dev; Status: implementation prerequisite stage

## Current Conclusion

**The WS147 provider handoff is closed for the published image. Wider A2 is
not yet ready for delivery acceptance and must not open an external developer
trial.**

The mainline now supplies the hardware-neutral control plane: JFDP/1, typed
Identity and Logs payloads, and the
install contracts, Developer Image manifests, a strict provider JSON/JSONL
parser, an explicit provider host client, CLI `discover/info/list/install/cancel/logs`
entry points, and VS Code discovery, identity and read-only installed-App list
session state. This proves that the host
does not guess ports, fabricate cancellation, or silently accept mismatched
provider output. The WS147 `jellyframe-device@0.1.0-dev` delivery archive,
`jellyframe-ws147-developer-0.6.0-a2-provider-0.1.0-dev.zip`, now provides the
separately installable wrapper, provider, dependency lock, configuration
template, Developer Image manifest, firmware, factory image and per-file
SHA-256 list. Its 13 payload hashes were reverified on 2026-08-25. This closes
the provider-package prerequisite, but it does not prove the actual clean-machine
VS Code session or that an installed App can render, receive input, emit logs,
or recover.

The current published package is explicitly limited to the read-only
`discover/info/list` smoke. The Runtime extension has a capability-gated
lifecycle group, but intentionally keeps deploy, mutation and App-debug
controls hidden until the selected provider advertises each operation. To align with selected-device attestation, the
unpublished `jellyframe-device@0.1.1-dev` provider source adds device records
to live cancellation and `logs` terminals. Repackaging it, passing host
fixtures and recording the archive hash are prerequisites for usable mutation
and lifecycle UI; the current `0.1.0-dev` package cannot substitute for them.

WS147 has A1 storage/recovery and factory-image evidence. The targeted
2026-08-21 workspace remeasure, based on `b372cc4`, also proves a real
22,924-byte resource bundle can be fully transmitted, committed, launched and
stopped: valid commit returned `accepted` in 2,049 ms, a fully transmitted
corrupt bundle returned typed `integrity-failed` in 2,969 ms, and no registry
publication occurred for the corrupt case. The storage owner kept the 4 KiB
inspection workspace, 4 KiB sector cache and 1 KiB transport scratch outside
the device task stack; the minimum free stack was 14,468 bytes from a 24,576
byte configured stack.

A2 provider handoff is **PASS** for firmware `afdcf75` and the published WS147
manifest: identity cross-match, in-flight cancellation, durable lifecycle and
30 mixed cycles all passed. Wider A2 remains **partial** because the evidence
does not close the clean-machine VS Code product workflow or real installed-App
panel/input acceptance. The provider handoff report is not an external-trial
release signoff.

## Ownership And Completion

| Layer | Completed | Required before A2 acceptance |
| --- | --- | --- |
| Render Core | Independent Core package, profiles/ABI, hardware-neutral render and input contracts | No A2 blocker; a provider must not duplicate renderer logic |
| JellyFrame Runtime/Tools | `.jfapp`, bundle checks, manifest/provider contracts, CLI host client, provider handoff contract | Clean-machine VS Code deployment/log session, readable error mapping and end-to-end tool regressions |
| Device OS | A1 launcher/registry/staging/recovery foundation; WS147 provider handoff and versioned provider delivery | Install/configure the real `jellyframe-device` provider and bind installed bundles to AppHost, renderer, input and logs |
| WS147 port | JFDP wire, persistent lifecycle, factory recovery, bounded real-resource commit and provider handoff for the measured profile | Real installed-App panel/input evidence; no provider lifecycle blocker remains for this image |

## Required Device OS Implementation

### 1. Provider Process And Session

Implement a separately installed, explicitly configured `jellyframe-device`
executable. It accepts only an explicit selector and never scans for or chooses
a port automatically. `discover` returns stable opaque `endpointId` values;
`info` validates the same endpoint. A provider must not output raw serial
console data, flash addresses, filesystem paths, pointers, or secrets as JSON.

The provider follows [device_tool_provider_contract.md](device_tool_provider_contract.md):

- `--request-id` is echoed exactly; JSON/JSONL stdout never contains logs.
- `discover/info` return JSON; `install/logs` return strictly increasing JSONL
  sequences.
- `install` transfers only a Runtime-packaged `.jfapp`; it never passes a host
  path to a device.
- `cancel` maps the actual JFDP abort outcome to `cancellation.confirmed`.
  Disconnecting, killing the provider, or resetting the MCU is not confirmed
  cancellation.
- Exit code, `resultCode`, and stderr diagnostics follow one contract and never
  contradict one another.

### 2. Device-Side JFDP Adapter

The endpoint needs a bounded frame decoder, session/request correlation,
timeouts, and reconnect handling. Each install uses the existing
`DeviceInstallStore` state machine: begin/write/verify/commit/abort. It reports
success only after atomic publish. The provider must consume typed AppList,
Recovery, progress, and failure results; fixed fixtures or serial-text parsing
are not substitutes.

Ownership must be explicit: transport RX/TX buffers belong only to the
transport task; install bytes are copied before crossing tasks; registry and
storage belong only to Device Runtime; UI/App tasks never retain provider or
transport handles; JerryScript, DOM, `Node`, `LayerNode`, and arena addresses
never cross tasks or processes.

### 3. Installed-App Execution Closure

This is the largest A2 gap. The first implementation now routes a copied
`runtime.script: "classic"` bundle into an isolated worker-local
`ScriptTaskWorkerRuntime`; static bundles continue through the native UI path.
It is implementation-complete enough for physical validation, not accepted
until the WS147 evidence below exists. Bind `AppInstalledBundleBinding` to the
actual runtime:

1. The launcher selects a published-registry bundle and loads resources through
   the bundle reader.
2. It creates an App Runtime, including a script worker where required, and a
   Render Core document. The installed-script route copies HTML, CSS and each
   package-local classic script before the worker starts. The worker alone owns
   JerryScript, DOM, layout and layer data; UI receives only sealed value
   frames. Resource, frame, input, and service handoff remain on existing
   value-only protocols.
3. The UI task decodes and presents frames, and converts input to value-only
   packets. App fatal or load failure returns to the protected launcher.
4. Runtime/launcher emit bounded app-scoped logs carrying app ID, generation,
   timestamp and level; the provider only forwards the typed, copied records
   (at most 11 records and 255 bytes per message).
5. `stop/remove/rollback` first stop input and services, wait for teardown, and
   only then mutate the registry. An old frame or generation may never present
   after a new App starts.

### 4. Developer Image Release Surface

Ship a versioned Developer Image package containing firmware, factory raw image,
manifest, recovery procedure, provider version/installation instructions,
supported profiles/feature families, bundle/storage limits, and provenance. A
provider rejects a device/image that does not match its manifest instead of
guessing compatibility.

## Mandatory Automated Fixtures

Implement the no-device, image-mismatch, transport-unavailable, storage-full,
interrupted-install, confirmed/unconfirmed-cancel, and bounded-log cases in
[device_provider_port_acceptance.md](device_provider_port_acceptance.md). Add:

1. Host rejection of request-ID, operation, or sequence errors.
2. Install -> update -> rollback -> remove of one App, with monotonic registry
   generation and no partial publication.
3. Launcher recovery after launch/load/runtime fatal, with every old
   service/frame/input invalidated.
4. Reconnect that neither repeats commit/logs nor assigns a stale response to a
   new session.

Fixtures run on a host without a board and test the provider contract; they do
not claim physical evidence.

## WS147 A2 Physical Acceptance Order

1. Fix one published Developer Image/manifest/provider version and confirm that
   JFDP wire and A1 recovery have not regressed.
2. Confirm `discover -> info` matches manifest identity exactly, including the
   JFDP Identity Render Core version, revision, ABI and feature families.
3. From VS Code or CLI, install an actual `.jfapp`; retain JSONL, registry,
   launch marker, panel, and input-response evidence.
4. Exercise update, rollback, remove, load failure, runtime fatal, and
   mid-install cancellation, checking after reconnect/reboot each time.
5. Read app-scoped logs and verify that diagnostics do not pollute provider
   stdout, and that there are no watchdog, reset loop, DMA/SPI/panel failures.
6. Complete at least 30 mixed lifecycle cycles and archive a versioned
   report/summary/raw-log/flash-log package.

Steps 1-4 are covered for the WS147 provider handoff by the
`provider-handoff-afdcf75-20260821` report, except that this report does not
claim panel/input behavior. The installed-script execution route also has no
physical evidence yet. Without panel/input evidence from a real installed
classic-script App, fatal recovery, and a clean-machine VS Code run, wider A2
remains `partial`; A1 or a desktop reference cannot fill that gap.

## A2 Exit

Close A2 only when an author on a clean machine, with no ESP-IDF installation,
can install the official image and provider and use VS Code to complete
`new -> check -> package -> discover -> install -> live logs -> update ->
rollback -> remove`. Every failure must identify package, manifest, provider,
transport, registry, Runtime, or port ownership. Controlled failures return to
the launcher with no unexplained reset, watchdog, registry corruption, or
unauthorized host operation. Only then may A3 limited external testing begin.
