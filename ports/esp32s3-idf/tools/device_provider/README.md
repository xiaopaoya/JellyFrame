# WS147 Device Provider

`jellyframe-device.cmd` is the explicitly configured WS147 Device OS provider.
It is a host-side adapter for JFDP/1 over the board's native USB Serial/JTAG
endpoint; it does not scan ports or parse the ESP-IDF console.

## Standalone Release

`package_ws147_device_provider.py` creates a versioned WS147 Developer Image
archive containing the provider, its strict manifest parser, one exact
firmware binary, a complete 16 MiB factory image, the manifest, recovery
procedure and `SHA256SUMS.txt`.

The archive is intentionally board-specific. It contains no serial-port scan,
serial-console parser or fallback provider. Its provider release record is
currently `jellyframe-device@0.1.1-dev`.

```powershell
python ports\esp32s3-idf\tools\device_provider\package_ws147_device_provider.py `
  --firmware <firmware.bin> `
  --factory-image <factory-16mb.bin> `
  --source-revision <40-lowercase-hex> `
  --image-version 0.6.0-a2 `
  --output <release-directory>
```

The factory image input must be an offset-zero, complete 16 MiB image created
from the same firmware build. The packager writes both firmware and factory
SHA-256 values into the immutable Developer Image manifest.

## Installation

Copy this directory to a stable local location, copy
`jellyframe-device.config.example.json` to `jellyframe-device.config.json`,
then set the physical port and the released Developer Image manifest.
Release archives use a manifest path relative to the configuration file;
absolute paths also work. `endpointId` is a user-chosen opaque identifier and
must not contain a COM port, path, secret, or flash address.

The provider finds its adjacent `jellyframe-device.config.json`, or a caller
may set `JELLYFRAME_DEVICE_CONFIG` to an absolute configuration path. The
Runtime CLI receives the absolute path to `jellyframe-device.cmd`; it does not
receive a COM port and does not infer one.

The provider requires Python 3.10+ and `pyserial==3.5` from `requirements.txt`.
It owns the serial handle for one invocation, emits only provider JSON/JSONL to
stdout, and writes transport diagnostics only to stderr. Every successful selected
operation echoes the typed device record for the requested opaque endpoint. This
also applies to a confirmed in-flight cancellation: the install owner passes the
attested device record through its local control session without opening a second
serial handle.

For VS Code, set `jellyframe.deviceProvider` to the absolute path of this
`jellyframe-device.cmd` and `jellyframe.deviceManifest` to the matching
manifest. Run Discover Device, then Device Info. The extension only invokes
this explicit provider through `jellyframe_cli.py`; it neither embeds the
provider nor infers a serial endpoint.

## Test Fixtures

`jellyframe_device.py --fixture <name>` is test-only and requires no board.
It provides deterministic contract fixtures for `no-device`, `image-mismatch`,
`transport-unavailable`, `storage-full`, `interrupted-install`,
`confirmed-cancel`, `unconfirmed-cancel`, `bounded-logs`, `lifecycle-ok`, and
`lifecycle-failed`. `lifecycle-ok` declares the exact capability order used by
the WS147 provider and is only a contract fixture; it does not claim a
physical endpoint or installed-app execution.
