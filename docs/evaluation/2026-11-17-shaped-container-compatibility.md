# Mandatory shaped-container compatibility (2026-11-17)

Status: local, unpublished. Production defaults, dependency lock and trust keys
are unchanged. This follows the installed selection and DOM-worker slices;
it does not qualify device execution or complete experimental migration.

## Execution requirement in the header

Adding an optional JSON declaration was insufficient: old readers skip unknown
`textPipeline` fields, and Runtime minimum-version checks are not uniformly
enforced by all loaders. A shaped package could therefore execute on a scalar
path with different geometry and painting.

The existing eight-byte `JFAPPV0\0` family magic and 56-byte header remain. The
little-endian u16 format field at byte10 carries the mandatory execution profile:

| Header format | Summary | Updated installed reader | Old format0 reader |
| --- | --- | --- | --- |
| 0 | no `textPipeline` | scalar route | scalar route |
| 1 | valid `jfsp1-block-v11`, classic script | authenticated shaped selection | unsupported version |
| 0 | shaped declaration | refuse | producer never emits this combination |
| 1 | no declaration | refuse | unsupported version |
| other | any | refuse | unsupported version |

The source manifest's own `formatVersion` remains0; it describes the JSON schema,
not the binary header format. Header format1 keeps the existing section layout,
CRC and reserved/flag constraints. A successful package inspection does not imply
that a target has the required Core API or authenticated font resources.

The packager writes the format from its validated declaration. Device inspection
checks the pair; launch rereads the header and declaration before publishing a
selection and clears its output on refusal. Existing immutable-lease signer and
resource-digest requirements still apply. CRC is an integrity check, not signature
authentication; the signed envelope binds the exact original container bytes,
including this format field.

The Python registry validates both formats for storage/inspection. Desktop native
loading remains scalar-only: it refuses format1, and now also refuses a top-level
`textPipeline` in a loose/source manifest. Static `--output-cpp` is refused for
shaped manifests because generated resource tables do not install an execution
route. The CLI regression verifies refusal occurs before any C++, bundle or
report output is created. Installed bundle output and validation remain available.

## Verification and limits

An independent harness compiled unmodified `device_bundle.{h,cpp}` extracted from
Runtime revision `c0fe1cb3`. The current packager's real scalar and shaped bundles
were fed to that reader: scalar succeeds; shaped refuses `UnsupportedVersion`
before reading the declaration. CTest1/1 passes. This directly verifies that
baseline; other historical releases were not individually rebuilt.

Baseline source identities and fixtures: `E:/Temp/shaped-legacy-gate-20261117`.
Build: `E:/Builds/shaped-legacy-gate-20261117`. Reproduction generator:
`E:/Temp/verify-shaped-legacy-gate-20261117.py`; logs:
`E:/Temp/shaped-legacy-{configure,build,ctest}-20261117.log`.
The baseline sources, fixtures, final logs, build evidence and reproduction
helpers are also retained with a SHA-256 manifest under
`E:/Experiments/migration-shaped-format1-20261117`; they do not depend on keeping
the temporary copies. The retained BIN stays in the build qualification folder.

Current C++ tests cover valid scalar/shaped pairs, both mismatches, unknown
versions and launch refusal clearing output. Python mutations recalculate CRC,
so rejection tests the format contract rather than a stale checksum. Native
source and binary refusal are exercised with both current and bundled Core.
Scalar fixtures use the configured release lines; product version checks are
not relaxed for the bundled older Core.

- Shaping-ON full Runtime/Core matrix:83/83 pass. Rebuilt native route test1/1
  after making the fixture work against both Core release lines.
- Bundled ABI3/shaping-OFF baseline:38 pass and3 configured skips, plus rebuilt
  native route1/1. These are legacy compatibility results, not a matched ABI4
  feature-OFF qualification.
- The separate matched ABI4/feature-OFF build keeps scripting and the worker ON.
  It exposed an unguarded reference to the shaping-only owner in
  `ScriptTaskAcceptedAppFrame::valid()`. The corrected OFF branch requires an
  empty shaped sidecar; the ON branch still requires its exact retained owner.
  ON affected tests7/7 pass after rebuilding; matched OFF full CTest80/80 pass
  with no skipped tests.
- Packager:98 total,97 pass and1 optional skip. Registry:24 total,23 pass and1
  optional skip.

Logs: `E:/Temp/runtime-shaped-format1-{on-full-ctest,off-full-ctest,off-native-ctest-fixed,packager,registry}-20261117.log`.
Matched ABI4/OFF build: `E:/Builds/runtime-shaped-format1-abi4-off-20261117`;
log: `E:/Temp/runtime-shaped-format1-abi4-off-ctest-20261117.log`.

The existing packaged WS147 provider is still format0-only and has a327680-byte
bundle limit. It does not transport this new profile or its larger font catalog.
It fails closed; do not present its installation path as qualified for format1.
Signed envelope transport, suitable storage limits and test-only trust need their
own matched end-to-end qualification. Desktop shaped execution also remains open.

## Current ESP-IDF compile/link identity

Fresh build: `E:/Builds/runtime-shaped-format1-idf53-20261117`, IDF5.3.1 / Xtensa
GCC13.2. Isolated lifecycle-entry configuration, shaping/scripting/worker enabled,
CoreText,240MHz, exceptions,RTTI, PSRAM malloc and console NONE. The final ELF
naturally retains installed selection/start, worker rebuild, Runtime layout and
Core shaped layout/paint/frame entry points. No forced linker roots.

Core canonical source hash (185 files):
`7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a`.
Runtime source-manifest hash (418 files):
`0dd652237e14872e367bd3460ee3f260a6d18b08d05d52d2407e78df875ff717`.
The machine evidence defines the exact source set; it is not a repository commit.

BIN4,514,960B (`0x44e490`); remaining3,873,648B in the8MiB app partition.
BIN SHA-256:
`84137358d38d7391050a50fb44de5125beadf5dd9dd70ee00a3c5a6fc277bf65`.
ELF SHA-256:
`fa02825c05749080c2217f8950aab3159e924417d1bc1f96560efb8be40c693a`.
The partition figures do not establish heap, allocation peaks or product reserve.

Checked configuration, compile units, symbols, source manifests and a retained
BIN are in this build's `qualification-final/`. Build log:
`E:/Temp/runtime-shaped-format1-idf53-build-final-20261117.log`. Qualification helper:
`E:/Temp/qualify-shaped-format1-worker-final-20261117.py`. This build's initial
`qualification/` and the preceding installed-worker build were not overwritten;
they predate the OFF-guard correction and have different source/BIN hashes.

No flashing or serial enumeration occurred. Remaining gates: signed positive
install/reboot/tamper/fallback, actual shaped-worker memory/OOM/recovery/latency,
matched published full/cropped packages and CI, and exact-image device acceptance
before enablement. Closed ordinary ABI4 generation200 acceptance stays closed.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
