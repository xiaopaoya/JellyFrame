#pragma once

#include "app_runtime/script_task_prepared_text.h"
#include "app_runtime/runtime_shaped_text_binding.h"

// Keep installed-frame selection and resource admission on the same boundary.
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM && \
    JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#define JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED 1
#else
#define JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED 0
#endif

#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/app_installed_bundle.h"
#include <memory>
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#include "jellyframe_esp32s3_installed_resource_owner.h"
#endif
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
#include "app_runtime/script_task_frame_renderer.h"
#endif

namespace jellyframe_esp32s3 {
inline std::uint8_t installed_text_pipeline_frame_version(const jellyframe::AppInstalledTextPipeline& pipeline) {
    using Status = jellyframe::AppInstalledTextPipelineStatus;
    if (pipeline.status == Status::Ready) {
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED && JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        return pipeline.frame_version == jellyframe::kRuntimeShapedFrameVersion &&
            pipeline.binding && pipeline.binding->valid() &&
            jellyframe::script_task_app_frame_codec_supports_version(jellyframe::kRuntimeShapedFrameVersion)
            ? jellyframe::kRuntimeShapedFrameVersion : 0;
#else
        return false;
#endif
    }
    if (pipeline.status != Status::Scalar ||
        pipeline.frame_version != 0 || pipeline.font_family_hash != 0) return false;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    if (pipeline.binding) return false;
#endif
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    constexpr std::uint8_t version = 10;
#else
    constexpr std::uint8_t version = jellyframe::kRuntimeScalarFrameVersion;
#endif
    return jellyframe::script_task_app_frame_codec_supports_version(version) ? version : 0;
#else
    return jellyframe::script_task_app_frame_codec_supports_version(5) ? 5 : 4;
#endif
}

inline bool installed_text_pipeline_supported(const jellyframe::AppInstalledTextPipeline& pipeline) {
    return installed_text_pipeline_frame_version(pipeline) != 0;
}

#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
inline bool installed_core_text_frame_renderable(const jellyframe::ScriptTaskAppFrame& frame) {
    for (const auto& command : frame.display_list)
        if (jellyframe::detail::command_type(command) == jellyframe::DisplayCommandType::Text) return false;
    return true;
}

inline jellyframe::ScriptTaskAppFrameTakeStatus take_installed_core_text_frame(
    jellyframe::ScriptTaskSupervisor& supervisor, const jellyframe::ScriptAppSession& session,
    const jellyframe::ScriptTaskAppFrameCodecOptions& options,
    jellyframe::ScriptTaskAcceptedAppFrame& output) {
    jellyframe::ScriptTaskAcceptedAppFrame candidate;
    const auto status = jellyframe::take_script_task_app_frame(supervisor, session, options, candidate);
    if (status != jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) return status;
    if (!installed_core_text_frame_renderable(candidate.frame()))
        return jellyframe::ScriptTaskAppFrameTakeStatus::ResourceRejected;
    output = std::move(candidate);
    return status;
}

// Refuse callback-dependent text before any framebuffer writes, even when an
// envelope was obtained through the generic transport rather than this port.
inline bool render_installed_core_text_frame(
    const jellyframe::ScriptTaskFrameRenderer& renderer,
    const jellyframe::ScriptTaskAcceptedAppFrame& accepted,
    jellyframe::FrameBuffer& target, jellyframe::Color background,
    const jellyframe::Rect* dirty, std::size_t dirty_count,
    jellyframe::SoftwareRasterizerScratch* scratch,
    jellyframe::ScriptTaskFrameRenderStatus* status) {
    if (!installed_core_text_frame_renderable(accepted.frame())) {
        if (status) *status = jellyframe::ScriptTaskFrameRenderStatus::InvalidFrame;
        return false;
    }
    return renderer.render_into(accepted, target, background, dirty, dirty_count, scratch, status);
}
#endif
struct InstalledAppResources;

inline jellyframe::ScriptAppSession begin_installed_script_session(
    jellyframe::ScriptTaskSupervisor& supervisor,
    std::uint32_t app_instance_id,
    const std::shared_ptr<const InstalledAppResources>& resources) {
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    if (!resources || !resources->text_catalog) return {};
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    if (resources->shaped_text_binding)
        return !resources->image_provider && resources->shaped_text_binding->valid() &&
            jellyframe::script_task_app_frame_codec_supports_version(jellyframe::kRuntimeShapedFrameVersion)
            ? supervisor.begin(app_instance_id, resources->text_catalog, resources->image_provider, resources->shaped_text_binding)
            : jellyframe::ScriptAppSession{};
#endif
    jellyframe::AppInstalledTextPipeline scalar;
    scalar.status = jellyframe::AppInstalledTextPipelineStatus::Scalar;
    if (!installed_text_pipeline_frame_version(scalar)) return {};
    return supervisor.begin(app_instance_id, resources->text_catalog, resources->image_provider);
#else
    (void)resources;
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM
    (void)supervisor;
    (void)app_instance_id;
    return {};
#else
    return supervisor.begin(app_instance_id);
#endif
#endif
}
} // namespace jellyframe_esp32s3
