# Full-resource signed store to shaped worker qualification

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

Local host execution is qualified for the admitted static-block v11 scope.
The actual signed store, authenticated Binding, JerryScript worker and installed
painter now execute one complete original-resource package. Host CTest passes
12/12; the shared store-fixture extraction also retains the prior capacity suite's
22/22. Actual IDF5.3.1 full-capacity installed-worker build and a matching provider
candidate archive pass. No serial endpoint was opened and no device flashed.
Production dependency lock, default partition/capacity and trust keys are unchanged.

## Full resources and capacity

The package retains all five actual shaping resources, plus HTML/CSS/JS/license:

| Resource | Bytes |
| --- | ---: |
| Regular16 JFFONT2, 4539 glyphs | 716274 |
| Regular catalog, 4686 raster GIDs | 733641 |
| Shaping companion | 41598 |
| Unicode14 tables | 50334 |
| Eligibility | 198 |

All app resources total1547203 bytes; JFAPP is1549404 bytes and the signed JFSG
is1550043 bytes. The fixture uses original assets, not padding or a reduced
one-glyph font. The generator checks source/profile and catalog/companion evidence;
the actual authenticated Core Binding validates resource contents and mappings.
Source identity is `a2b93e6c2db05d6bbbf6f27d413ec73269735b7b679019c8a5aa9670ff0ffbf2`;
profile identity is `7efac7babdc2da34475ffcb8ca985e5fb1e7da6544c7e58c401fed3679d58aa7`.

The previous3MiB storage profile gives1044480 bytes/slot and cannot fit this
package. The corrected generator refuses that capacity and retains its report;
the actual transaction also refuses before changing NOR bytes. Earlier padding
and placeholder qualification remains valid for its smaller workload only.

Separate opt-in `sdkconfig.ws147_shaped_full.defaults` selects8MiB app,1MiB
assets,6MiB raw store at0x910000, and512KiB coredump. The decoded table ends at
0xf90000 within16MiB. Effective capacity is
`min(2048*1024, floor((6*1024*1024-8192)/3) aligned down to4096) = 2093056`.
Moving assets/storage boundaries requires explicit data preservation and complete
partition migration/recovery. This is not deployable as an app-only update.

## Executed checks

The test generator signs with deliberately public P-256 scalar1 only under an
explicit test flag. The test-only store key router calls the actual IDF mbedTLS
adapter; no production key is provisioned. NOR and provider wire are host mocks.

- Real transaction Begin/chunk/verify/commit, reconstructed boot reauthentication,
  actual lease acquisition and five-resource Binding selection succeed.
- Signature mutation with recomputed transport CRC refuses commit and leaves the
  prior active version usable. Lower aggregate resource allowance refuses without
  invalidating the already selected Binding.
- HTML/CSS/JS and the scalar compatibility font catalog come through the installed
  lease. The selected contextual Binding is session-owned. The real worker emits
  v11 final-line packets and refuses scalar Text/PreparedText commands.
- Initial frame and three pointer-down/up click updates match independent Core
  DOM/layout final packets, baseline geometry, insertion positions and framebuffer
  pixels. JS changes text, count and width104/148; resulting wrapping moves the
  button. Latin ligature/kerning candidates, Chinese and combining-accent input
  participate in the real contextual service; this is not a browser-shaping oracle.
- An accepted initial frame paints identically after worker destruction, session
  teardown, source/resource/store release and simulated NOR erasure.
- Actual provider preparation/chunking reconstructs all1550043 bytes, including
  after replacing the input file after admission. JSONL has192 events, within its
  limits. Old capacity/missing shaping refuse before Begin. Host inspector does
  not establish signer trust; device authentication remains authoritative.

The five integration cases, one full-package provider test and six existing
actual-crypto cases total12/12. Existing signed-store/capacity suite22/22 is run
separately after extracting its shared host fixture. Integration cases use explicit
checks and compile with assertions active.

The initial fixture used a fixed button height outside the already documented
auto-height-only shaped scope and was correctly refused. Removing that fixture
property preserves line-height28 and passes. The harness also had to configure
its worker input inbox and use DOM hit geometry rather than optional target-key
records. These were fixture/harness corrections; no product admission was relaxed.
Declared package-input hashes select fresh fixture directories to prevent stale
signed packages in incremental builds.

## Target and candidate identity

Fresh IDF5.3.1 / Xtensa esp-13.2.0_20240530 CoreText240MHz shaping/installed-worker
build succeeds. Seven actual shaped route entry points are present in the ELF
without forced roots. Core source identity remains185 files,
`7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a`.

- BIN4515040 bytes, SHA-256 `32b1d236493173f9fba983531d59965604a9f61535c4b0ad908bac7ca927937c`.
- ELF SHA-256 `612dea8f6adff573a7c98b79466643b55b4f87d6a73ef577c3314ab74713a7ae`.
- Partition SHA-256 `190383880c5a02dd8eb7a651e1dba25f1a4af0b92fbd67c67b7706dd501e6b11`.
- Provider0.1.2-dev archive SHA-256 `fbff3dc9813397baca12b4c693cde3d39ef9af3a23c7dd72ec232eef7c4a5dcc`.
- Executed JFSG SHA-256 `61653ac786beb4f60dbb5693635c17a1422fe19f1e16515f6d976bd0bb13ce66`.

Provider checks generated config, binary partition and exact factory/app bytes;
its manifest reports2093056-byte capacity and passes independent JSON Schema
validation. First packaging attempt correctly refused an unnamed defaults seed;
reconfiguring/rebuilding with the named WS147 defaults establishes provenance.
No recorded cache/config was edited to bypass validation.

Builds: `E:/Builds/runtime-real-signed-shaped-20261117` and
`E:/Builds/runtime-shaped-full-idf53-20261117`.
Evidence: `E:/Experiments/migration-real-shaped-store-20261117/complete-result.json`,
verbose test logs, package/resource hashes, decoded partition, verified symbols,
35-file source snapshot, render images and candidate archive. Generator-only
`executionVerified:false` remains accurate; the separate integration result records
`hostExecutableExecutionVerified:true` and `deviceExecutionVerified:false`.
Reproduction: `ports/esp32s3-idf/tests/signed_shaped/README.md`.

## Remaining gates

Physical signed transport/install/reboot/tamper/fallback, actual heap peaks,
allocation-failure recovery, fragmentation/product reserve and latency are open.
This workload covers bounded generic static blocks, not mixed-inline/image/
effect/native-control shaping. Production trust/partition adoption, reviewable
PRs, newly versioned matching packages and hosted CI remain open. ABI9 canonical
identity and its contextual Layout/paint/Runtime route remain separate migration
work; this ABI4 host result does not complete the experimental migration.
