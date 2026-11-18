# Explicit signed-install storage capacity

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

Local/unpublished. This closes a bounded store-capacity and target compile
slice; it does not establish signed physical transport or shaped execution.
No device was enumerated/flashed. Production default partition, dependency
lock and trust allowlist remain unchanged.

## Admission and configuration

`CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB` is an integer setting
with default 320 and range 64–2048. It bounds the complete incoming envelope,
not just the inner JFAPP. `DeviceImageStore::max_bundle_bytes()` returns the
smaller of that cap and the physical sector-aligned slot size. Runtime JFDP
capability responses and the install transaction now use this effective limit.
The header explicitly includes SDK configuration on ESP targets, so every
translation unit sees the same setting.

Three independent physical slots continue to protect active, rollback and
staging generations. A package larger than one slot is rejected before any
erase/write or registry mutation; the former code eventually refused its flash
operation but could reach the operation before establishing slot capacity.
The staging write boundary is now the declared transaction length, including
overflow-safe offset/size checks, rather than only the global package cap.
Boot envelope admission also checks the effective physical/configuration cap.
A partition that cannot hold even one sector per slot fails initialization
before publishing a registry.

The opt-in `sdkconfig.ws147_shaped_install_capacity.defaults` selects a 1024KiB
cap and `partitions_16mb_n16r8_shaped_install.csv`. Apply it after the ordinary
lifecycle/CoreText/worker defaults using a fresh SDKCONFIG. It is a capacity
overlay, not a complete standalone application build profile.

| Region | Existing default | Explicit capacity layout |
| --- | --- | --- |
| factory application | 8MiB | 8MiB |
| assets | 6MiB | 4MiB |
| installed storage | 1MiB | 3MiB |
| registry | two 4KiB sectors | two 4KiB sectors |
| physical slot | 344064 bytes | 1044480 bytes |
| effective envelope cap | 327680 bytes | 1044480 bytes |

The new storage offset is `0xc10000`; the existing offset is `0xe10000`.
Assets/storage boundaries move. This layout must be deployed as an explicit
partition migration with required data preserved externally and the new
regions initialized. It cannot be treated as an app-only in-place upgrade.
No such deployment was performed. The newly reduced assets area also needs
workload qualification before adoption.

The real regular catalog, companion, Unicode tables and eligibility total
825771 bytes before font/app/container/envelope. The new slot admits that
order of workload; exact executable resource packages remain to be qualified.
Only raising a software limit under the existing partition would not suffice.

## Host evidence

Fresh build: `E:/Builds/runtime-jfsg-capacity-host-20261117`.
CTest passes **22/22**: the previous 18 signed-store/crypto/tool/symbol cases
plus default-capacity refusal, too-small-partition initialization refusal,
opt-in large-capacity lifecycle, and opt-in cap paired with old physical
capacity. Both store variants compile the actual production store source;
only the existing test signature router is substituted.

The actual packager and signer produce `v3.jfsg` with a 900KiB deterministic
resource. Its shaping asset declarations still use placeholders. This checks
large envelope installation, not that those assets are executable shaping
resources. The default cap refuses it; the 3MiB/1024KiB build installs it,
reconstructs the store, re-verifies authenticated lease bytes and rolls back
to the older valid app. The 1MiB physical-partition case refuses it even when
the build cap is 1024KiB.

Boundary checks exercise effective-cap+1 refusal in both store and transaction,
unchanged flash/registry on refusal, exact-cap staging and its last writable
byte, writes beyond a shorter declared transaction, UINT32_MAX offsets, and
old-lease preservation. The successful large-capacity scenario records 9627
explicit checks and 14 actual signature verifier calls. Test keys remain
public fixture material and are absent from production trust.

Final logs: `E:/Temp/jfsg-capacity-{build,ctest}-final3-20261117.log`.
Frozen sources/fixtures/logs and their hash manifest:
`E:/Experiments/migration-signed-capacity-host-20261117`.

## ESP-IDF evidence

Fresh IDF5.3.1/Xtensa build:
`E:/Builds/runtime-shaped-capacity-idf53-20261117`.
Evidence: `E:/Experiments/migration-shaped-capacity-idf53-20261117`.
Actual store compilation preprocessor output confirms the 1024KiB setting;
the decoded binary partition table confirms storage at `0xc10000`, size3MiB.
CoreText, shaping, lifecycle and script worker are enabled, with 240MHz, RTTI,
exceptions, PSRAM malloc and console NONE. No forced linker roots are used.

The final BIN is 4515056 bytes, SHA-256:
`79fcab5ae75c1869b2264a25379f5eb0373af9e120317ecbe40f753204cf4718`.
ELF SHA-256:
`a01f378deaa6952c822e08200b034c5d242ddd2e2c1abc223102017e6365db9c`.
The 8MiB app partition has 46% free. Compile/link and remaining flash space
do not prove runtime heap reserve, performance or successful initialization on
a real board. Result JSON, exact SDKCONFIG, source inputs, commands, partition
bytes and BIN are retained. Historical earlier build evidence is unchanged.

## Next boundaries

The Python WS147 provider still accepts scalar `.jfapp` only and its release
manifest builder still emits 327680 bytes. It must explicitly transport signed
JFSG, read the inner identity without claiming trust verification, and bind
the manifest's effective capacity to the selected firmware/partition evidence.
Do not package this larger firmware with that old hardcoded provider manifest.
Actual five-resource signed packages, physical install/reboot/tamper/fallback,
peak memory/allocation refusal and worker latency remain open. ABI9 identity
and contextual Layout/paint integration are separate unfinished work.
