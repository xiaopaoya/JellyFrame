#pragma once

#include "app_runtime/app_host.h"
#include "device_runtime_contracts/device_bundle.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

// A lease owns the platform-specific access to one immutable, already
// validated installed bundle. It is used synchronously by the App Runtime
// supervisor only and must never cross into a script or UI task.
class AppInstalledBundleLease : public DeviceBundleReader {
public:
    ~AppInstalledBundleLease() override = default;

    virtual const DeviceBundleDescriptor& descriptor() const = 0;
    virtual void release() = 0;
};

// Device OS implements this at the registry/storage boundary. It must return
// a lease only for a committed, immutable bundle record; staging data and
// mutable registry pointers are never valid lease sources.
class AppInstalledBundleProvider {
public:
    virtual ~AppInstalledBundleProvider() = default;

    virtual DeviceBundleStatus acquire_installed_bundle(std::string_view app_id,
                                                        AppInstalledBundleLease*& lease) = 0;
};

// The protected launcher can be firmware-owned or a separately trusted
// package. It deliberately gets no third-party bundle lease.
class AppProtectedLauncher {
public:
    virtual ~AppProtectedLauncher() = default;

    virtual bool launch_protected_launcher(AppRuntimeHost& host,
                                           AppTeardownReason recovery_reason) = 0;
};

enum class AppInstalledBundleStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    AcquireFailed,
    IdentityMismatch,
    LauncherUnavailable,
    LauncherFailed,
};

struct AppInstalledBundleLaunchResult {
    AppInstalledBundleStatus status = AppInstalledBundleStatus::InvalidArgument;
    DeviceBundleStatus bundle_status = DeviceBundleStatus::InvalidArgument;
    AppInstance instance;
    AppTeardownResult previous_teardown;

    bool launched() const {
        return status == AppInstalledBundleStatus::Ok && instance.active();
    }
};

struct AppInstalledBundleRecoveryResult {
    AppInstalledBundleStatus status = AppInstalledBundleStatus::InvalidArgument;
    AppTeardownResult teardown;
    bool launcher_started = false;
};

// Serializes installed-bundle ownership with AppRuntimeHost lifecycle changes.
// Callers use this for installed-app launch, resource lookup and recovery; do
// not mix direct AppRuntimeHost termination with an active bundle lease.
class AppInstalledBundleBinding {
public:
    explicit AppInstalledBundleBinding(AppInstalledBundleProvider& provider,
                                       AppProtectedLauncher* protected_launcher = nullptr)
        : provider_(provider), protected_launcher_(protected_launcher) {}

    AppInstalledBundleBinding(const AppInstalledBundleBinding&) = delete;
    AppInstalledBundleBinding& operator=(const AppInstalledBundleBinding&) = delete;

    ~AppInstalledBundleBinding();

    AppInstalledBundleLaunchResult launch(AppRuntimeHost& host, std::string_view app_id);
    AppInstalledBundleRecoveryResult recover_to_protected_launcher(AppRuntimeHost& host,
                                                                    AppTeardownReason reason);
    AppTeardownResult terminate_current(AppRuntimeHost& host, AppTeardownReason reason);

    DeviceBundleStatus read_active_resource(std::string_view app_path,
                                            std::uint8_t* output,
                                            std::size_t output_capacity,
                                            std::size_t& output_size) const;

    bool has_active_bundle() const {
        return active_lease_ != nullptr;
    }

    bool copy_active_descriptor(DeviceBundleDescriptor& descriptor) const;

private:
    void release_active_bundle();

    AppInstalledBundleProvider& provider_;
    AppProtectedLauncher* protected_launcher_ = nullptr;
    AppInstalledBundleLease* active_lease_ = nullptr;
};

const char* app_installed_bundle_status_name(AppInstalledBundleStatus status);

} // namespace jellyframe
