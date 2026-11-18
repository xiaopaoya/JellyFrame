# Signed WS147 provider transport and capacity matching

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

Local candidate only. Provider 0.1.2-dev can inspect and transport signed JFSG
packages, including explicitly admitted JFAPP format1. This closes the host
transport/capacity-manifest gap; it does not qualify physical installation or
shaped worker execution. No serial endpoint was opened and no device flashed.
Production dependency lock, partition defaults and trust allowlist are unchanged.

## Behavior

- The standalone `tools/device_install_package.py` uses only stdlib. It bounds
  one immutable read by the attested target capacity, checks the JFSG header,
  canonical manifest metadata/resource digests, inner JFAPP CRC/index/payload,
  and required shaped metadata/nonempty assets. The identical bytes populate
  InstallBegin length/CRC and all USB chunks. A file replacement after admission
  cannot change transmitted bytes.
- `signatureVerified` is always false. The inspector neither verifies ECDSA nor
  trusts a key. A changed signature can reach the device, whose commit refusal
  remains an install failure. The real cryptographic/store qualification is
  separate: `2026-11-17-signed-install-host.md`.
- Scalar0 unsigned/signed host transport remains supported. This does not grant
  unsigned installation permission in the current signed store. Shaped1 requires
  JFSG and exact device/manifest `text.shaping` identity. The additive JFDP family
  uses bit6; unknown bits still refuse, and existing public layouts/wire sizes
  are unchanged. The lifecycle advertises it only when Core shaping is compiled.
- `jellyframe_cli.py device install` accepts an absolute `.jfapp`/`.jfsg` and
  forwards it without repackaging. The provider prepares its local cancellation
  session before remote InstallBegin. USB chunks remain 1024 bytes; progress
  output is coalesced to remain within 256 KiB/1024-event JSONL limits.

## Release identity and capacity

A shaping release requires `--build-dir`; historical scalar releases without it
retain their legacy 327680-byte manifest path. Build-directory releases cross-check
sdkconfig against generated `config/sdkconfig.h`, validate binary partition MD5,
bounds/non-overlap and factory/storage identities, then compute:

`min(configuredKiB * 1024, floor((storageBytes - 8192) / 3) aligned down to 4096)`.

Manifest provenance includes partition SHA-256, configured byte cap and storage
offset/size as a complete group. The parser checks this group's agreement with
`storage.maxBundleBytes`; schema and English/Chinese contracts are updated.
The supplied complete 16 MiB factory image must contain the exact build partition
bytes and app bytes at its actual partition offset. Host consistency is not a
running-device attestation. IDF compatibility aliases can repeat identical
sdkconfig values; conflicting assignments refuse.

The opt-in layout gives 1044480 bytes/slot in 3 MiB storage at 0xc10000. It moves
partition boundaries and requires explicit data/partition migration. Never deploy
this layout by app-only flashing.

## Verification

Eight Python suites pass 68/68: signed provider12, offline signer5, existing
provider12, provider release6, image manifest5, provider contract16, client6,
CLI6. Tests use real packager/signer envelopes, including deterministic 900 KiB
padding with placeholder shaping assets. Physical transport is mocked; no claim
is made that placeholder shaping resources execute.

Coverage includes exact-byte chunk reconstruction and Begin CRC, file replacement,
legacy scalar and signed0/1, target-capacity/shaping refusal before Begin,
malformed envelope/resource metadata, recomputed outer CRC, empty shaped assets,
device commit signature refusal, bounded/contract-parsed JSONL, bit6/unknown-bit,
config-vs-physical capacity, stale/conflicting config, partition MD5/bounds and
mismatched recovery images. Existing session-order checks remain passing.
Release tests import the bundled stdlib inspector with an isolated interpreter,
exercise shaping build evidence and reject missing capacity provenance.

The C++ protocol executable was compiled with assertions active and passed,
including the new feature round-trip and existing invalid-payload corpus.
Actual IDF5.3.1 CoreText/shaping/installed-worker rebuild passed, with app size
4515056 bytes (46% app partition free). A candidate archive was generated from
that exact build and independently passed the JSON Schema validator.

- BIN SHA-256: `3bc9f762344b0aae397604f4672a19c9a50ff678bebfc29034a5902f58876b32`
- ELF SHA-256: `6fcda19f7ed6b6c2ea4df96047182198dc949fae715810d5fbf0323d9a92e273`
- Partition SHA-256: `966223411d2ac264b724d3768afa4ab97bd425158f3b508fbf3e87ca3b067229`
- Final archive SHA-256: `2ec7b3422d92ee3a4fd05efdfdd0f02c6f0cac8ad9ad04d26049ecd40b2128e1`

Evidence: `E:/Experiments/migration-signed-provider-20261117/result.json`,
`python-results-final.json`, per-suite final logs, protocol executable/build log,
configuration/compile commands/source snapshots, and
`release-candidate-final/jellyframe-ws147-developer-0.6.2-ws147.1-provider-0.1.2-dev`.
Earlier candidate directories are intermediate local artifacts, not release gates.
The prior capacity-only build/BIN remains frozen in its separate experiment.

## Remaining migration gates

Build a real executable five-resource shaping package, then qualify the actual
store-to-Binding-to-worker-to-paint route. Physical signed install/reboot/tamper/
fallback, peak memory/OOM/recovery/latency and explicit partition deployment are
still open. Production provisioning and matched published packages/hosted CI
remain separate review gates; none are implied by this local candidate.
