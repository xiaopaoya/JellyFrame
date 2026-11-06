# Core-Owned Font Package Consumer Validation (2026-11-06)

The Runtime package provider was validated against an installed Core package,
not a Core source override:

- Core `0.8.0`, engine ABI `3`, source hash
  `fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795`,
  111 source files, default feature profile.
- Runtime lock and generated provenance match all three identity fields, with
  `provider=package` and `lockEnforced=true`.
- Fresh Visual Studio 2022 x64 Release consumer configured with scripting and
  script-task runtime enabled; the installed static library is present in the
  desktop shell link inputs.
- Core Release CTest: 3/3 passed. Runtime package-consumer CTest: 82/82 passed.

The first complete Runtime build found a header-order defect: imported Core
headers were treated as system includes and the Runtime's bundled
`src/render_core` headers won lookup for colliding paths. The package provider
now keeps imported Core includes in normal lookup order, and package-consuming
targets list the package include directory before Runtime `src`. The generated
MSBuild include list and successful full Release build confirm this ordering.

`jellyframe_cli_external_trial_evidence` was excluded from the 82-test rerun.
It requires its trial build directory to be nested under a CMake build tree
inside the repository root; this verification build is intentionally isolated
under `D:\CodexTemp\Builds`. The test is unrelated to Core package linkage.
No device or firmware test was part of this host package check.

## Feature-profile consumer regression

After making Runtime consumer checks aware of the installed Core feature
profile, both package configurations were rerun in fresh Release build trees.
The default Core profile registers 83 Runtime tests; all 82 applicable tests
pass, including the two Canvas frame-script checks. The no-Canvas Core profile
registers 81 tests; all 80 applicable tests pass, while the two Canvas-specific
frame tests are not registered. A separate script test confirms Canvas context
creation is unavailable in the no-Canvas profile. In both runs,
`jellyframe_cli_external_trial_evidence` remains excluded for the repository
build-directory constraint above.

A separate Core Debug package install was built from the same ABI3 candidate,
then consumed by a fresh Runtime Debug tree. Core Debug CTest passes 3/3 and
Runtime Debug CTest passes 82/82 applicable tests. Keeping package and consumer
configurations matched avoids mixing MSVC Debug and Release runtime libraries.

The WS147 Core TextSystem acceptance configuration also builds with ESP-IDF
5.3.1 against Core topic `47597bb7` and the Runtime topic working tree based on
`3ff7551c`. The image is `0x5ab840` bytes, leaving `0x2547c0` bytes (29%) in the
8 MiB app partition. This was a compile-only check: the Runtime tree was dirty,
so its embedded source revision does not identify the complete candidate, and
the image was not flashed or physically accepted.

The generated Core system font is now gated by
`JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM`. Fresh ESP-IDF 5.3.1 builds verify
both the default-off profile and the acceptance profile: the default build
does not generate or link the Core font and produces a `0x4e4950` image with
`0x31b6b0` bytes (39%) free in the app partition; the enabled build generates
the resource and retains the `0x5ab840` image with 29% free. The profile delta
is `0xc6ef0` bytes (814,832 bytes). Neither image was flashed.
