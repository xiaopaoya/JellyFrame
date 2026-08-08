#include <iostream>

#if defined(JELLYFRAME_TEST_ASSERTS_ENABLED) && defined(NDEBUG)
#error "JellyFrame test targets must run with assert() enabled."
#endif

int script_runtime_tests_main();
#if defined(JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME)
int script_task_worker_runtime_tests_main();
#endif

namespace {

int run_test(const char* name, int (*test_main)()) {
    std::cout << "[ RUN      ] " << name << '\n';
    const int result = test_main();
    if (result == 0) {
        std::cout << "[       OK ] " << name << '\n';
    } else {
        std::cout << "[  FAILED  ] " << name << '\n';
    }
    return result == 0 ? 0 : 1;
}

} // namespace

int main() {
    int failed = 0;
    failed += run_test("script_runtime", script_runtime_tests_main);
#if defined(JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME)
    failed += run_test("script_task_worker_runtime", script_task_worker_runtime_tests_main);
#endif

    if (failed != 0) {
        std::cerr << failed << " script test group(s) failed\n";
        return 1;
    }

    std::cout << "all script tests passed\n";
    return 0;
}
