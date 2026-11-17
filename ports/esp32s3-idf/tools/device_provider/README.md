# WS147 Device Provider

> Last updated: 2026-11-17; Applies to: 0.6.0-dev

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
currently `jellyframe-device@0.1.2-dev`.

```powershell
python ports\esp32s3-idf\tools\device_provider\package_ws147_device_provider.py `
  --firmware <firmware.bin> `
  --factory-image <factory-16mb.bin> `
  --build-manifest <build-manifest.json> `
  --render-core-profile <jellyframe_render_core_profile.json> `
  --build-profile installedWorker `
  --build-dir <esp-idf-build-directory> `
  --output <release-directory>
```

The full build manifest and generated Core profile are required. Select the
manifest profile explicitly with `--build-profile` (for example, `native` or
`installedWorker`). The packager takes the image version and firmware SHA-256
from that profile's `imageVersion`, `bin.path` and `bin.sha256`, and takes the
Runtime source revision and Core version/ABI from the build manifest. It verifies
the supplied firmware against both the recorded digest and referenced binary.
Core version/ABI must match the generated profile, whose feature-family set is
copied into the Developer Image manifest. Unsupported JFDP feature families,
missing evidence and identity mismatches are rejected. Identity fields cannot
be overridden on the command line.

`--build-dir` is mandatory for `text.shaping` releases. For historical scalar
release evidence it remains optional, preserving the old 327680-byte limit.
When supplied, it adds ESP-IDF provenance checks: it validates the
generated Core source manifest/provenance/profile, ESP-IDF
`project_description.json`, `CMakeCache.txt`, and `sdkconfig`; confirms Core
identity/version/ABI/features and ESP target; and hashes the `app_bin` named by
the project description. It records Core source identity/count, IDF version,
the separate `git_revision`, versioned compiler, target, selected WS147 defaults
and sdkconfig SHA-256 under `buildProvenance`. Manifest source identity/count, IDF
version and versioned compiler evidence must agree with this build directory.
Compiler evidence uses the `esp-<version>_<date>` toolchain identifier. The final
Developer Image manifest passes the same strict parser bundled with the provider
before any release output is written.

Capacity comes from the matching `sdkconfig`, generated `config/sdkconfig.h`
and binary `partition_table/partition-table.bin`, including its MD5, bounds,
labels and non-overlap. It is `min(configured KiB * 1024,
((storageBytes - 8192) / 3 rounded down) aligned down to 4096)`, matching the
store's two registry sectors and three bundle slots. The manifest records the
partition SHA-256 and configured/physical inputs. Factory partition bytes and
the application at the factory partition offset must match the build exactly.
The opt-in 3 MiB storage profile advertises 1044480 bytes. It moves partition
boundaries and requires explicit data/partition migration; an app-only update
cannot deploy that layout.

`runtimeVersion` matches the lifecycle firmware's JFDP runtime contract (`0.6.0-dev`);
`imageVersion` records the selected firmware build version separately.

The factory image input must be an offset-zero, complete 16 MiB image created
from the same firmware build. The build manifest attests the selected app
firmware, not the factory image; verify and retain factory-image provenance
separately. The packager records both firmware and factory SHA-256 values in
the immutable Developer Image manifest.

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

Install accepts an absolute scalar `.jfapp` or signed `.jfsg`. Shaped format1
requires a signed envelope and the exact device/manifest `text.shaping` family
(JFDP identity bit6); scalar format0 remains supported by the host. Device store
policy still decides whether unsigned scalar installation is permitted.
Inspection and transport use one bounded immutable read, so replacing the file
after admission cannot change transmitted bytes. The standalone stdlib inspector
checks envelope bounds, metadata, resource CRC/SHA and shaped declarations. It
never claims signature verification: only the device's configured trust table
authenticates the package. Production trust keys remain unchanged.

`jellyframe_cli.py device install --bundle <absolute.jfsg>` forwards the envelope
without repackaging. USB chunks remain 1024 bytes; progress events are coalesced
to stay within the provider JSONL contract. This local provider version is not a
claim of signed-package device execution or provisioning qualification.

## Test Fixtures

`jellyframe_device.py --fixture <name>` is test-only and requires no board.
It provides deterministic contract fixtures for `no-device`, `image-mismatch`,
`transport-unavailable`, `storage-full`, `interrupted-install`,
`confirmed-cancel`, `unconfirmed-cancel`, `bounded-logs`, `lifecycle-ok`, and
`lifecycle-failed`. `lifecycle-ok` declares the exact capability order used by
the WS147 provider and is only a contract fixture; it does not claim a
physical endpoint or installed-app execution.
