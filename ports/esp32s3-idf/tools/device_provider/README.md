# WS147 Device Provider

`jellyframe-device.cmd` is the explicitly configured WS147 Device OS provider.
It is a host-side adapter for JFDP/1 over the board's native USB Serial/JTAG
endpoint; it does not scan ports or parse the ESP-IDF console.

## Installation

Copy this directory to a stable local location, copy
`jellyframe-device.config.example.json` to `jellyframe-device.config.json`,
then set the physical port and the absolute path to the released Developer
Image manifest. `endpointId` is a user-chosen opaque identifier and must not
contain a COM port, path, secret, or flash address.

The provider finds its adjacent `jellyframe-device.config.json`, or a caller
may set `JELLYFRAME_DEVICE_CONFIG` to an absolute configuration path. The
Runtime CLI receives the absolute path to `jellyframe-device.cmd`; it does not
receive a COM port and does not infer one.

The provider requires Python 3 and `pyserial`. It owns the serial handle for
one invocation, emits only provider JSON/JSONL to stdout, and writes transport
diagnostics only to stderr.

## Test Fixtures

`jellyframe_device.py --fixture <name>` is test-only and requires no board.
It provides deterministic contract fixtures for `no-device`, `image-mismatch`,
`transport-unavailable`, `storage-full`, `interrupted-install`,
`confirmed-cancel`, `unconfirmed-cancel`, and `bounded-logs`. Fixtures do not
claim a physical endpoint or installed-app execution.
