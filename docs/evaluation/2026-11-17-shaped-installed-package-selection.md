# Explicit installed shaped-text selection (2026-11-17)

Status: local, unpublished, production lock/defaults/trust unchanged. This adds
package-selected routing to the bounded ABI4 DOM/worker implementation described
in `2026-11-17-shaped-dom-worker-integration.md`. It does not complete migration.

## Declaration and trust

The source manifest and normalized JFAPP summary accept one optional top-level
`textPipeline`. Absence selects the ordinary scalar route. The only admitted
explicit profile is `jfsp1-block-v11`:

```json
{
  "kind": "jfsp1-block-v11",
  "font": "/font.jffont2",
  "catalog": "/catalog.jfcat",
  "companion": "/companion.jfshr",
  "tables": "/tables.bin",
  "eligibility": "/eligibility.bin",
  "sourceIdentity": "<64 lowercase hex characters, nonzero>",
  "profileIdentity": "<64 lowercase hex characters, nonzero>",
  "glyphCount": 30900,
  "resourceId": 555,
  "rasterSize": 16,
  "weight": 400,
  "selection": 0,
  "fontFamilyHash": 0
}
```

The identity strings above are placeholders, not a runnable manifest. All fields
except `fontFamilyHash` are required; its default is zero. Five distinct normalized
package paths are bounded to 255 UTF-8 bytes. Integer bounds are glyphCount1..65536,
resourceId1..UINT32_MAX, rasterSize1..65535, weight1..1000, selection0..65535 and
fontFamilyHash0..UINT32_MAX. The declaration requires classic script mode. Device
parsing rejects duplicates, unknown fields, malformed identities and unsupported
profiles, both at inspection and launch. It clears launch output on refusal.
The packager validates all five resources exist and are nonempty before output,
including `--validate-only`; resource content validation remains Core's job.

`select_active_text_pipeline` rereads the active immutable lease's summary. A
shaped declaration requires an authenticated nonzero signer key id, authenticated
SHA-256 for every resource, and successful Core Binding construction from exactly
that lease's bytes. Package identities do not replace authenticated digests.
Missing metadata, altered/missing resources, unavailable APIs and malformed
summaries return refusal with no selected frame version or Binding. Explicit
shaped requests never downgrade in this updated Runtime.

The container now enforces the required execution mode: ordinary scalar packages
use header format0; shaped packages use format1, which old format0 readers refuse
before parsing the declaration. New readers require the format/declaration pair
to agree. Loose desktop manifests with `textPipeline` and static C++ resource
table output refuse until those paths have a shaped connection. See
`2026-11-17-shaped-container-compatibility.md` for the legacy-reader experiment,
current build identity and remaining installation/tooling gates. Neither the
existing release number nor production dependency lock was changed here.

## Device connection

The lifecycle endpoint selects the route once before task startup. A ready result
pins the Binding in the session and resource owner; worker production and UI
decoding both select v11. Ordinary packages select the existing port version.
Worker and UI receive the same immutable catalog/Binding. The v11 owner drops an
unused image adapter; image DOM remains explicitly unsupported by shaped layout.
Task admission rejects unselected, failed, mismatched-version or unavailable
routes. It has no implicit `NoActiveLease`/default route.

The inspection workspace is allocated off the lifecycle task stack. The device
selector clamps aggregate asset and Core scratch caps to 2 MiB. This bounds
admission/storage categories, not peak copies, parser allocations, fragmentation
or product reserve. Device peak allocation, OOM recovery and latency remain open.

## Host checks

- Complete shaping-ON Runtime/Core matrix:82/82 pass.
- Complete OFF matrix:38 pass,3 configured skips; no failures.
- After tightening unselected-route refusal:ON targeted7/7 and OFF targeted5/5.
- Packager regression:96 total,95 pass,1 optional skip.
- Tests cover absent scalar declaration; no lease; unavailable API; invalid
  launch summary; absent signer/digest; bounded resource refusal; authenticated
  selection; real worker and changed final-line packets; old-frame immutability;
  visible glyph pixels after package/worker/session release.
- Actual port resource-owner/session/take/render helpers are exercised with BMP
  adapter both enabled and disabled, plus all four CoreText/BMP admission modes.

The lease tests inject authenticated metadata through a test provider. They are
not an actual signed-install or device acceptance. Separate actual-crypto host
evidence remains in `ports/esp32s3-idf/tests/package_signature/README.md`.

Logs: `E:/Temp/runtime-shaped-selection-{on,off}-ctest*-20261117.log`,
`runtime-shaped-selection-{on,off}-last-ctest-20261117.log`, and
`runtime-shaped-selection-tools-20261117.log`.

## Historical package-selection IDF compile/link

This checkpoint predates the mandatory format1 header. Its frozen artifacts
remain valid evidence for that earlier source only. The newer format1 build is
recorded in `2026-11-17-shaped-container-compatibility.md`.

Build: `E:/Builds/runtime-shaped-worker-idf53-20261117`, IDF5.3.1 / Xtensa GCC13.2,
ABI4 Core source override, shaping/scripting/worker ON. An isolated SDKCONFIG
selects the lifecycle endpoint, CoreText,240MHz, exceptions,RTTI, PSRAM malloc
and console NONE. JerryScript is `D:/CodexTemp/jerryscript-abi4-ci-repro`.

The initial static band-shell profile linked successfully but GC removed the
worker route, so it was not accepted as worker evidence. The installed profile
also required enabling RTTI for the existing ABI4 SubmitEvent dynamic_cast.
The final ELF naturally retains all seven checked entry points: package
selection, installed task start, worker rebuild, Runtime layout_document, Core
layout_shaped/build_shaped and shaped frame capture. No forced linker roots.

Core canonical source hash:
`7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a` (185 files).
Each entry and aggregate identity was checked with the project's newline
normalization. Runtime source-manifest identity (418 files):
`9dd6867ff16009fbeb9f06f6d48ee75a185257f9c62198d3b5762a197dbe28e7`.

Final BIN4,514,464B (`0x44e2a0`), remaining3,874,144B in the8MiB app partition.
BIN SHA-256:
`87e593afd11b9019de881df1d58519c7edeca2e792ed699b32fcca3502506eb9`.
ELF SHA-256:
`820779be2ea037c04ee736d4154b7a1c0a8d912ca8b807c3626c46cb4c97f1a5`.
These sizes include a different entry point/configuration than the earlier
native-only image; their difference is not a shaping performance/size benchmark.

Machine evidence, checked symbols/compile units, input manifests, SDKCONFIG and
a retained BIN are under that build's `qualification/`. Build log:
`E:/Temp/runtime-shaped-worker-idf53-20261117-final-build.log`. Reproduction helpers:
`E:/Temp/build-shaped-installed-worker-20261117.ps1` and
`E:/Temp/qualify-shaped-installed-worker-20261117.py`.

No flashing, serial enumeration or physical acceptance took place. Next gates:
signed device install/reboot/tamper/fallback using test-only trust, real shaped
worker memory/refusal/latency, matching published full/cropped packages and CI,
then exact-image visual/touch acceptance before enablement.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
