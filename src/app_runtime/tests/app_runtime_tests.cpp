#include <iostream>

int host_services_tests_main();

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
    failed += run_test("host_services", host_services_tests_main);

    if (failed != 0) {
        std::cerr << failed << " app runtime test group(s) failed\n";
        return 1;
    }

    std::cout << "all app runtime tests passed\n";
    return 0;
}
