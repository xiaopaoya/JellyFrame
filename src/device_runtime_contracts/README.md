# Device Runtime Contracts

> Last updated: 2026-08-15; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

`device_runtime_contracts` owns the hardware-neutral, value-only contracts for
device discovery, `JFDP/1` frames, capability snapshots and bounded staged app
installation.

## Boundaries

- `device_runtime_protocol.*` defines framing, bounded capability payloads and
  stable request result codes.
- `device_install_transaction.*` defines ordered, cancellable staging through
  an injected `DeviceInstallStore`.
- `jellyframe_device_runtime_contracts` and
  `jellyframe_device_runtime_contracts_tests` link neither App Runtime nor
  Render Core.

This module does not implement a USB, serial, Wi-Fi or other physical transport,
filesystem/flash I/O, signature policy, app registry or Device OS launcher.
Those are host and port responsibilities.

It is a monorepo transition boundary until typed JFDP request/response payload
dispatch and the future Device OS package migration are complete. See
`docs/device_runtime.md` for the protocol and lifecycle contract, and
`docs/engine_architecture.md` for repository ownership.
