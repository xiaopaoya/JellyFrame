# Device Runtime Contracts

> Last updated: 2026-08-18; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

`device_runtime_contracts` owns the hardware-neutral, value-only contracts for
device discovery, `JFDP/1` frames, capability snapshots and bounded staged app
installation.

## Boundaries

- `device_runtime_protocol.*` defines framing, bounded capability payloads and
  stable request result codes, plus payload-versioned install, lifecycle, logs
  and fixed status/progress result codecs.
- `device_install_transaction.*` defines ordered, cancellable staging through
  an injected `DeviceInstallStore`. Its controller calls idempotent
  transaction-scoped cleanup after every failed begin/write/verify/commit path;
  only a successful store commit may atomically publish a registry entry.
- `jellyframe_device_runtime_contracts` and
  `jellyframe_device_runtime_contracts_tests` link neither App Runtime nor
  Render Core.
- `tests/fixtures/jfdp_v1_wire_vectors.txt` fixes canonical byte encodings for
  framing, capabilities, staged installation and lifecycle payloads. Both the
  C++ contract target and the Python desktop reference validate this fixture.

This module does not implement a USB, serial, Wi-Fi or other physical transport,
filesystem/flash I/O, signature policy, app registry or Device OS launcher.
Those are host and port responsibilities.

It is a monorepo transition boundary until future Device OS package migration
is complete. The desktop reference endpoint has a deterministic typed-frame
dispatcher for the staged-install and lifecycle loop, but it is not a physical
transport. See `docs/device_runtime.md` for the protocol and lifecycle contract,
and `docs/engine_architecture.md` for repository ownership.
