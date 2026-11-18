#include "jellyframe_esp32s3_installed_frame_admission.h"
#include "app_runtime/script_task_sampling.h"
#include <iostream>

int main() {
    constexpr bool expected_text = JELLYFRAME_TEST_CORE_TEXT &&
        JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API;
    static_assert(JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED == expected_text,
                  "installed frame resource admission must match feature selection");
    jellyframe::ScriptTaskSupervisor supervisor;
    jellyframe::AppInstalledTextPipeline pipeline;
    using Status = jellyframe::AppInstalledTextPipelineStatus;
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
    pipeline.status = Status::Scalar;
    const bool scalar_supported = !JELLYFRAME_TEST_CORE_TEXT ||
        (JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API && jellyframe::script_task_app_frame_codec_supports_version(
            JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API ? 10 : jellyframe::kRuntimeScalarFrameVersion));
#if JELLYFRAME_TEST_CORE_TEXT && JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    if (!jellyframe::script_task_app_frame_codec_supports_version(jellyframe::kRuntimeScalarFrameVersion)) return 1;
    if (jellyframe_esp32s3::installed_text_pipeline_frame_version(pipeline) != jellyframe::kRuntimeScalarFrameVersion) return 1;
#endif
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline) != scalar_supported) return 1;
#if !JELLYFRAME_TEST_CORE_TEXT
    const auto legacy_version = jellyframe_esp32s3::installed_text_pipeline_frame_version(pipeline);
    if (legacy_version != (jellyframe::detail::kScriptTaskSamplingTransportAvailable ? 5 : 4) ||
        !jellyframe::script_task_app_frame_codec_supports_version(legacy_version)) return 1;
    jellyframe::ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 32, 32};
    jellyframe::ScriptTaskAppFrameCodecOptions options{4, 64, 4, 1024};
    options.version = legacy_version;
    std::vector<std::uint8_t> encoded;
    if (jellyframe::encode_script_task_app_frame(frame, options, encoded) !=
            jellyframe::ScriptTaskAppFrameCodecStatus::Accepted ||
        jellyframe::decode_script_task_app_frame(encoded, options, frame) !=
            jellyframe::ScriptTaskAppFrameCodecStatus::Accepted) return 1;
#endif
    for (auto status : {Status::Ready, Status::InvalidSummary, Status::Unauthenticated,
                        Status::Unsupported, Status::BindingRejected}) {
        pipeline.status = status;
        if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
    }
    pipeline.status = Status::Scalar;
    pipeline.frame_version = 11;
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
    const auto session = jellyframe_esp32s3::begin_installed_script_session(supervisor, 1, {});
    if (session.valid() == bool(JELLYFRAME_TEST_CORE_TEXT)) {
        std::cerr << "FAIL only the legacy route may begin without resources\n";
        return 1;
    }
    return 0;
}
