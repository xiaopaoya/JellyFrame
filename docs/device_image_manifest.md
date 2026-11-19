# Developer Image Manifest V0

> Last updated: 2026-11-17; Applies to: 0.6.0-dev; status: contract baseline

Each releasable first-party Developer Image publishes one immutable JSON
manifest beside its firmware and recovery materials. It is the common identity
record for Device OS release tooling and the future `jellyframe-device`
provider. It is not a JFDP message, an App manifest, a board driver
configuration or an install command.

The canonical schema is
[`tools/schemas/jellyframe.device_image.schema.json`](../tools/schemas/jellyframe.device_image.schema.json).
`tools/device_image_manifest.py` supplies the strict Runtime-side parser and
the provider-device compatibility check. Both reject unknown fields and
duplicate JSON members.

## Required Identity

```json
{
  "format": "jellyframe.device-image",
  "formatVersion": 0,
  "imageId": "org.jellyframe.ws147.developer",
  "imageVersion": "0.1.0-dev",
  "runtimeVersion": "0.6.0-dev",
  "renderCore": { "version": "0.6.1", "abi": 1 },
  "source": {
    "revision": "<40-lowercase-hex>",
    "firmwareSha256": "<64-lowercase-hex>"
  },
  "board": {
    "id": "ws147",
    "display": { "width": 172, "height": 320, "shape": "rect" }
  },
  "profile": {
    "id": "rect-172x320",
    "featureFamilies": ["core.document"]
  },
  "transport": { "protocol": "JFDP/1", "kind": "usb-serial-jtag" },
  "storage": { "maxBundleBytes": 327680 },
  "recovery": {
    "procedureId": "ws147-usb-recovery-v1",
    "factoryImageSha256": "<64-lowercase-hex>"
  }
}
```

`firmwareSha256` identifies the exact image tested and released. The recovery
hash identifies the factory image described by the procedure named in
`procedureId`; it is not an instruction to erase or flash a device. The actual
procedure is a versioned Device OS release document and must state host tools,
target board, confirmation steps and recovery outcomes.

The optional boolean `storage.requiresSignedBundle` declares a signed-only
install store. If true, the provider rejects a raw JFAPP before `INSTALL_BEGIN`
after matching the device to the manifest. Absence or false adds no host
restriction. The firmware still authenticates every package according to its
own policy. Use the provider and manifest from the same archive; older strict
manifest readers do not recognize the field.

## Optional install-capacity provenance

When a release supplies `buildProvenance.partitionTableSha256`, it must also
supply `configuredMaxBundleBytes`, `storagePartitionOffset` and
`storagePartitionBytes`. The parser checks their bounds/alignment and requires
`storage.maxBundleBytes` to equal the smaller of configured capacity and the
physical three-slot capacity after two 4096-byte registry sectors. Release
tooling checks binary partition MD5/SHA, generated configuration and factory
partition/application bytes. These are host consistency checks, not firmware
attestation or a device signature-verification result. Historical manifests
without this optional group retain their original meaning.

The JFDP identity feature family `text.shaping` uses bit6. Its release requires
build-directory capacity evidence; unknown feature bits are rejected. Signed
`.jfsg` installation remains subject to device trust policy.

## Optional install-capacity provenance

When `buildProvenance.partitionTableSha256` is supplied,
`configuredMaxBundleBytes`, `storagePartitionOffset` and `storagePartitionBytes`
are required with it. Bounds/alignment are checked, and `storage.maxBundleBytes`
must equal min(configured capacity, physical three-slot capacity after two
4096-byte registry sectors). Release tooling checks binary partition MD5/SHA,
generated configuration and factory partition/application bytes. These host
consistency checks do not attest running firmware or verify signatures.
Historical manifests without this group retain their original meaning.

JFDP identity feature family `text.shaping` uses bit6 and requires build-directory
capacity evidence for packaging. Unknown feature bits are rejected. Signed
`.jfsg` installation remains subject to device trust policy.

## Provider Matching

Before a CLI or editor deploys an App, it must parse the provider's discovery
result using `device_provider_contract.py`, then require an exact match for
board ID, profile ID, image version, Runtime version, `JFDP/1`, display shape
and dimensions, maximum bundle bytes and the set of feature families. A
provider's current `availableStorageBytes` remains dynamic and is deliberately
not an image identity field.

A mismatch is a provenance or compatibility failure. Tooling must present it
as such and must not infer a serial endpoint, change the board's manifest or
attempt an install. The physical provider itself remains Device OS work; this
contract gives it a stable, testable input without importing USB, serial or
board dependencies into JellyFrame Runtime.

## WS147 Published Baseline

The WS147 Developer Image manifest and factory procedure are now published and
physically verified. The 2026-08-19 baseline is
`org.jellyframe.ws147.developer@0.1.0-dev`, source revision
`fbf10784ac8ce38f41ced40fa013a43564c992c8`, Runtime `0.6.0-dev`, Render Core
`0.6.1` / ABI `1`, and profile `rect-172x320`. Its firmware SHA-256 is
`e7eb9b16cce5d9e781fc93717826192781b652704e91d1859f353f74e5cfaacc`; its
16 MB factory image SHA-256 is
`6f3360753422c60ba32a1cee92fd01575e64ae41433acc31b9fb35d527bad2e1`.

The strict manifest/provider check and a complete factory write at offset zero
were verified on WS147: recovery selected the protected launcher with
`RegistryInvalid`, then a JFDP lifecycle fixture was installed and launched
without reflashing. This closes the manifest/factory-recovery A1 sub-item only.
A future image revision needs a new manifest, hashes and compatibility
assessment; it cannot reuse this image identity.

## Core 0.6.2 Handoff Status

Runtime `master` consumes the signed Render Core `v0.6.2` release. The published
`0.1.0-dev` baseline above remains an immutable `0.6.1` artifact and must not be
edited in place. A Core `0.6.2` WS147 candidate, `0.6.2-ws147.1`, is archived in
`core062-developer-image-final-20260905` with image source revision
`131ce8c15702eea6fff3187c10a0926ef21cfc98`, firmware SHA-256
`9a67aef07b833fe7f6be8ace4ce70a23eed58df33bb3cda4642d4c022a2ebb72`, factory
recovery SHA-256 `7256568c5741d4131d526a25e4072eda49c68778b70746efdc99c86a29eb427e`,
manifest SHA-256 `c118df34a7f98eee3efb0b1b711b78b9d69ff614ce1f2acb390a9d11447ef031`,
and archive SHA-256 `687d57903e8c1565c966cf3c7c4f9eaf8ef2e5fecdf29a4e1ac28ae4ab8839b1`.
The candidate has passed bounded entry/reconnect, rollback integrity, installed
script input, non-script lifecycle, explicit update/rollback/remove, confirmed
in-flight cancellation, chunk/commit power-loss recovery, malformed/CRC/
oversize/storage-full coverage, registry corruption recovery and 30-cycle
mixed lifecycle evidence. The complete Developer Image gate is **PASS**. The
controlled refusal and load-failure cases are acceptance fixtures only, not
product capabilities. The full handoff is defined in
[`project_docs/device_image_core_062_provenance_handoff.md`](../project_docs/device_image_core_062_provenance_handoff.md).
