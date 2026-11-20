# Actual-crypto signed installation qualification

This standalone host suite compiles the unmodified WS147 DeviceImageStore,
DeviceInstallTransaction, JFAPP reader and JFSG parser. The signature adapter
uses software primitives from the exact local IDF5.3.1 mbedTLS tree. The only
store-object substitution is its signature callback symbol, redirected to a
test router invoking the real adapter's test-key entry point. No parser or
verification result is mocked. The production adapter is separately compiled
unchanged; its empty allowlist is also exercised through the store.

The partition stub models bounded NOR flash: erase uses 4096-byte sectors,
writes permit only1-to0 transitions, and every read/write checks partition
bounds. Bytes survive reconstruction of DeviceImageStore within the process.
It is memory-backed, not actual ESP flash, not process/power-loss persistence,
and not an RTOS/heap/latency qualification. Heap-cap allocation can be refused;
it does not simulate every C++ allocation or measure fragmentation.

`generate_fixtures.py` calls the actual packager and `tools/sign_app_package.py`
to create complete scalar format0 and shaped format1 JFAPPs inside JFSG envelopes.
ECDSA scalar1 is a deliberately public test key, with the public id/coordinates
from the existing crypto vectors. Never provision it in a product. The format1
fixture's assets contain placeholder bytes: this qualifies installation and
authenticated metadata delivery, not Core shaping or execution of those assets.
Signatures are randomized; the fixture manifest records the exact generated
bytes. Rebuilding fixtures produces new legitimate signatures and identities.

Configure and run after loading the development environment:

```powershell
cmake -S ports/esp32s3-idf/tests/signed_install `
  -B E:/Builds/runtime-jfsg-installed-host-20261117 -G Ninja `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DCMAKE_BUILD_TYPE=Release `
  -DPython3_EXECUTABLE=C:/Espressif/python_env/idf5.3_py3.11_env/Scripts/python.exe `
  -DJFSG_CORE_INCLUDE_DIR=D:/CodexTemp/Core-shaped-paint-integration-20261117/include `
  -DJFSG_MBEDTLS_SOURCE_DIR=D:/CodexTemp/abi4-mainline-20261115/idf-v5.3.1/components/mbedtls/mbedtls
cmake --build E:/Builds/runtime-jfsg-installed-host-20261117
ctest --test-dir E:/Builds/runtime-jfsg-installed-host-20261117 -V
```

Requires Python cryptography, a native C/C++ compiler, and llvm-nm on PATH.
No device, serial port, production key or release secret is used.

## Configured public trust through the store

`jfsg_install_configured_public_trust` compiles the store and signature callback
with the ESP32-S3 build's public-trust CMake helper. Configure generates two
ephemeral signing keys in memory and writes only a public manifest and signed
fixture envelopes to the build directory. The test installs v1, reconstructs
the store and checks the authenticated signer, resource digest and bundle
bytes. It verifies a v2 update signed by the configured key and then rejects
the same bundle signed by the other key with the same key ID. The active v1
and its registry generation survive the refusal and a second reconstruction.
The configured symbol test checks both store and verifier objects for test
injection symbols. Run both cases with
`ctest -R "^jfsg_(install_configured_public_trust|configured_store_symbol_boundary)$" -V`.

Initial qualification 2026-11-17:18/18 CTest cases pass. The later explicit
capacity extension passes22/22; see
[capacity evidence](../../../../docs/evaluation/2026-11-17-signed-install-capacity.md).
The initial counts below describe the smaller-fixture run only. Ten installation scenarios run
838 explicit checks and65 actual signature callback calls. The included six
crypto cases run289 checks; signing-tool CTest runs five Python tests. The
remaining CTest checks the production object has its callback but neither test
router symbol. Assertions are independent of NDEBUG in the host harness.

Covered installation behaviors:

- Positive scalar then shaped commits and exact inner-container lease bytes.
- DeviceInstallTransaction chunking, incomplete commit refusal, final commit.
- Store reconstruction verifies signatures again before accepting registry state.
- Lease acquisition verifies signatures and exposes exact resource SHA/key id;
  a missing path clears its output digest.
- Signature mutation with recomputed transport CRC is rejected by the actual
  verifier; prior active content remains readable.
- Published envelope corruption falls back to the older valid registry copy.
- Signature mutation plus recomputed envelope and registry CRCs still fails
  authentication at boot and falls back. This mutation uses host ABI registry
  records from this executable; it does not establish target record layout.
- Verified but uncommitted staging is ignored on reconstruction.
- Explicit rollback, wrong public key and envelope-snapshot allocation refusal.
- Empty production trust refuses the valid signed test package.

Logs: `E:/Temp/runtime-jfsg-installed-host-{configure,build-final3,ctest-final3}-20261117.log`.
Build/fixtures: `E:/Builds/runtime-jfsg-installed-host-20261117`.

Still open: actual-device signed transport/install/reboot/power-loss/tamper,
larger package/storage limits, reviewed key provisioning, shaped workload
execution, peak memory, all-allocation exhaustion and latency/product reserve.
Default WS147 provider/store limits remain320KiB; an explicit larger store
profile is host-tested/IDF-built but its provider transport is not adapted yet; these small fixtures do not
qualify the larger real shaping catalog. Anti-rollback remains relative to the
recovered active version, not a monotonic trusted counter.

Last updated: 2026-11-20; Applies to: 0.6.0-dev.
