#include <iostream>

int app_capability_broker_tests_main();
int app_device_services_tests_main();
int host_services_tests_main();
int app_frame_policy_tests_main();
int app_services_tests_main();
int app_storage_lifecycle_policy_tests_main();
int app_service_worker_tests_main();
int app_host_tests_main();
int app_lifecycle_tests_main();
int app_load_telemetry_tests_main();
int system_events_tests_main();
int xml_http_request_tests_main();

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
    failed += run_test("app_capability_broker", app_capability_broker_tests_main);
    failed += run_test("app_device_services", app_device_services_tests_main);
    failed += run_test("app_frame_policy", app_frame_policy_tests_main);
    failed += run_test("app_host", app_host_tests_main);
    failed += run_test("app_lifecycle", app_lifecycle_tests_main);
    failed += run_test("app_load_telemetry", app_load_telemetry_tests_main);
    failed += run_test("app_service_worker", app_service_worker_tests_main);
    failed += run_test("app_services", app_services_tests_main);
    failed += run_test("app_storage_lifecycle_policy", app_storage_lifecycle_policy_tests_main);
    failed += run_test("host_services", host_services_tests_main);
    failed += run_test("system_events", system_events_tests_main);
    failed += run_test("xml_http_request", xml_http_request_tests_main);

    if (failed != 0) {
        std::cerr << failed << " app runtime test group(s) failed\n";
        return 1;
    }

    std::cout << "all app runtime tests passed\n";
    return 0;
}
