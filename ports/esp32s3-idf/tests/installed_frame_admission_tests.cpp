#include "jellyframe_esp32s3_installed_frame_admission.h"
#include <iostream>

int main() {
    constexpr bool expected_text = JELLYFRAME_TEST_CORE_TEXT &&
        (JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API ||
         (JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API));
    static_assert(JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED == expected_text,
                  "installed frame resource admission must match feature selection");
    jellyframe::ScriptTaskSupervisor supervisor;
    jellyframe::AppInstalledTextPipeline pipeline;
    using Status = jellyframe::AppInstalledTextPipelineStatus;
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
    pipeline.status = Status::Scalar;
    const bool scalar_supported = !JELLYFRAME_TEST_CORE_TEXT ||
        (JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API && jellyframe::script_task_app_frame_codec_supports_version(10));
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline) != scalar_supported) return 1;
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
        std::cerr << "FAIL resource-free v5 must begin, resource-free v10 must refuse\n";
        return 1;
    }
    return 0;
}
