# JellyFrame Device Runtime

> Last updated: 2026-08-15; Applies to: 0.6.0-dev; active development line: 0.6.0

## Purpose

JellyFrame Device Runtime is the product layer that makes an official board
usable by an app author without building firmware. It is not a new renderer,
general-purpose operating system, package marketplace or board abstraction
inside Render Core.

Its minimum user flow is deliberately small:

1. Select an officially supported board and install its signed developer image.
2. Connect the board through an official developer transport.
3. Build, install, update, launch, stop or remove a `.jfapp` from the CLI or
   VS Code extension.
4. Inspect app-scoped logs, lifecycle state and supported device capabilities.

An app author must never need ESP-IDF, a partition table, a board pin map or a
firmware rebuild for that workflow.

## Boundary

| Layer | Owns | Must not own |
| --- | --- | --- |
| Render Core | DOM, layout, paint, input semantics and capability profiles | Board drivers, storage, transport, package installation policy |
| App Runtime | lifecycle, recovery and bounded host services; D0 temporarily hosts hardware-neutral install transaction contracts | Serial/USB/Wi-Fi drivers, flash APIs, signing authority and final Device OS policy |
| Device Runtime | launcher policy, installed-app registry, rollback policy, developer session and device diagnostics | Rendering implementation or SoC-specific fast paths |
| Port | board image, boot, display/touch, persistent storage adapter, developer transport and firmware update | New core APIs without a platform-neutral need |
| CLI and VS Code | package, preflight, connection UX, deployment progress, logs and interactive debug | Direct GPIO, flash or arbitrary shell access |

The Device Runtime consumes Render Core capabilities; it does not make them
mandatory. A port that does not enable scripting, Canvas or a media adapter
must advertise that fact before an app is transferred.

## Current Baseline

The repository already has useful pieces, but they do not yet form a device
product:

| Existing | Status | Missing for device use |
| --- | --- | --- |
| `.jfapp` packaging, preflight, target budgets and resource integrity | Available | Device-side transfer and storage adapter |
| Registry install/update/rollback semantics | Desktop reference implementation | Persistent embedded registry and staging store |
| App launch, teardown, crash recovery and launcher return | Hardware-neutral contract plus tested ports | Installed-bundle loader connected to the device registry |
| Sample launcher and desktop app manager | Reference UI | Official system launcher image and protected fallback path |
| VS Code app author workbench | Desktop package/check/preview/debug | Device selector, deploy, log and live-device debug commands |
| ESP32-S3 display ports | Bring-up and acceptance configurations | Stable developer image, storage partition and control transport |
| ESP32-P4 port | Accelerator bring-up | A display-equipped official board profile and developer image |

Until the missing work is complete, the capability matrix must describe app
distribution as a desktop/system-shell contract, not as supported device
deployment.

## Device Control Contract

The initial device protocol is a local developer protocol, named `JFDP/1`
(JellyFrame Device Protocol). Its message meaning is transport-neutral. A port
may carry it over USB CDC, USB Serial/JTAG, UART, Wi-Fi or a host bridge, but
must not expose raw flash, arbitrary native execution or arbitrary filesystem
access.

The protocol has five bounded groups:

| Group | Operations |
| --- | --- |
| Discovery | `hello`, runtime version, board/profile id, display shape, enabled capability families, storage budget |
| App library | list installed apps, app state, launch, stop, remove, rollback, enable and disable |
| Transfer | begin, ordered chunks, commit, abort, progress and retry-safe result codes |
| Debug | app-scoped log subscription, lifecycle events, frame/capture request where the profile permits it |
| Recovery | current app status, last failure reason, launcher/fallback state and controlled reboot request |

Every request has a session and request id. Transfers are staged under a
single transaction id; chunks carry explicit offsets and integrity data. A
commit verifies the completed bundle before atomically publishing the new
registry entry. Failure or disconnect discards staging bytes and keeps the
last committed version launchable. Updating a foreground app either continues
running the old bundle until the host switches it, or returns to the launcher;
the new bundle is never partially visible.

The protocol does not define remote download, account login, marketplace
payments or package signing authority. Those remain product-host concerns.

In `JFDP/1`, bit `0` of `DeviceFrameHeader.flags` is
`kDeviceFrameFlagResponse`. A response retains its request's message type,
session id and request id. Other flag bits are reserved: receivers preserve the
frame value but must not assign them meaning until a later protocol revision.
The D0 C++ regression includes an in-memory discovery request/capability
response loopback. It proves this framing contract only; it is not a physical
transport or a claim that the desktop registry reference endpoint is already a
JFDP device.

### Current Reference Implementation (D0 Transition)

The platform-independent `src/app_runtime/device_runtime_protocol.*` implements
`JFDP/1` framing with a fixed 24-byte header, little-endian integers, a strict
4096-byte payload limit, message-type validation and CRC32. A decoded payload is
a read-only view into the input buffer; it must be copied before crossing a task
or asynchronous queue boundary. The protocol layer never transfers pointer
ownership.

The same module provides bounded `DeviceCapabilitySnapshot` encoding and stable
request result codes for board/profile identity, runtime version, display size,
enabled capability bits, maximum bundle size and available storage. Strings have
explicit limits and the payload codec does not depend on JSON, heap allocation or
port-private structures.

`src/app_runtime/device_install_transaction.*` implements a bounded, ordered and
cancellable staging state machine through the injected `DeviceInstallStore`.
Flash, filesystem, signature and registry policy remain in the adapter and are
not pulled into Render Core. Write, verification, commit and explicit
cancellation failures discard staging; a new version becomes visible only after
atomic commit.

These two `device_*` modules are not part of the App Runtime's final ownership
model. D0 now compiles them as the independent
`jellyframe_device_runtime_contracts` target, with tests that link that target
without App Runtime or Render Core. Their source path remains transitional until
the typed JFDP request/response payload dispatcher and separate-owner migration
are complete. A port must consume the existing framing, result-code and staging
contracts; it must not fork them. Physical extraction is complete only when the
new owner also has versioned headers, a Runtime/Device OS compatibility entry
and its own repository release policy.

The desktop reference endpoint can be selected explicitly while exercising the
toolchain:

```text
python tools/jellyframe_cli.py device --transport reference --store build/device-reference discover --json
python tools/jellyframe_cli.py device --transport reference --store build/device-reference install --bundle app.jfapp --chunk-bytes 1024
python tools/jellyframe_cli.py device --transport reference --store build/device-reference launch --id org.example.app
python tools/jellyframe_cli.py device --transport reference --store build/device-reference logs --id org.example.app --json
python tools/jellyframe_cli.py device --transport reference --store build/device-reference rollback --id org.example.app
```

The reference host persists chunk staging separately from the registry, exposes
`resume`, `commit`, `cancel`, `launch`, `stop`, `remove`, `logs` and `recovery`,
and only publishes a bundle after commit. `--pause-after-chunks` is a
reference-only test hook for exercising resume/cancel; it is not a device
transfer option. The endpoint reports `deviceAvailable=false`: its lifecycle
logs and recovery records are desktop reference evidence, not panel, touch,
wire-transport or device-frame telemetry. Without `--transport reference`, the
CLI reports that no physical transport is configured. USB, serial and Wi-Fi
transports will be registered by their respective ports rather than inferred by
the core.

## Official Board Profiles

An official profile is more than a port compiling. It publishes a stable board
id, display/touch configuration, capability profile, storage limits, developer
transport, factory launcher and recovery behavior. The initial support order is:

1. **ESP32-S3 Waveshare Touch LCD 1.47**: first developer image and reference
   wearable profile (`rect-172x320`).
2. **ESP32-S3 Waveshare Touch LCD 1.69**: second official image once the same
   lifecycle and transport contract is exercised (`rect-240x280`).
3. **ESP32-P4**: only after a display-equipped board profile is validated; the
   current P4 accelerator bring-up is not an app-author board.

Each image must reserve a protected system partition/app set containing a
launcher and fallback screen. Third-party bundles must be stored outside the
firmware image and cannot replace the launcher, recovery UI or port code.

## Delivery Plan

### D0: Contract and reference host (contract baseline; extraction pending)

- Define `JFDP/1` framing, request/result codes and a capability handshake.
- Add a hardware-neutral staged-install controller with injected storage
  callbacks and focused tests for offsets, replay, abort, disconnect and
  atomic commit failure.
- Platform-independent framing, capability payloads, request result codes and
  staged-install control now exist. The desktop reference host covers durable
  discovery/list/chunked-install/commit/cancel/lifecycle/log/recovery control
  semantics; an in-memory discovery loopback verifies request/response
  correlation and the capability payload. Typed JFDP request/response payload
  dispatch and separate-owner extraction remain before port integration.

### D1: First official developer image

- Add the ESP32-S3 1.47 storage partition, immutable launcher/fallback and a
  USB developer transport adapter.
- Implement only the D0 control operations required by the first workflow.
- Publish one board/profile manifest and a recoverable factory flashing tool.

### D2: Author tooling

- Add device discovery and explicit device selection to the CLI.
- Add VS Code commands and a device view: connect, install/update, launch,
  stop, remove, logs and runtime capabilities.
- Keep desktop preview separate from device debug. A reported device frame time
  must come from device telemetry, never inferred from Win32.

### D3: External pilot

- Test with users who have no ESP-IDF setup.
- Require installation, update, rollback, bad-app recovery and reconnection to
  work repeatedly without reflashing.
- Add a second official board only after D1's lifecycle path is stable.

### D4: Physical repository split

- Extract Render Core only after the package consumer matrix and provenance
  records remain green across a release cycle.
- Move the D0 `device_*` contracts into Device OS or
  `device_runtime_contracts`; keep JFDP/1 compatibility tests at the new owner.
- Migrate launcher, registry, official images and ports together as a Device OS
  product boundary. Do not move them piecemeal into the Render Core or Runtime
  repositories.

## Acceptance

The first official image is ready for external app authors only when a clean
machine with the VS Code extension can complete the following without SDK
installation:

1. Detect a connected supported board and display its exact profile/capabilities.
2. Install a checked `.jfapp`, observe bounded transfer progress and launch it.
3. Update it, reject a downgrade by default, roll back, remove it and preserve
   or delete private data only through an explicit command.
4. Receive app-scoped logs and lifecycle errors; disconnect/reconnect without
   corrupting the installed library.
5. Recover from a malformed, over-budget or crashing app to the launcher
   without a watchdog reset or firmware reflash.

This product path is the external-trial gate. More HTML/CSS surface area is
valuable, but it cannot compensate for requiring every app author to become a
board-port maintainer.
