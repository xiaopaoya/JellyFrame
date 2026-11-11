#include "jellyframe_esp32s3_installed_frame_admission.h"
#include <iostream>

int main() {
    constexpr bool expected_frame10 = JELLYFRAME_TEST_CORE_TEXT && JELLYFRAME_TEST_BMP &&
        JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API;
    static_assert(JELLYFRAME_ESP32S3_INSTALLED_FRAME10_ENABLED == expected_frame10,
                  "installed frame resource admission must match feature selection");
    jellyframe::ScriptTaskSupervisor supervisor;
    const auto session = jellyframe_esp32s3::begin_installed_script_session(supervisor, 1, {});
    if (session.valid() == expected_frame10) {
        std::cerr << "FAIL resource-free v5 must begin, resource-free v10 must refuse\n";
        return 1;
    }
    return 0;
}
