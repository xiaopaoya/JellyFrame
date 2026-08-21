# Developer Image Lifecycle Acceptance

> Last updated: 2026-08-21; Applies to: 0.6.0-dev; Protocol: JFDP/1

This is the A1-2 acceptance gate for the first official Developer Image. It follows, but does not replace, [JFDP/1 Physical Transport Acceptance](jfdp_v1_port_acceptance.md). It proves persistent staged installation, registry publication and launcher recovery on one concrete board image. It is not a general filesystem test, firmware-update protocol, marketplace, remote-download service or performance benchmark.

## Gate and scope

### Accepted WS147 baseline

The first published WS147 Developer Image passed this gate on 2026-08-19 at
source revision `fbf10784ac8ce38f41ced40fa013a43564c992c8`. Its manifest is
`org.jellyframe.ws147.developer@0.1.0-dev`; the factory image write was hash
verified, boot selected protected launcher with `RegistryInvalid`, and the
`org.jellyframe.device.lifecycle@1.0.0` fixture was installed and launched
through JFDP/1 without reflashing. The published report records 9 transmitted,
9 received and 0 timed-out frames, with zero panic, watchdog, reset-loop, DMA,
SPI, panel and present errors. This confirms the A1-2 lifecycle and the image
manifest/factory-recovery sub-item; it is not A2 installed-App rendering,
input or author-tool evidence.

Before this gate starts, the exact image/profile must have a passing JFDP/1 wire report. The WS147 native USB Serial/JTAG transport passed that wire-only gate on 2026-08-18 using fixture SHA-256 345d2c6bafadfdfab86af216b428c437fd34e0b9b3adfd16687662da494ef3bb. That evidence does not prove this lifecycle gate, and a later image change requires a compatibility assessment or a new wire report.

The port must implement DeviceInstallStore from device_runtime_contracts/device_install_transaction.h rather than duplicate the transaction state machine:

- begin_staging, write_staging and verify_staging never publish an app.
- commit_staging returns true only after the new registry entry is durably and atomically published. A false return leaves the previous registry unchanged.
- abort_staging(transaction_id) is idempotent and scoped to that transaction, including a partially-created staging object after a failed begin.
- DeviceInstallChunkView.bytes is copied before asynchronous storage work or a cross-task handoff.

`DeviceInstallStore::verify_staging()` must inspect staged bytes through
`DeviceBundleReader` and `inspect_device_bundle(...)`, with the board profile's
explicit bundle/resource/summary budgets. It must persist or revalidate the
accepted descriptor before publication. AppList and Recovery responses use the
typed `DeviceAppListPayload` and `DeviceRecoveryDetailPayload` codecs. Installed
app start and failure fallback use `AppInstalledBundleBinding`, not a
fixed-fixture loader or a port-private registry-to-HTML shortcut.

The device overload of `inspect_device_bundle(...)` takes a
`DeviceBundleInspectionWorkspace`. A port must keep that 4 KiB maximum-summary
workspace in the storage owner or another explicitly budgeted long-lived
object, never on the JFDP, UI, or script-task call stack. Sector caches are
subject to the same rule. The desktop convenience overload is not acceptable
evidence for a board profile.

Third-party bundles must live outside immutable firmware, launcher and fallback assets. JFDP accepts only documented bounded operations; it must not become raw flash, arbitrary file or native-command access.

## Required durable model

The report must state the partition/filesystem scheme, record format, bundle and staging limits, and boot-time recovery algorithm. A journal, dual registry slots or generation records are all acceptable if the following properties hold:

1. A committed app record names one complete, verified bundle and version/identity. No registry entry references incomplete staging bytes.
2. A replacement preserves a launchable previously committed version until the replacement is atomically published. allow_downgrade=false rejects a lower version without changing that record.
3. Interrupted or invalid staging is invisible after boot and reclaimed by a bounded recovery path. It cannot block a later install indefinitely.
4. Registry decoding is bounded and corruption-safe. Invalid, torn or checksum-failed metadata selects protected launcher/fallback rather than an arbitrary bundle; the report names the recovery diagnostic.
5. Launcher/fallback is non-removable through app-library operations. A failed app load, runtime fatal or app budget recovery returns there without reflashing or a whole-MCU reset.
6. Remove and rollback are durable lifecycle operations. Remove cannot erase protected assets; rollback selects a retained verified version or returns documented not-found/unsupported without corrupting the active record.

Cleanup of old staging after a successful commit is not publication. Losing power after publication but before cleanup must still boot the committed registry.

## Fixture and interruption rules

Use at least two checked .jfapp bundles with the same app id and different visible version strings, plus one malformed or integrity-failing bundle. Each fixture must expose a deterministic launch marker in its app-scoped log or launcher state; panel photography is not required.

The port must provide a repeatable interruption hook: a test-only controlled reset, storage-adapter failpoint followed by reboot, or recorded physical power interruption. A simple host disconnect is not sufficient for durable-write cases. The hook must not mutate registry state outside normal storage code or repair it manually after reboot.

Every reboot case starts with a fresh discovery and ends by reading installed-app state and launching the expected app or protected launcher. Record observed registry generation/version and recovery reason where available.

## Mandatory matrix

| Case | Procedure | Pass condition |
| --- | --- | --- |
| Baseline install | Install fixture A, commit, reboot, list and launch it. | A is published, its marker appears, and no staging record is visible. |
| Update and rollback | Install B for A's app id, commit, reboot, launch B, then rollback and reboot. | B replaces A only after commit; rollback restores A or a documented retained version. |
| Default downgrade | With B published, submit lower-version A with allow_downgrade=false. | Rejected; B remains listed and launchable after reboot. |
| Explicit downgrade | Repeat with allow_downgrade=true only if profile declares it. | Documented target becomes active only after durable commit. |
| Begin interruption | Interrupt after staging creation and reboot. | No new version is published; previous app/launcher launches; stale staging is reclaimed. |
| Mid-write interruption | Interrupt during a chunk write, including a near-final chunk, then reboot. | No partial bundle is listed or launched; prior committed state survives. |
| Verify interruption/failure | Interrupt verification, then use invalid CRC/bundle. | No publication; retry or abort returns to a usable idle state. |
| Commit interruption | Interrupt before registry publication and after durable publication but before optional cleanup. | Before: old state survives. After: exactly one complete new state survives. No torn registry is visible. |
| Commit/storage failure | Make commit_staging fail after adapter-private preparation. | Stable failure; no new entry; a later install succeeds. |
| Disconnect and abort | Disconnect mid-transfer, reconnect and begin a new transaction; send explicit abort twice. | Partial staging is not published; repeated abort is harmless; a new transaction completes. |
| Remove | Remove foreground and non-foreground fixtures where supported, then reboot. | Removed app cannot launch; launcher survives; retained rollback data follows policy. |
| Bad-app recovery | Select a validation/load-failing package; trigger one controlled app fatal if available. | Returns to launcher/fallback without watchdog, reset loop or registry damage. |
| Registry corruption | Inject torn/corrupt registry metadata through the test storage path, then reboot. | Parser stays bounded; launcher/fallback starts; no arbitrary bundle runs; diagnostic is recorded. |
| Repetition | Run install -> update -> rollback -> remove plus disconnect/reconnect for at least 30 cycles. | No reset, watchdog, publication mismatch, unbounded staging growth or irrecoverable install failure. |

Unsupported lifecycle operations may be rejected, but never silently reported as success. Each rejection needs a stable JFDP/1 result code and unchanged-state assertion.

## Required evidence and decision

A versioned report directory must include Runtime and Device OS commit; image/profile and storage configuration; JFDP fixture SHA-256; exact uncommitted port changes if any; image and fixture hashes; build/flash logs; raw host capture; machine-readable summary; each interruption point and post-boot observation.

Include counters for staging begin/write/verify/commit/abort, registry publication, recovery/fallback entries, rejected requests, reconnects, resets and watchdogs. Report memory/queue watermarks where available, otherwise state the limitation and structural bounds. This gate may establish launch/fallback through typed binding and resource-read results; actual installed-App DOM/panel rendering, visual comparison and input are A2 end-to-end evidence, not a substitute claim for this storage lifecycle gate.

For every real-resource verification timeout or latency regression, archive
phase telemetry for: transport CRC, JFAPP header/bundle CRC, summary parse,
resource validation, registry publish and response write. Also archive the
executing task's configured stack, minimum stack watermark, workspace/cache
placement and reader call/byte counts. A provider timeout is not an acceptable
final result: the port must identify a bounded phase and return a typed failure
or successful commit within its documented provider timeout.

Separate verdicts for wire acceptance, storage lifecycle, launcher recovery and tooling. Passing a reference dispatcher or a disconnect-only test is not evidence for persistent interruption safety.

A1-2 passes only when all applicable cases are deterministic, every failed or interrupted publication preserves the previous committed state, and every recovery returns to protected launcher/fallback without watchdog, MCU reset loop or reflashing. The WS147 board/profile manifest and factory recovery procedure now pass separately; A1 remains open until the A2 author-tool workflow passes.
