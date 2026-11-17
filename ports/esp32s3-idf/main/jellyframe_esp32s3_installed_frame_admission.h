#pragma once

#include "app_runtime/script_task_prepared_text.h"

// Keep installed-frame selection and resource admission on the same boundary.
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM && \
    JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
#define JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED 1
#else
#define JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED 0
#endif

#include "app_runtime/script_task_contract.h"
#include "app_runtime/app_installed_bundle.h"
#include <memory>
#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
#include "jellyframe_esp32s3_installed_resource_owner.h"
#include "app_runtime/script_task_frame_renderer.h"
#endif

namespace jellyframe_esp32s3 {
inline bool installed_text_pipeline_supported(const jellyframe::AppInstalledTextPipeline& pipeline) {
    using Status = jellyframe::AppInstalledTextPipelineStatus;
    if (pipeline.status == Status::Ready) {
#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED && JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        return pipeline.frame_version == 11 && pipeline.binding && pipeline.binding->valid() &&
            jellyframe::script_task_app_frame_codec_supports_version(11);
#else
        return false;
#endif
    }
    if (pipeline.status != Status::Scalar ||
        pipeline.frame_version != 0 || pipeline.font_family_hash != 0) return false;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    if (pipeline.binding) return false;
#endif
    return true;
}

#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
inline bool installed_core_text_frame_renderable(const jellyframe::ScriptTaskAppFrame& frame) {
    for (const auto& command : frame.display_list)
        if (command.type() == jellyframe::DisplayCommandType::Text) return false;
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
#if JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED
    if (!resources || !resources->text_catalog || !jellyframe::script_task_app_frame_codec_supports_version(10)) return {};
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
    if (resources->shaped_text_binding)
        return jellyframe::script_task_app_frame_codec_supports_version(11)
            ? supervisor.begin(app_instance_id, resources->text_catalog, resources->image_provider, resources->shaped_text_binding)
            : jellyframe::ScriptAppSession{};
#endif
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
