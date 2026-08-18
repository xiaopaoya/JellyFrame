# Developer Image Manifest V0

> Last updated: 2026-08-19; Applies to: 0.6.0-dev; status: contract baseline

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
  "renderCore": { "version": "0.6.0", "abi": 1 },
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

## WS147 A1 Handoff

The WS147 port owner must publish this manifest with the board/profile release
and factory recovery procedure. Its values must correspond to the physical
image, not a generic fixture. For the accepted persistent lifecycle baseline,
record the source lineage through `743a011` and preserve the lifecycle report's
firmware and bundle hashes. A future image revision needs a new manifest and a
compatibility assessment; it cannot reuse the old image identity.
