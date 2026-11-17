# WS147 shaping-enabled compile/link qualification (2026-11-17)

Historical native-only checkpoint. Later installed-worker and mandatory format1
builds compile and link real shaped DOM/worker/package-selection entry points;
see `../../../docs/evaluation/2026-11-17-shaped-container-compatibility.md` and
`../../../docs/evaluation/2026-11-17-shaped-installed-package-selection.md`.
Its hashes and limits are separate. The native-only bytes below remain unchanged.

Status: local, uncommitted, default OFF in production. No device was flashed.
This build checks ABI4 InputsV2 and public paint against Runtime and the actual
IDF5.3.1 Xtensa toolchain. It does not qualify application-level shaped layout,
installed worker behavior, allocation peaks or a signed reboot/install path.

Runtime: `D:/CodexTemp/Runtime-shaping-package-v1-20261116` at `c0fe1cb3` plus
local edits. Core: `D:/CodexTemp/Core-shaped-paint-integration-20261117` at
`c858f171` plus local InputsV2/public-paint edits. Effective Core identity:
0.9.0 / ABI4, source `129f579c9f61706b5f3865559d05d38ded5add1b8376a13c3cd2623643ff3b03`,
180 files. Production dependency lock remains unchanged.

Configure IDF with source override, `JELLYFRAME_ENABLE_TEXT_SHAPING=ON`, an
isolated SDKCONFIG, and `sdkconfig.ws147_core_text.defaults`. Build directory:
`E:/Builds/runtime-shaping-on-idf53-20261117`. The exact cache, compile commands,
source manifest and map are retained there. Effective configuration has
CoreText, 240 MHz, C++ exceptions, and PSRAM in malloc; internal allocation
threshold16384 and reserve32768. This is a native launcher compile profile,
not the script-worker acceptance profile.

Two narrow integration fixes were necessary:

- Export `JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED` from the IDF Core
  component to all dependent translation units. The original component selected
  shaping sources without defining the matching public API macro.
- In the UI compositor call, select the ABI4 two-argument constructor while
  retaining the old three-argument constructor for ABI3.

The ordinary launcher has no shaping callers, so section GC removes the new
functions. Its successful BIN is3,532,848B, SHA-256
`6b2762d8fa56926f46fccb3820036bb34204a23486a5193076fc5405a21d5e74`;
retained as `qualification/ordinary-no-shaped-calls.bin`.

For the final link qualification, CMake EXE linker flags explicitly root the
Xtensa mangled `RuntimeShapedTextBinding::create` and `shaped_text::paint`
entry points with `-Wl,-u,<symbol>`. This makes the linker resolve the actual
Runtime-to-Core InputsV2 call and paint dependencies. `nm` confirms all three
in the final ELF. These flags live only in this qualification cache and are
not product build defaults.

Final forced-link BIN:3,557,280B (`0x3647a0`), app partition8,388,608B,
remaining4,831,328B (58%). BIN SHA-256:
`7d265bf762f42215ef7448b95655806ac6915ef98f687e01459dadb22fb00c10`.
ELF SHA-256:
`12f335081cb614a2594e1c899c7b10eef2df00b9afd05c4925dde1263a1b759a`.
The forced symbols add24,432B over the same native build with no shaped callers;
this is not a full shaped workload cost. Font catalogs, packets, runtime copies
and temporary parser storage are not measured by this image delta.

IDF static DIRAM usage71,115B; remaining270,645B is a linker accounting figure,
not free heap after boot or a product reserve. Peak memory and fragmentation
still require an executable workload. Detailed machine evidence:
`E:/Builds/runtime-shaping-on-idf53-20261117/qualification/build-evidence.json`.
Logs: `E:/Temp/runtime-shaping-on-idf53-20261117*.log`.

The original shaping-OFF bundled ABI3 build also recompiles successfully after
these edits: `D:/CodexTemp/Builds/runtime-jfsg-idf53-20261117-b`, BIN`0x327370`,
61% app space free. It checks the legacy constructor branch, not a matched
ABI4-OFF feature profile. Host matched/OFF matrices are recorded separately.

Next gates: real shaped Layout/final-fragment production and ownership, matched
worker/transport negotiation, workload memory/refusal testing, and exact-device
execution before any default or release lock change.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
