#include "jellyframe_esp32s3_installed_frame_admission.h"
#include <iostream>

int main() {
    constexpr bool expected_frame10 = JELLYFRAME_TEST_CORE_TEXT &&
        JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API;
    static_assert(JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED == expected_frame10,
                  "installed frame resource admission must match feature selection");
    jellyframe::ScriptTaskSupervisor supervisor;
    jellyframe::AppInstalledTextPipeline pipeline;
    using Status = jellyframe::AppInstalledTextPipelineStatus;
    if (jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
    pipeline.status = Status::Scalar;
    if (!jellyframe_esp32s3::installed_text_pipeline_supported(pipeline)) return 1;
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
