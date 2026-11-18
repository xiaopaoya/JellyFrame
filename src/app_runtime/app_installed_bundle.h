#pragma once

#include "app_runtime/app_host.h"
#include "device_runtime_contracts/device_bundle.h"
#include "app_runtime/runtime_shaped_text_binding.h"

#include <cstddef>
#include <cstdint>
#include <array>
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

    // Return a digest only when this exact immutable lease owns resource
    // metadata from a successfully authenticated signed-package commit.
    // Implementations must not use JFAPP CRCs, staged metadata, or a host
    // candidate's signature status. Legacy and unsigned leases fail closed.
    virtual bool copy_authenticated_resource_sha256(
        std::string_view app_path,
        std::array<std::uint8_t, 32>& sha256) const {
        (void)app_path;
        sha256.fill(0);
        return false;
    }

    virtual bool copy_authenticated_signer_key_id(
        std::array<std::uint8_t, 16>& key_id) const {
        key_id.fill(0);
        return false;
    }
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

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
struct AppInstalledShapedTextRequest {
    std::string_view font_path;
    std::string_view shaping_catalog_path;
    std::string_view shaping_companion_path;
    std::string_view normalization_tables_path;
    std::string_view normalization_eligibility_path;
    RuntimeSha256 source_identity{};
    RuntimeSha256 profile_identity{};
    std::uint32_t shaping_glyph_count = 0;
    std::uint64_t resource_id = 0;
    std::uint16_t raster_size = 0;
    std::uint16_t weight = 0;
    std::uint16_t selection = 0;
    RuntimeShapedTextLimits limits{};
};

enum class AppInstalledShapedTextStatus : std::uint8_t {
    Ready,
    NoActiveLease,
    InvalidArgument,
    UnauthenticatedMetadata,
    ResourceLimit,
    ResourceBindingSchemaIncomplete,
    ResourceReadFailed,
    BindingRejected,
};

struct AppInstalledShapedTextResult {
    AppInstalledShapedTextStatus status = AppInstalledShapedTextStatus::NoActiveLease;
    DeviceBundleStatus bundle_status = DeviceBundleStatus::Ok;
    RuntimeShapedTextStatus binding_status = RuntimeShapedTextStatus::InvalidRequest;
    RuntimeShapedTextBinding::Handle binding;
};
#endif

enum class AppInstalledTextPipelineStatus : std::uint8_t {
    Scalar, Ready, NoActiveLease, InvalidSummary, Unauthenticated, Unsupported, BindingRejected
};
struct AppInstalledTextPipeline {
    AppInstalledTextPipelineStatus status = AppInstalledTextPipelineStatus::NoActiveLease;
    std::uint8_t frame_version = 0;
    std::uint32_t font_family_hash = 0;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    RuntimeShapedTextBinding::Handle binding;
#endif
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
    // One package-selected text entry point. Missing declaration means scalar;
    // an explicit unavailable/malformed shaped profile never downgrades.
    // Resource/scratch caps bound admission, not peak allocations or heap reserve.
    AppInstalledTextPipeline select_active_text_pipeline(DeviceBundleInspectionWorkspace& workspace,
        std::size_t max_resource_bytes = 16u * 1024u * 1024u) const;

    DeviceBundleStatus read_active_fonts(DeviceBundleInspectionWorkspace& workspace,
                                         DeviceBundleFontTable& fonts) const;

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    // Resource bytes and digests are read from the same active immutable
    // lease. Caller-supplied identities are checked by Core against those
    // signed resources; caller-supplied digests are never accepted.
    AppInstalledShapedTextResult create_active_shaped_text_binding(
        const AppInstalledShapedTextRequest& request) const;
#endif

private:
    void release_active_bundle();

    AppInstalledBundleProvider& provider_;
    AppProtectedLauncher* protected_launcher_ = nullptr;
    AppInstalledBundleLease* active_lease_ = nullptr;
};

const char* app_installed_bundle_status_name(AppInstalledBundleStatus status);

} // namespace jellyframe
