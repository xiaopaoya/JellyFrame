#include "app_runtime/app_installed_bundle.h"

#include <algorithm>
#include <cstring>
#include <new>
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

DeviceBundleStatus AppInstalledBundleBinding::read_active_fonts(DeviceBundleInspectionWorkspace& workspace,
                                                                DeviceBundleFontTable& fonts) const {
    if (!active_lease_) {
        fonts.clear();
        return DeviceBundleStatus::ResourceNotFound;
    }
    return read_device_bundle_fonts(*active_lease_, active_lease_->descriptor(), workspace, fonts);
}

AppInstalledTextPipeline AppInstalledBundleBinding::select_active_text_pipeline(
    DeviceBundleInspectionWorkspace& workspace, std::size_t max_resource_bytes) const {
    AppInstalledTextPipeline result;
    if (!active_lease_) return result;
    DeviceBundleTextPipeline pipeline;
    if (read_device_bundle_text_pipeline(*active_lease_,active_lease_->descriptor(),workspace,pipeline) !=
        DeviceBundleStatus::Ok) {
        result.status = AppInstalledTextPipelineStatus::InvalidSummary;
        return result;
    }
    if (pipeline.kind == DeviceBundleTextPipelineKind::Scalar) {
        result.status = AppInstalledTextPipelineStatus::Scalar;
        return result;
    }
    std::array<std::uint8_t,16> key{};
    if (!active_lease_->copy_authenticated_signer_key_id(key) ||
        std::all_of(key.begin(),key.end(),[](auto b){return b == 0;})) {
        result.status = AppInstalledTextPipelineStatus::Unauthenticated;
        return result;
    }
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    const auto supported_kind = JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
        ? DeviceBundleTextPipelineKind::Jfsp2BlockV12 : DeviceBundleTextPipelineKind::Jfsp1BlockV11;
    if (pipeline.kind != supported_kind) {
        result.status = AppInstalledTextPipelineStatus::Unsupported;
        return result;
    }
    AppInstalledShapedTextRequest request;
    request.font_path=pipeline.paths[0].data(); request.shaping_catalog_path=pipeline.paths[1].data();
    request.shaping_companion_path=pipeline.paths[2].data(); request.normalization_tables_path=pipeline.paths[3].data();
    request.normalization_eligibility_path=pipeline.paths[4].data();
    request.source_identity=pipeline.source_identity; request.profile_identity=pipeline.profile_identity;
    request.shaping_glyph_count=pipeline.glyph_count; request.resource_id=pipeline.resource_id;
    request.raster_size=pipeline.raster_size; request.weight=pipeline.weight; request.selection=pipeline.selection;
    request.limits.core.bytes = std::min(request.limits.core.bytes, max_resource_bytes);
    request.limits.core.scratch_bytes = std::min(request.limits.core.scratch_bytes, max_resource_bytes);
    const auto bound=create_active_shaped_text_binding(request);
    if (bound.status != AppInstalledShapedTextStatus::Ready) {
        result.status=AppInstalledTextPipelineStatus::BindingRejected;
        return result;
    }
    result.binding=bound.binding;
    result.frame_version=kRuntimeShapedFrameVersion;
    result.font_family_hash=pipeline.font_family_hash;
    result.status=AppInstalledTextPipelineStatus::Ready;
#else
    (void)max_resource_bytes;
    result.status=AppInstalledTextPipelineStatus::Unsupported;
#endif
    return result;
}

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
AppInstalledShapedTextResult AppInstalledBundleBinding::create_active_shaped_text_binding(
    const AppInstalledShapedTextRequest& request) const {
    AppInstalledShapedTextResult result;
    if (active_lease_ == nullptr) {
        result.status = AppInstalledShapedTextStatus::NoActiveLease;
        return result;
    }

    const std::array<std::string_view, 5> paths = {
        request.font_path,
        request.shaping_catalog_path,
        request.shaping_companion_path,
        request.normalization_tables_path,
        request.normalization_eligibility_path,
    };
    std::array<RuntimeSha256, 5> digests{};
    for (std::size_t index = 0; index < paths.size(); ++index) {
        // An empty index applies the bundle contract's path validation without
        // reading resources before the lease authenticates their metadata.
        DeviceBundleResource unused_resource;
        if (find_device_bundle_resource(*active_lease_, DeviceBundleDescriptor{}, paths[index],
                                        unused_resource) == DeviceBundleStatus::InvalidArgument) {
            result.status = AppInstalledShapedTextStatus::InvalidArgument;
            return result;
        }
        if (!active_lease_->copy_authenticated_resource_sha256(paths[index], digests[index])) {
            result.status = AppInstalledShapedTextStatus::UnauthenticatedMetadata;
            return result;
        }
    }

    std::array<std::string, 5> bytes;
    try {
        constexpr std::size_t kCoreHardResourceBytes = 16u * 1024u * 1024u;
        const std::size_t resource_budget = std::min(request.limits.core.bytes, kCoreHardResourceBytes);
        std::size_t total_resource_bytes = 0;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            DeviceBundleResource resource;
            const DeviceBundleStatus find_status = find_device_bundle_resource(
                *active_lease_, active_lease_->descriptor(), paths[index], resource);
            if (find_status != DeviceBundleStatus::Ok || resource.payload_bytes == 0) {
                result.status = AppInstalledShapedTextStatus::ResourceReadFailed;
                result.bundle_status = find_status;
                return result;
            }
            if (total_resource_bytes > resource_budget ||
                resource.payload_bytes > resource_budget - total_resource_bytes) {
                result.status = AppInstalledShapedTextStatus::ResourceLimit;
                result.binding_status = RuntimeShapedTextStatus::ResourceLimit;
                return result;
            }
            total_resource_bytes += resource.payload_bytes;
            bytes[index].resize(resource.payload_bytes);
            std::size_t read_size = 0;
            const DeviceBundleStatus read_status = read_device_bundle_resource(
                *active_lease_, active_lease_->descriptor(), resource,
                reinterpret_cast<std::uint8_t*>(bytes[index].data()), bytes[index].size(), read_size);
            if (read_status != DeviceBundleStatus::Ok || read_size != resource.payload_bytes) {
                result.status = AppInstalledShapedTextStatus::ResourceReadFailed;
                result.bundle_status = read_status;
                return result;
            }
        }
    } catch (const std::bad_alloc&) {
        result.status = AppInstalledShapedTextStatus::ResourceLimit;
        result.binding_status = RuntimeShapedTextStatus::OutOfMemory;
        return result;
    }

    RuntimeShapedTextManifest manifest;
    manifest.font_resource = bytes[0];
    manifest.shaping_catalog = bytes[1];
    manifest.shaping_companion = bytes[2];
    manifest.normalization_tables = bytes[3];
    manifest.normalization_eligibility = bytes[4];
    manifest.font_resource_identity = digests[0];
    manifest.catalog_identity = digests[1];
    manifest.companion_identity = digests[2];
    manifest.tables_identity = digests[3];
    manifest.eligibility_identity = digests[4];
    manifest.source_identity = request.source_identity;
    manifest.profile_identity = request.profile_identity;
    manifest.shaping_glyph_count = request.shaping_glyph_count;
    manifest.resource_id = request.resource_id;
    manifest.raster_size = request.raster_size;
    manifest.weight = request.weight;
    manifest.selection = request.selection;
    result.binding_status = RuntimeShapedTextBinding::create(manifest, result.binding, request.limits);
    result.status = result.binding_status == RuntimeShapedTextStatus::Ready
        ? AppInstalledShapedTextStatus::Ready
        : AppInstalledShapedTextStatus::BindingRejected;
    return result;
}
#endif

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
