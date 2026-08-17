#include "app_runtime/app_installed_bundle.h"

#include <array>
#include <cassert>
#include <cstring>

using namespace jellyframe;

namespace {

template <std::size_t Capacity>
void copy_string(std::array<char, Capacity>& destination, const char* source) {
    const std::size_t length = std::strlen(source);
    assert(length < destination.size());
    std::memcpy(destination.data(), source, length);
}

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{4, 2, 4, 4096, 1});
}

class FakeLease final : public AppInstalledBundleLease {
public:
    FakeLease(const char* app_id, DeviceBundleAppRole role = DeviceBundleAppRole::App) {
        copy_string(descriptor_.summary.app_id, app_id);
        copy_string(descriptor_.summary.app_name, "Installed Test App");
        copy_string(descriptor_.summary.version_name, "1.0.0");
        copy_string(descriptor_.summary.entry_path, "/index.html");
        descriptor_.summary.role = role;
    }

    bool read_at(std::uint32_t, std::uint8_t*, std::size_t) const override {
        return false;
    }

    const DeviceBundleDescriptor& descriptor() const override {
        return descriptor_;
    }

    void release() override {
        ++release_count;
    }

    int release_count = 0;

private:
    DeviceBundleDescriptor descriptor_;
};

class FakeProvider final : public AppInstalledBundleProvider {
public:
    explicit FakeProvider(FakeLease& lease) : lease_(lease) {}

    DeviceBundleStatus acquire_installed_bundle(std::string_view app_id,
                                                AppInstalledBundleLease*& lease) override {
        ++acquire_count;
        last_requested_app_id = app_id;
        if (forced_status != DeviceBundleStatus::Ok) {
            lease = nullptr;
            return forced_status;
        }
        lease = &lease_;
        return DeviceBundleStatus::Ok;
    }

    FakeLease& lease_;
    DeviceBundleStatus forced_status = DeviceBundleStatus::Ok;
    int acquire_count = 0;
    std::string_view last_requested_app_id;
};

class FakeProtectedLauncher final : public AppProtectedLauncher {
public:
    explicit FakeProtectedLauncher(const FakeLease& lease) : lease_(lease) {}

    bool launch_protected_launcher(AppRuntimeHost& host, AppTeardownReason recovery_reason) override {
        ++launch_count;
        observed_release_count = lease_.release_count;
        observed_reason = recovery_reason;
        host.launch("org.jellyframe.launcher", AppRole::Launcher);
        return result;
    }

    const FakeLease& lease_;
    bool result = true;
    int launch_count = 0;
    int observed_release_count = 0;
    AppTeardownReason observed_reason = AppTeardownReason::None;
};

void installed_launch_replaces_the_old_instance_before_releasing_its_lease() {
    AppRuntimeHost host = make_host();
    const AppInstance old = host.launch("org.example.old", AppRole::App);
    FakeLease lease("org.example.installed");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);

    const AppInstalledBundleLaunchResult result = binding.launch(host, "org.example.installed");
    assert(result.launched());
    assert(result.previous_teardown.app_instance_id == old.id);
    assert(result.previous_teardown.reason == AppTeardownReason::AppSwitch);
    assert(host.current().app_id == "org.example.installed");
    assert(binding.has_active_bundle());
    assert(provider.acquire_count == 1);
    assert(provider.last_requested_app_id == "org.example.installed");
    assert(lease.release_count == 0);

    const AppTeardownResult teardown = binding.terminate_current(host, AppTeardownReason::UserKill);
    assert(teardown.app_instance_id == result.instance.id);
    assert(teardown.reason == AppTeardownReason::UserKill);
    assert(!host.current().active());
    assert(!binding.has_active_bundle());
    assert(lease.release_count == 1);
}

void source_errors_and_identity_mismatches_leave_the_current_app_untouched() {
    AppRuntimeHost host = make_host();
    const AppInstance old = host.launch("org.example.old", AppRole::App);
    FakeLease lease("org.example.actual");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);

    AppInstalledBundleLaunchResult mismatch = binding.launch(host, "org.example.requested");
    assert(mismatch.status == AppInstalledBundleStatus::IdentityMismatch);
    assert(host.current().id == old.id);
    assert(lease.release_count == 1);
    assert(!binding.has_active_bundle());

    provider.forced_status = DeviceBundleStatus::ReadFailed;
    AppInstalledBundleLaunchResult unavailable = binding.launch(host, "org.example.requested");
    assert(unavailable.status == AppInstalledBundleStatus::AcquireFailed);
    assert(unavailable.bundle_status == DeviceBundleStatus::ReadFailed);
    assert(host.current().id == old.id);
    assert(lease.release_count == 1);
}

void reloading_an_app_never_releases_a_reused_active_lease() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.reload");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    const AppInstalledBundleLaunchResult first = binding.launch(host, "org.example.reload");
    assert(first.launched());
    const AppInstalledBundleLaunchResult second = binding.launch(host, "org.example.reload");
    assert(second.launched());
    assert(second.previous_teardown.app_instance_id == first.instance.id);
    assert(second.instance.id != first.instance.id);
    assert(lease.release_count == 0);

    binding.terminate_current(host, AppTeardownReason::NormalExit);
    assert(lease.release_count == 1);
}

void recovery_releases_the_bundle_after_host_teardown_and_before_launcher_start() {
    AppRuntimeHost host = make_host();
    FakeLease lease("org.example.recover");
    FakeProvider provider(lease);
    FakeProtectedLauncher launcher(lease);
    AppInstalledBundleBinding binding(provider, &launcher);
    const AppInstalledBundleLaunchResult launched = binding.launch(host, "org.example.recover");
    assert(launched.launched());

    const AppInstalledBundleRecoveryResult recovered =
        binding.recover_to_protected_launcher(host, AppTeardownReason::RuntimeError);
    assert(recovered.status == AppInstalledBundleStatus::Ok);
    assert(recovered.launcher_started);
    assert(recovered.teardown.app_instance_id == launched.instance.id);
    assert(recovered.teardown.reason == AppTeardownReason::RuntimeError);
    assert(lease.release_count == 1);
    assert(launcher.observed_release_count == 1);
    assert(launcher.observed_reason == AppTeardownReason::RuntimeError);
    assert(host.current().role == AppRole::Launcher);
    assert(host.current().app_id == "org.jellyframe.launcher");
}

void active_resource_queries_require_an_active_lease() {
    FakeLease lease("org.example.resources");
    FakeProvider provider(lease);
    AppInstalledBundleBinding binding(provider);
    std::array<std::uint8_t, 8> output{};
    std::size_t output_size = 9;
    assert(binding.read_active_resource("/index.html", output.data(), output.size(), output_size) ==
           DeviceBundleStatus::ResourceNotFound);
    assert(output_size == 0);
}

} // namespace

int main() {
    installed_launch_replaces_the_old_instance_before_releasing_its_lease();
    source_errors_and_identity_mismatches_leave_the_current_app_untouched();
    reloading_an_app_never_releases_a_reused_active_lease();
    recovery_releases_the_bundle_after_host_teardown_and_before_launcher_start();
    active_resource_queries_require_an_active_lease();
    return 0;
}
