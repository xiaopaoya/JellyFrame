#include <iostream>
#include <cstdlib>
#include <string_view>

int script_task_contract_tests_main();
int script_task_frame_codec_tests_main();
int script_task_frame_renderer_tests_main();
int script_task_input_codec_tests_main();
int script_task_input_dispatch_tests_main();
int script_task_service_request_codec_tests_main();
int script_task_service_bridge_tests_main();
int script_task_worker_inbox_tests_main();
int script_task_value_flow_tests_main();
int script_task_fatal_codec_tests_main();

namespace {

using TestMain = int (*)();

bool g_selected_test_group = false;

int run_test_group(const char* name, TestMain test_main) {
    const char* only = std::getenv("JELLYFRAME_TEST_ONLY");
    if (only != nullptr && *only != '\0' && std::string_view(only) != name) {
        return 0;
    }
    g_selected_test_group = true;
    if (const char* trace = std::getenv("JELLYFRAME_TEST_TRACE"); trace != nullptr && *trace != '\0') {
        std::cerr << "[script-task-tests] " << name << '\n';
    }
    return test_main();
}

} // namespace

int main() {
    int failed = 0;
    failed += run_test_group("contract", script_task_contract_tests_main);
    failed += run_test_group("frame-codec", script_task_frame_codec_tests_main);
    failed += run_test_group("frame-renderer", script_task_frame_renderer_tests_main);
    failed += run_test_group("input-codec", script_task_input_codec_tests_main);
    failed += run_test_group("input-dispatch", script_task_input_dispatch_tests_main);
    failed += run_test_group("service-request-codec", script_task_service_request_codec_tests_main);
    failed += run_test_group("service-bridge", script_task_service_bridge_tests_main);
    failed += run_test_group("worker-inbox", script_task_worker_inbox_tests_main);
    failed += run_test_group("value-flow", script_task_value_flow_tests_main);
    failed += run_test_group("fatal-codec", script_task_fatal_codec_tests_main);
    if (const char* only = std::getenv("JELLYFRAME_TEST_ONLY");
        only != nullptr && *only != '\0' && !g_selected_test_group) {
        std::cerr << "unknown JELLYFRAME_TEST_ONLY group: " << only << '\n';
        ++failed;
    }
    if (failed == 0) {
        std::cout << "script task runtime tests passed\n";
    }
    return failed == 0 ? 0 : 1;
}
