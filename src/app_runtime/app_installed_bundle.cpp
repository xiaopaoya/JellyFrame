#include "app_runtime/app_installed_bundle.h"

#include <cstring>
#include <string>

namespace jellyframe {
namespace {

bool valid_requested_app_id(std::string_view app_id) {
    return !app_id.empty() && app_id.size() <= kDeviceBundleMaxAppIdBytes &&
           std::memchr(app_id.data(), '\0', app_id.size()) == nullptr;
}

AppRole app_role_from_bundle_role(DeviceBundleAppRole role) {
    switch (role) {
    case DeviceBundleAppRole::App: return AppRole::App;
    case DeviceBundleAppRole::Launcher: return AppRole::Launcher;
    case DeviceBundleAppRole::Watchface: return AppRole::Watchface;
    case DeviceBundleAppRole::Settings: return AppRole::Settings;
    }
    return AppRole::App;
}

} // namespace

AppInstalledBundleBinding::~AppInstalledBundleBinding() {
    release_active_bundle();
}

AppInstalledBundleLaunchResult AppInstalledBundleBinding::launch(AppRuntimeHost& host, std::string_view app_id) {
    AppInstalledBundleLaunchResult result;
    if (!valid_requested_app_id(app_id)) {
        result.status = AppInstalledBundleStatus::InvalidArgument;
        return result;
    }

    AppInstalledBundleLease* acquired_lease = nullptr;
    result.bundle_status = provider_.acquire_installed_bundle(app_id, acquired_lease);
    if (result.bundle_status != DeviceBundleStatus::Ok || acquired_lease == nullptr) {
        if (acquired_lease != nullptr && acquired_lease != active_lease_) {
            acquired_lease->release();
        }
        result.status = AppInstalledBundleStatus::AcquireFailed;
        return result;
    }

    const DeviceBundleDescriptor& descriptor = acquired_lease->descriptor();
    if (descriptor.summary.app_id_view() != app_id) {
        if (acquired_lease != active_lease_) {
            acquired_lease->release();
        }
        result.status = AppInstalledBundleStatus::IdentityMismatch;
        return result;
    }

    // Tear down the old app before returning its backing storage to the
    // registry. This keeps reads valid throughout normal lifecycle cleanup.
    result.previous_teardown = host.terminate_current(AppTeardownReason::AppSwitch);
    if (acquired_lease != active_lease_) {
        release_active_bundle();
    }
    result.instance = host.launch(std::string(descriptor.summary.app_id_view()),
                                  app_role_from_bundle_role(descriptor.summary.role));
    active_lease_ = acquired_lease;
    result.status = AppInstalledBundleStatus::Ok;
    return result;
}

AppInstalledBundleRecoveryResult AppInstalledBundleBinding::recover_to_protected_launcher(
    AppRuntimeHost& host,
    AppTeardownReason reason) {
    AppInstalledBundleRecoveryResult result;
    result.teardown = terminate_current(host, reason);
    if (protected_launcher_ == nullptr) {
        result.status = AppInstalledBundleStatus::LauncherUnavailable;
        return result;
    }
    result.launcher_started = protected_launcher_->launch_protected_launcher(host, reason);
    result.status = result.launcher_started ? AppInstalledBundleStatus::Ok : AppInstalledBundleStatus::LauncherFailed;
    return result;
}

AppTeardownResult AppInstalledBundleBinding::terminate_current(AppRuntimeHost& host, AppTeardownReason reason) {
    AppTeardownResult result = host.terminate_current(reason);
    release_active_bundle();
    return result;
}

DeviceBundleStatus AppInstalledBundleBinding::read_active_resource(std::string_view app_path,
                                                                     std::uint8_t* output,
                                                                     std::size_t output_capacity,
                                                                     std::size_t& output_size) const {
    output_size = 0;
    if (active_lease_ == nullptr) {
        return DeviceBundleStatus::ResourceNotFound;
    }
    DeviceBundleResource resource;
    const DeviceBundleStatus found = find_device_bundle_resource(*active_lease_,
                                                                  active_lease_->descriptor(),
                                                                  app_path,
                                                                  resource);
    if (found != DeviceBundleStatus::Ok) {
        return found;
    }
    return read_device_bundle_resource(*active_lease_,
                                       active_lease_->descriptor(),
                                       resource,
                                       output,
                                       output_capacity,
                                       output_size);
}

bool AppInstalledBundleBinding::copy_active_descriptor(DeviceBundleDescriptor& descriptor) const {
    if (active_lease_ == nullptr) {
        descriptor = {};
        return false;
    }
    descriptor = active_lease_->descriptor();
    return true;
}

void AppInstalledBundleBinding::release_active_bundle() {
    if (active_lease_ != nullptr) {
        active_lease_->release();
        active_lease_ = nullptr;
    }
}

const char* app_installed_bundle_status_name(AppInstalledBundleStatus status) {
    switch (status) {
    case AppInstalledBundleStatus::Ok: return "ok";
    case AppInstalledBundleStatus::InvalidArgument: return "invalid-argument";
    case AppInstalledBundleStatus::AcquireFailed: return "acquire-failed";
    case AppInstalledBundleStatus::IdentityMismatch: return "identity-mismatch";
    case AppInstalledBundleStatus::LauncherUnavailable: return "launcher-unavailable";
    case AppInstalledBundleStatus::LauncherFailed: return "launcher-failed";
    }
    return "invalid-argument";
}

} // namespace jellyframe
