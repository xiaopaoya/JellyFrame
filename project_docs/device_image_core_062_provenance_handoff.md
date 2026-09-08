# Device Image Core 0.6.2 Provenance Handoff

> Last updated: 2026-09-07; Applies to: 0.6.0-dev

This handoff covers the JellyFrame Runtime `0.6.0-dev` line and its locked
Render Core `0.6.2` dependency.

This handoff records the evidence required for the accepted WS147 Developer
Image using Render Core `0.6.2`. It does not promote or mutate the historical
image.

## Current Boundary

Runtime `master` consumes signed Render Core `v0.6.2`, ABI `1`, source identity
`539a894519d3251f02c8b3aee8d0d0fb715bf49a732fc74126ccb2188462e3f0`, and archive
SHA-256 `d136a0d7fd7ab58436a5f2fa9c7eb27a497e08bad384a96cd93689ba6898f43e`.

The published WS147 baseline remains an immutable historical artifact using
Core `0.6.1`. A new candidate `0.6.2-ws147.1` has now been built from Device OS
revision `131ce8c15702eea6fff3187c10a0926ef21cfc98`. Its firmware SHA-256 is
`9a67aef07b833fe7f6be8ace4ce70a23eed58df33bb3cda4642d4c022a2ebb72`, factory
recovery SHA-256 is `7256568c5741d4131d526a25e4072eda49c68778b70746efdc99c86a29eb427e`,
manifest SHA-256 is `c118df34a7f98eee3efb0b1b711b78b9d69ff614ce1f2acb390a9d11447ef031`,
and the complete archive SHA-256 is
`687d57903e8c1565c966cf3c7c4f9eaf8ef2e5fecdf29a4e1ac28ae4ab8839b1`.
These identities must remain tied to the archived candidate; the historical
manifest and evidence must not be edited in place.

## Candidate Status

The candidate passes bounded entry/reconnect, rollback integrity, installed
script input (20 cycles), non-script lifecycle, explicit update/rollback/remove,
confirmed in-flight cancellation, chunk/commit power-loss recovery, malformed/
CRC/oversize/storage-full coverage, registry-corruption protected-launcher
recovery and mixed lifecycle (30 cycles). The complete Developer Image gate is
**PASS**. The controlled storage-refusal and load-failure images remain
acceptance fixtures only; they are not enabled product capabilities.

## Accepted Image Record

The accepted image was built from the merged Runtime commit containing the
`0.6.2` lock and the selected WS147 feature profile. Its delivery contains:

- firmware and factory-recovery image from the same source/configuration;
- a new image version and source revision, never reused historical hashes;
- a manifest declaring Runtime `0.6.0-dev`, Core `0.6.2`, ABI `1`, actual
  display/profile/features and new firmware/recovery hashes;
- Provider discovery and identity responses regenerated from that exact image;
- one release record containing manifest, hashes, Provider version and Core provenance.

## Evidence Gates

- Generated Runtime/Core provenance reports `0.6.2`, ABI `1` and the expected source identity.
- Standalone, package-consumer and source-override Runtime tests pass against Core `0.6.2`; these do not prove firmware behavior.
- Strict manifest parsing and Provider matching reject any version, ABI, profile, display, capacity or feature-family mismatch before installation.
- A clean WS147 verifies flash, reconnect, discovery, identity and App listing.
- Script and non-script Apps verify install, launch, stop, update, rollback, remove, cancellation and reboot recovery.
- Panel/input and frame/present evidence is repeated. Existing Core `0.6.1` image evidence cannot close this gate.

The image is not releasable if its manifest was edited after build, Provider
identity was hand-written, or a report mixes old image and new Core versions.

## Next Owner Action

The port maintainer should retain the complete report and its exact artifact
hashes as the release record. Provider delivery metadata may now identify this
candidate as the accepted `0.6.2-ws147.1` Developer Image. The historical image
remains accepted only under its `0.6.1` identity.
