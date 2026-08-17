# Installed Bundle Binding

> Last updated: 2026-08-18; Applies to: 0.6.0-dev; status: platform-neutral contract

`app_installed_bundle.*` is the narrow boundary between a Device OS committed
app library and `AppRuntimeHost`. It does not parse filesystems, duplicate the
desktop package loader, own registry publication or create a second app
runtime. It gives a port one safe way to bind a checked `.jfapp` bundle into
the normal lifecycle and resource-loading path.

## Ownership

`AppInstalledBundleProvider` is implemented by the Device OS registry/storage
owner. `acquire_installed_bundle(app_id, lease)` may return a lease only for a
committed immutable record that was accepted with
`inspect_device_bundle(...)`. It must never expose staging bytes, a mutable
registry record or a borrowed transport buffer.

`AppInstalledBundleLease` supplies bounded synchronous `read_at()` access and
a copied `DeviceBundleDescriptor`. The provider owns the physical partition,
flash mapping or cache. `release()` is called exactly once when the Runtime is
finished with that lease. The lease stays in the App Runtime supervisor task;
it must not be sent to a script worker, UI task, DMA queue or service worker.
Only copied resource bytes and value-only frame/input/service protocol values
may cross those boundaries.

A provider should return an independently releasable lease for every acquire.
The binding also tolerates a provider reusing its currently active lease for an
in-place reload: it retains that lease through the old app teardown and releases
it only when the replacement exits. A provider must not return the active lease
for a different app id.

## Launch And Recovery

Use `AppInstalledBundleBinding` as the coordinator for an installed app:

1. Acquire and verify a lease before changing the active app.
2. Check that the lease descriptor identity exactly matches the requested app
   id. A mismatch releases the new lease and leaves the current app untouched.
3. Terminate the old app through `AppRuntimeHost` with `app-switch`.
4. Release the old installed-bundle lease only after host teardown completed.
5. Launch the new `AppInstance`, retain its lease, and load resources through
   `read_active_resource(...)`.

For a load failure, runtime fatal or budget recovery, call
`recover_to_protected_launcher(host, reason)`. It terminates the active app,
releases its lease, and only then invokes `AppProtectedLauncher`. The launcher
gets no third-party lease. A false launcher result is a stable failure that the
Device OS must report and recover from; it must not silently continue using a
released bundle.

Do not mix direct `AppRuntimeHost::launch/terminate_current` calls with an
active `AppInstalledBundleBinding`. The binding is intentionally explicit so
the port has one teardown ordering and one source-release point.

## Port Responsibilities

The WS147/other port adapter must:

- keep raw-partition reads and any cache/flash lease wholly inside the Device
  OS storage owner;
- call `inspect_device_bundle` while staging verification, using target bundle,
  resource-count and summary budgets;
- store the verified `DeviceBundleDescriptor` alongside the committed record
  or reconstruct and validate it before granting a lease;
- use `DeviceAppLibraryEntry` and `DeviceRecoveryDetailPayload` for JFDP/1
  list/recovery responses, rather than serializing registry structures;
- use the binding for installed-bundle load and the protected launcher callback
  for every failure fallback;
- release or invalidate storage leases during reboot recovery only after all
  readers have left the supervisor task.

The contract has no filesystem, RTOS, panel, JerryScript, `Node*`,
`LayerNode*`, `jerry_value_t`, or port-private ABI dependency. This keeps the
future Device OS migration possible without making the generic App Runtime a
storage implementation.
