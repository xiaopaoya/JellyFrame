#include <iostream>
#include <cstdlib>
#include <string>
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

std::string environment_value(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string copied(value);
    std::free(value);
    return copied;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

int run_test_group(const char* name, TestMain test_main, std::string_view only, bool trace) {
    if (!only.empty() && only != name) {
        return 0;
    }
    g_selected_test_group = true;
    if (trace) {
        std::cerr << "[script-task-tests] " << name << '\n';
    }
    return test_main();
}

} // namespace

int main() {
    const std::string only = environment_value("JELLYFRAME_TEST_ONLY");
    const bool trace = !environment_value("JELLYFRAME_TEST_TRACE").empty();
    int failed = 0;
    failed += run_test_group("contract", script_task_contract_tests_main, only, trace);
    failed += run_test_group("frame-codec", script_task_frame_codec_tests_main, only, trace);
    failed += run_test_group("frame-renderer", script_task_frame_renderer_tests_main, only, trace);
    failed += run_test_group("input-codec", script_task_input_codec_tests_main, only, trace);
    failed += run_test_group("input-dispatch", script_task_input_dispatch_tests_main, only, trace);
    failed += run_test_group("service-request-codec", script_task_service_request_codec_tests_main, only, trace);
    failed += run_test_group("service-bridge", script_task_service_bridge_tests_main, only, trace);
    failed += run_test_group("worker-inbox", script_task_worker_inbox_tests_main, only, trace);
    failed += run_test_group("value-flow", script_task_value_flow_tests_main, only, trace);
    failed += run_test_group("fatal-codec", script_task_fatal_codec_tests_main, only, trace);
    if (!only.empty() && !g_selected_test_group) {
        std::cerr << "unknown JELLYFRAME_TEST_ONLY group: " << only << '\n';
        ++failed;
    }
    if (failed == 0) {
        std::cout << "script task runtime tests passed\n";
    }
    return failed == 0 ? 0 : 1;
}
