#include <iostream>

#if defined(JELLYFRAME_TEST_ASSERTS_ENABLED) && defined(NDEBUG)
#error "JellyFrame test targets must run with assert() enabled."
#endif

int device_install_transaction_tests_main();
int device_runtime_protocol_tests_main();

namespace {

int run_test(const char* name, int (*test_main)()) {
    std::cout << "[ RUN      ] " << name << '\n' << std::flush;
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
    failed += run_test("device_install_transaction", device_install_transaction_tests_main);
    failed += run_test("device_runtime_protocol", device_runtime_protocol_tests_main);
    if (failed != 0) {
        std::cerr << failed << " device runtime contract test group(s) failed\n";
        return 1;
    }
    std::cout << "all device runtime contract tests passed\n";
    return 0;
}
