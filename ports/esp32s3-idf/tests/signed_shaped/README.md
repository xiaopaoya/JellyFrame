# Signed full-resource shaped worker qualification

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

This standalone host suite joins the actual DeviceInstallTransaction,
DeviceImageStore, IDF mbedTLS adapter, authenticated installed lease/Binding,
JerryScript worker, v11 frame codec and installed Core painter. The NOR partition
and the provider wire are host mocks; no device, serial endpoint or production
trust configuration is changed.

The fixture preserves the complete regular16 JFFONT2 (4539 glyphs), catalog,
companion, Unicode14 tables and eligibility resource. The generator validates
source/profile identities and fixture hashes and signs using deliberately public
P-256 scalar1 only when explicitly requested. Never provision this key. The
production allowlist still rejects it; the test-only router invokes the actual
cryptographic adapter. Core validates the real resource formats and mappings.

```powershell
cmake -S ports/esp32s3-idf/tests/signed_shaped `
  -B E:/Builds/runtime-real-signed-shaped-20261117 -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_BUILD_TYPE=Release `
  -DPython3_EXECUTABLE=C:/Espressif/python_env/idf5.3_py3.11_env/Scripts/python.exe `
  -DJELLYFRAME_RENDER_CORE_SOURCE_DIR=D:/CodexTemp/Core-shaped-paint-integration-20261117 `
  -DJERRYSCRIPT_ROOT=D:/CodexTemp/jerryscript-abi4-ci-repro `
  -DJFSG_MBEDTLS_SOURCE_DIR=D:/CodexTemp/abi4-mainline-20261115/idf-v5.3.1/components/mbedtls/mbedtls
cmake --build E:/Builds/runtime-real-signed-shaped-20261117 --parallel 5
ctest --test-dir E:/Builds/runtime-real-signed-shaped-20261117 --output-on-failure
```

Python requires cryptography for explicit test signing. Assertions are enabled
for the integration executable. Declared input hashes select a fresh fixture
directory, preserving previous package bytes. Each process gets its own NOR.

The five C++ cases cover successful install/execution, reconstructed boot,
signature tampering after recomputed transport CRC, physical capacity refusal
before mutation, and aggregate shaping-resource budget refusal preserving the
prior Binding. Executable cases read HTML/CSS/JS/font through the actual lease,
publish an initial frame and three real pointer-triggered JS updates, and compare
every final packet, baseline and resulting pixel with independent Core DOM/layout
calls. No scalar text commands are admitted. A retained frame paints identically
after worker destruction, session teardown, resource/store release and NOR erasure.
The provider test reconstructs exact full-package chunks even after the admitted
file is replaced, checks JSONL bounds and refuses old capacity/missing shaping
before InstallBegin. Six additional cases exercise actual mbedTLS primitives.

The approximately 1.55 MB signed package cannot fit the former 1044480-byte
opt-in slot. `sdkconfig.ws147_shaped_full.defaults` is a separate opt-in profile
with 6 MiB storage and effective 2093056-byte slots. Its partition changes require
explicit whole-partition migration/recovery; an app-only update is insufficient.
Default partition/capacity and production keys remain unchanged. This suite does
not qualify device heap peaks, OOM/recovery, latency, physical flash/reboot or
general mixed-inline/effect/image/native-control shaping.

Evidence: [full-resource qualification](../../../../docs/evaluation/2026-11-17-signed-shaped-full-resource.md).

Allocation-recovery extension: the executable-local C++ allocation shim scans
Binding selection, worker initialization, JS text/width mutation and real pointer
input. Old accepted pixels survive candidate failure/destruction; normal worker
execution recovers afterward. Fatal-report allocation refusal/retry publishes
exactly once. The expanded suite passes17/17. It does not inject C malloc/VM arena
or target heap failures and is not a peak-memory/leak measurement. See
[allocation recovery](../../../../docs/evaluation/2026-11-17-shaped-worker-allocation-recovery.md)
for measured indices, the confirmed exception-boundary fixes and remaining gates.

## Contextual qualification

The same harness also selects `jfsp2-block-v12` explicitly when contextual
text, layout and JFSP2 are all enabled on Core ABI 9 or later. It checks that the
compiled API matches that requested route. Capture uses the owned contextual
API; packet/pixel comparison and retained-owner release checks run in both
routes. It never paints contextual geometry through scalar JFTP.

For a candidate Core, use a separate Runtime snapshot with an exact dependency
lock and matching `samples/apps/packages/jelly_font_policy/jellyframe.app.json`
minimum Core line. Do not edit production source policy or bypass the signing
tool's strict validation. The generator's `--candidate-core-version` checks
that this isolated tool lock matches; it cannot override a mismatched lock.
The exact staged changes and source hashes are recorded in the topic evidence.

For installed public Core qualification, configure this directory with:

```text
-DJELLYFRAME_RENDER_CORE_PROVIDER=package
-DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=<matching exact installed prefix>
-DJFSG_CORE_FIXTURES_DIR=<frozen Core tests/fixtures>
-DJELLYFRAME_ENABLE_CONTEXTUAL_TEXT=ON
-DJELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT=ON
-DJELLYFRAME_ENABLE_SHAPED_PACKET_V2=ON
```

Keep `JELLYFRAME_RENDER_CORE_SOURCE_DIR` unset for installed consumers. Fixture
assets are offline data, not a private header/source fallback. The standalone
parent target takes package-first include order from app_runtime; otherwise
the bundled legacy Core headers could precede imported headers. Package API,
archive linkage, source identity and assertions are audited independently.
Cropping additionally disables Canvas2D, modern paint, flex/grid, advanced
forms and length math, using the matching cropped installed prefix.

ABI9 installed full/cropped and ABI4 compatibility each pass17/17. ABI9
complete resource package size remains1550043B. The fixture evidence records the
selected Core ABI, which the provider test compares with the configured ABI
before exercising the route. It checks incompatible ABI, partial capability,
cross-route and capacity refusal before Begin. These are host mocked wire/NOR
results with actual mbedTLS verification, not physical installation or VM/heap
qualification. See [ABI9 execution evidence](../../../../docs/evaluation/2026-11-17-abi9-signed-resource-execution.md).

## Requested C++ memory and fatal VM boundary

The current suite passes20/20 per installed full/cropped and ABI4 compatibility
configuration. Three added cases check the allocation ledger,32 pointer-driven
updates with per-phase counters, and actual initialized-worker VM exhaustion
in a bounded child process. The pinned host Jerry port exits with code10 on
arena exhaustion; this is an expected fatal boundary, not worker recovery.
ESP's actual port suspends the task instead. Do not longjmp through C++ owners.

The ledger counts requested C++ new/new[] bytes and tracks frees across phase
boundaries, including aligned/nothrow/sized forms. Its allocation-free prefix
and atomic counters add overhead; counters exclude that prefix, malloc metadata,
C allocations and VM arena. Process totals include test-only NOR, package and
reference/framebuffer storage; they cannot be used as a target RAM budget.

For repeated serial host observations:

```powershell
python ports/esp32s3-idf/tests/signed_shaped/measure_resource_cost.py `
  --executable <installed-build>/jfsg_shaped_pipeline_tests.exe `
  --fixtures <exact-fixtures-directory> --output <new-evidence-directory> --runs 5
```

Every measured update still checks independent Core packets and pixels. The
report separates first/later phases and final retained replay, with bounded
same-branch retained-byte deltas. Timings include instrumentation overhead and
are not device latency or an exhaustive leak proof. See
[resource costs and VM boundary](../../../../docs/evaluation/2026-11-17-abi9-resource-cost-vm-boundary.md).
