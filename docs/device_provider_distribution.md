# Device Provider Distribution

## Decision

Device Providers are distributed as separate, board-specific Developer Image
packages. They are not part of the App Author SDK and are not copied into the
VS Code extension. The SDK is a host authoring/runtime dependency; a provider
owns the transport adapter, the matching Developer Image identity, recovery
image and board-specific installation instructions.

Each provider release is a GitHub prerelease with:

- one versioned ZIP containing `provider/`, `developer-image/` and `recovery/`;
- `SHA256SUMS.txt` inside the ZIP;
- a separate `<archive>.sha256` Release asset;
- a release tag that identifies the board/provider line, such as
  `device-provider-ws147-v0.1.1-dev`;
- a report and manifest that record the firmware, Runtime, Render Core/ABI,
  board display, transport and physical acceptance evidence.

The archive is intentionally not a general-purpose installer. It does not
scan serial ports, infer endpoints or silently flash a device. The provider
configuration remains explicit, and the Developer Image manifest is used to
reject an incompatible device/image pair.

## Official Catalog

The machine-readable catalog is
[`tools/device-providers/catalog.json`](../tools/device-providers/catalog.json).
Each entry pins the GitHub release tag, exact asset name and SHA-256, and
declares the board, display, Runtime, Render Core ABI and host platform. The
VS Code extension fetches this catalog over HTTPS, resolves the exact Release
asset, verifies the catalog digest, and only then extracts it.

The catalog is a distribution index, not a compatibility override. A provider
must still validate the selected device's returned identity against its
Developer Image manifest.

## Current Entry

`WS147 Developer Image` is published as
`device-provider-ws147-v0.1.1-dev` and targets:

- board: `ws147`;
- display: `172 x 320`, `rect-172x320`;
- Runtime: `0.6.0-dev`;
- Render Core: `0.6.2`, ABI `1`;
- provider: `0.1.1-dev`;
- host: Windows x64;
- archive SHA-256:
  `4ccb9ccb8589d473e08ddf9890d95d102101d0d46f8126eeab37c0805fef64f7`.

## Extension Workflow

1. Run `JellyFrame: Install Provider`.
2. Select a catalog entry (required even for a single entry), then an installation parent directory.
3. The extension downloads the exact Release asset, checks SHA-256 and safely
   extracts it without Python.
4. It writes absolute `jellyframe.deviceProvider` and
   `jellyframe.deviceManifest` settings.
5. Copy the provider configuration example to the provider configuration file,
   set the physical port, and run `JellyFrame: Discover Device`.

The provider executable still requires its documented host dependency when run
outside the SDK-managed CLI. The extension does not embed board-specific Python
or `pyserial` into the provider package.
