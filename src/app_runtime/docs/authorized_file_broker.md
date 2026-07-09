# Authorized File Broker

> Last updated: 2026-07-10; Applies to: 0.5.0-dev

JellyFrame does not expose a raw filesystem API to ordinary apps. App-private
storage remains the default persistence model. General file access is reserved
for system components, file-manager apps or user-approved app actions, and must
go through a host-owned broker.

## Capabilities

The standard manifest capability names are:

- `file.read`: read user-approved files or logical host paths.
- `file.write`: write or replace user-approved files through host staging.
- `file.manage`: list, rename, delete or create entries for trusted file-manager
  and system-component flows.

These names are only intent declarations. A host/profile must still grant the
same capability, and every operation must be user-approved or come from a
trusted system component.

## V0 UX And API Decision

V0 keeps general file access as a host/system-shell broker contract, not an
ordinary app JavaScript API.

Ordinary apps should use app-private storage unless the user starts a specific
file action through product UI, such as "import this file", "export this log" or
"replace this selected picture". The host records the approved operation,
logical path scope, byte budget and whether the approval is one-shot or
persistent, then submits a bounded broker job. The app still does not receive a
native path or filesystem handle.

Trusted system components and file-manager apps may use
`trusted_system_component=true` when the host has launched them in that role.
That role is product policy, not something an installed third-party app can set
for itself.

Every mutating operation should stage first, then commit only after validation.
Write, rename and delete failures must roll back or leave the old entry intact.
User cancellation, timeout, media removal, quota failure and unsupported
operation should all return stable broker status without crashing the runtime or
requiring firmware reflashing.

A future JavaScript surface should be added only after this UX is validated. It
should likely remain a small async broker API tied to manifest capabilities and
host approval, not a clone of desktop browser File System Access.

## Core Contract

`authorized_file_broker.h` provides the platform-neutral validation layer:

- `AuthorizedFilePolicy`: host/profile gates for read/write/manage plus path and
  byte budgets.
- `AuthorizedFileRequest`: requested operation, normalized logical path, optional
  secondary path for rename, transfer byte count and approval flags.
- `validate_authorized_file_request(...)`: returns stable status values such as
  `user-approval-required`, `capability-denied`, `invalid-path`,
  `traversal-rejected` and `byte-budget-exceeded`.

The core never opens files, flash partitions or block devices. A valid request
should be submitted as a host-owned async job, represented by
`HostServiceJobKind::AuthorizedFile`, and completed back on the UI task. Apps
must not receive raw filesystem handles; any result handle should be a bounded
broker result owned by the host.

## Path Rules

Broker paths are logical absolute paths, not native filesystem paths:

- Must start with `/`.
- Must not contain `://`, `//`, `\`, control characters, `.` or `..`
  components.
- Must not end with `/` unless future broker versions explicitly define a root
  directory operation.
- Must fit `AuthorizedFilePolicy::max_path_bytes`.

The host maps these logical paths to product storage, media partitions or user
mounts after validation.

## Host Responsibilities

The host or port layer must implement:

- User approval or trusted-component policy.
- Path-to-storage mapping.
- Staging and rollback for writes, rename and delete.
- Async completion, timeout and cancellation.
- Byte budgets and stable error mapping.
- A fallback for any non-firmware-modifying failure, so recovery never requires
  reflashing firmware.

No JavaScript file API is exposed yet. Future JS bindings should be added only
after the broker lifecycle and permission UX are proven in the Win32 shell.

## Win32 Validation

The Win32 shell includes a deterministic broker smoke test:

```powershell
.\build-script\Release\jellyframe_win32_browser.exe --authorized-file-smoke out\file_broker
```

It verifies that unapproved writes do not alter another logical app path,
traversal paths are rejected, staged writes commit only after validation,
simulated commit failures preserve the previous file and manage operations are
gated separately from read/write.
