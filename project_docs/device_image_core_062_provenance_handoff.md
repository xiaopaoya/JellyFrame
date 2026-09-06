# Device Image Core 0.6.2 Provenance Handoff

> Last updated: 2026-09-06; Applies to: 0.6.0-dev

This handoff covers the JellyFrame Runtime `0.6.0-dev` line and its locked
Render Core `0.6.2` dependency.

This handoff defines the evidence required before a new WS147 Developer Image
may claim Render Core `0.6.2`. It does not promote or mutate the existing image.

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
`cdc7385ed08e37e322d39d734948781106205af29cd756fb56727c4243cb2e4e`.
These identities must remain tied to the archived candidate; the historical
manifest and evidence must not be edited in place.

## Candidate Status

The candidate passes bounded entry/reconnect, rollback integrity, installed
script input (20 cycles) and mixed lifecycle (30 cycles). Its report remains
**partial** because non-script lifecycle, explicit update, confirmed in-flight
cancellation, chunk/commit power-loss recovery, malformed/CRC/oversize/
storage-full coverage and registry-corruption protected-launcher recovery have
not yet been evidenced.

## Required Image Change

The next image must be built from the merged Runtime commit containing the
`0.6.2` lock and the selected WS147 feature profile. The delivery must contain:

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

The port maintainer should complete the listed remaining cases against this
candidate. After they pass, update manifest/recovery and Provider delivery
metadata together in one reviewed change. Until then, tooling may accept the
candidate only as a partial `0.6.2` image, while the historical image remains
accepted only under its `0.6.1` identity.
