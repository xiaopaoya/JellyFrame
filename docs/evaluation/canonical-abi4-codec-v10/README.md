# Canonical ABI4 Frame Codec v10 Validation

This Runtime candidate adds canonical v10 elliptical-radius transport. V9 keeps
its existing wire version, record widths, reserved-zero requirements, and
decoded `-1` Y-radius sentinel. V9 encoding accepts a Y sentinel or a Y value
equal to X; distinct Y values fail closed. V10 uses the command and clip
reserved words for normalized encoded Y radii and adds one 32-bit Y radius
to each group record (116 bytes, versus 112 bytes in v9).

Each axis accepts a nonnegative uniform radius or a signed per-corner encoding
whose high nibble is `0x8` and whose remaining 28 bits hold four 7-bit radii.
Only `-1` is the in-memory Y fallback sentinel; v10 replaces it with the encoded
X axis on the wire. Reserved high bits and wire sentinels are rejected.

Earlier focused validation used this preliminary Core F5 package snapshot:

- Package: `0.9.0-dev`
- Profile: `render-core-default`
- Source hash: `ad780488024780d0fdc518bdf51d23847b7e76153a2e92d5c028e2721bfb9fb7`

The focused consumer staged only `src/app_runtime` so `render_core/*` resolves
from the selected Core package. It built and ran with:

```powershell
cmake -S build-f5-canonical-v10/focused -B build-f5-canonical-v10/focused/build `
  -DCMAKE_PREFIX_PATH=D:/CodexTemp/migration-followup/Runtime-f5-canonical-v10/build-f5-canonical-v10/core-package
cmake --build build-f5-canonical-v10/focused/build --config Release `
  --target runtime_f5_canonical_tests --clean-first -j 8
.\build-f5-canonical-v10\focused\build\Release\runtime_f5_canonical_tests.exe
```

The executable passed the canonical v9 wire/refusal suite (including 1,582
truncation cases), direct-Core versus decoded-renderer pixel comparison,
canonical v10 signed per-corner Basic/Text/Image payload and group/clip round
trips, v9 equal-axis byte identity and lossy-encode rejection, and the frame
codec and renderer suites. The signed-axis tests also compare both canonical
replay and flattened replay to direct Core pixels, prove a circular control
produces different pixels, check Y-only damage, and compare every input hit in
the viewport with elliptical clip coverage. All Y wire slots reject the `-1`
sentinel and reserved high bits without modifying the output frame. Legacy v4
encoding preserves representable bytes and rejects distinct Y axes.
The executable is produced at
`build-f5-canonical-v10/focused/build/Release/runtime_f5_canonical_tests.exe`.

Two additional focused consumers passed after these changes:

- ABI3 package `D:/CodexTemp/migration-followup/abi3-core-install`: frame codec
  and renderer suites, with canonical v9/v10 unavailable. This selected Core
  does not expose v5 sampling capability, so positive v5 byte qualification
  remains unexercised.
- Older canonical package `D:/CodexTemp/migration-followup/installs/f4-full`:
  v9 capture/encode/decode/render and pixel smoke check, v9 capability present,
  v10 capability absent, and v10 encode refusal.

Clang syntax checks also passed for codec, renderer and codec tests against
both vendored legacy headers and the older canonical Core headers. The
consumer harness routes CRT assertions to stderr and disables report dialogs.

The resumed qualification built the actual script backend with
`JELLYFRAME_BUILD_SCRIPTING=ON` and `JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON` in
`E:/Builds/runtime-f5-worker-resume`. Both `jellyframe_script_tests` and
`jellyframe_script_task_runtime_tests` passed. This uses JerryScript commit
`b7069350c2e52e7dc721dfb75f067147bd79b39b` at
`E:/Workspaces/JellyFrame/third_party/jerryscript`, its `jerry-core/include`
headers and `build/lib/MinSizeRel/jerry-{core,ext,port}.lib` libraries.
The optional runtime target now includes `runtime_font_catalog.cpp` and
`runtime_image_provider.cpp`, required by the canonical worker path.

The real worker tests run both v9 and v10 initialization/catalog validation and
canonical group publication. The v10 test initializes signed per-corner X/Y
radii, publishes and consumes a frame, applies a supervisor-validated JavaScript
Y-only style mutation, publishes and consumes frame 2, checks exact paint and
clip-group axes/catalog identity, checks retained damage, and replays the
accepted frame after worker teardown. The test verifies the mutation changes
pixels and logs `worker v10: signed axes, supervisor-gated mutation,
publish/take, damage and retained replay passed`.

The checked-in `CMakeLists.txt` and `main.cpp` in this directory reproduce the
installed-package qualification without the old ignored build harness. By
default only `src/app_runtime` is staged. `BUILD_V10_WORKER=ON` additionally
stages Runtime script and device-contract sources. Neither mode stages the
bundled `src/render_core` tree. All Core symbols and headers come from the
installed package. Configuration requires matching expected source hash and
profile plus ABI4; the entry point statically requires independent radius-axis
support, so an older v9 Core cannot silently skip the v10 tests. Wrong expected
source hash and profile were each confirmed to fail configuration.

A fresh frozen local Core install and this tracked consumer passed 2/2 CTest targets
in each of the full and cropped profiles:
`installed_runtime_canonical_v10` and `installed_runtime_canonical_v10_worker`
(the latter includes ordinary script API tests). The qualified local source is:

- Core commit: `8b2c2f9d114458d7a2956b228a5dd87712770463`
- Package/ABI: `0.9.0-dev` / `4`
- Configured source hash: `369973a9b051cdff8af56f5bd11a94cd17c75de6940473d2fa6dcee8b171c33c`
- Source files: `139`
- Full prefix/profile: `E:/Builds/runtime-f5-installed-core-final-full/install` / `render-core-default`
- Cropped prefix/profile: `E:/Builds/runtime-f5-installed-core-final-cropped/install` / `render-core-minimal-no-forms-advanced`
- Identity records: `E:/Builds/runtime-f5-installed-consumer-final-{full,cropped}/qualified_core_identity.txt`

The cropped Core has Canvas2D, modern paint, flex/grid and advanced forms
disabled. Its codec test explicitly verifies atomic refusal of unsupported
modern paint before comparing supported image/text/group pixels. Both profiles
run the v10 signed-axis and actual worker mutation tests. The source hash above
identifies the local frozen Core commit; merged-package qualification remains
separate. The earlier resumed full-profile run used source snapshot
`3155577363b2ee6fa308b30ccc786fec1ccb2ec8368bf57786af6bb5efddf4be`.

The tracked consumer was run as follows for each profile, selecting the
corresponding expected profile ID:

```powershell
. E:\Start-Development.ps1 -Project JellyFrame
cmake -S D:/CodexTemp/migration-followup/Runtime-f5-canonical-v10/docs/evaluation/canonical-abi4-codec-v10 `
  -B E:/Builds/runtime-f5-installed-consumer-final-full `
  -DJellyFrameRenderCore_DIR=E:/Builds/runtime-f5-installed-core-final-full/install/lib/cmake/JellyFrameRenderCore `
  -DEXPECTED_CORE_SOURCE_HASH=369973a9b051cdff8af56f5bd11a94cd17c75de6940473d2fa6dcee8b171c33c `
  -DEXPECTED_CORE_PROFILE_ID=render-core-default `
  -DBUILD_V10_WORKER=ON `
  -DJERRYSCRIPT_ROOT=E:/Workspaces/JellyFrame/third_party/jerryscript
cmake --build E:/Builds/runtime-f5-installed-consumer-final-full --config Release -j 8
ctest --test-dir E:/Builds/runtime-f5-installed-consumer-final-full -C Release -V --timeout 90
```

Logs for the resumed runs are in `E:/Temp`:

- `runtime-f5-worker-resume-build.log`
- `runtime-f5-worker-resume-ctest-verbose.log`
- `runtime-f5-installed-core-resume-build.log` and `runtime-f5-installed-core-resume-install.log`
- `runtime-f5-installed-consumer-resume-configure.log`, `runtime-f5-installed-consumer-resume-build.log`, and `runtime-f5-installed-consumer-resume-ctest.log`
- `runtime-f5-installed-consumer-refuse-hash.log` and `runtime-f5-installed-consumer-refuse-profile.log`
- Final source builds/installs: `runtime-f5-installed-core-final-{full,cropped}-{build,install}.log`
- Final installed consumers: `runtime-f5-installed-consumer-final-{full,cropped}-{configure,build,ctest}.log`

Core F5 merged in PR 20 at
`a0067eebde0ffe677e6e38acdd4c75da8bc78b50`, retaining source hash
`369973a9b051cdff8af56f5bd11a94cd17c75de6940473d2fa6dcee8b171c33c` / 139 files.
The automatic PR/push `canonical-v10-installed-consumers` CI job in
`.github/workflows/ci.yml` now checks out that exact commit and the same pinned
JerryScript commit, builds and installs both profiles, verifies the source
manifest/hash/count and package/profile metadata, and runs this harness with
the actual worker enabled. The CMake harness also rejects packages older than
0.9 and any ABI other than 4. The existing v9 lane and ABI3 production lock
remain unchanged. The YAML parsed without duplicate keys, all new PowerShell
steps passed syntax parsing, and the local full-profile harness rerun passed
2/2 after these CI changes (`E:/Temp/runtime-f5-ci-harness-{configure,build,ctest}.log`).

Remaining admission gates:

1. Run the newly added automatic full/cropped v10 installed consumer and worker
   CI job on the Runtime PR and require both profiles to pass.
2. Repeat the package consumer qualification against the merged package before
   changing Runtime's production dependency lock. The existing ABI3 `0.8.0`
   lock and production codec selection are unchanged by this candidate.
3. Legacy positive v5/v6/v7/v8 transport qualification needs a package exposing
   its prerequisites. The selected ABI4 package reports those capabilities
   unavailable, and the earlier ABI3 package did not expose v5 sampling.
   Their skips are not counted as positive byte qualification.
