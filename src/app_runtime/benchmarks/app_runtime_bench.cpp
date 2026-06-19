#include "app_runtime/app_lifecycle.h"
#include "app_runtime/host_services.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn>
double average_microseconds(int iterations, Fn fn) {
    const auto begin = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    const auto end = Clock::now();
    const auto total = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    return static_cast<double>(total) / static_cast<double>(iterations);
}

void print_result(const char* name, int iterations, double average_us) {
    std::cout << name << " iterations=" << iterations << " avg_us=" << average_us << '\n';
}

void bench_request_queue(std::size_t capacity) {
    HostServiceRequestQueue queue(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        queue.submit(HostServiceJobKind::NetworkFetch,
                     static_cast<std::uint32_t>((i % 4) + 1),
                     0,
                     static_cast<std::uint8_t>(i % 8),
                     1000);
    }
    HostServiceRequest request;
    while (queue.pop_next(request)) {
    }
}

void bench_completion_queue(std::size_t capacity) {
    HostServiceCompletionQueue queue(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        queue.push(HostServiceCompletion{
            static_cast<std::uint32_t>(i + 1),
            HostServiceJobKind::NetworkFetch,
            HostServiceStatus::Completed,
            static_cast<std::uint32_t>((i % 4) + 1),
            0,
            0,
            128,
        });
    }
    std::vector<HostServiceCompletion> output;
    output.reserve(capacity);
    while (!queue.empty()) {
        queue.pop(8, output);
        output.clear();
    }
}

void bench_handle_table(std::size_t capacity) {
    HostHandleTable table(capacity, capacity * 256);
    std::vector<std::uint32_t> handles;
    handles.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        handles.push_back(table.allocate(HostServiceHandleKind::FetchResponse,
                                         static_cast<std::uint32_t>((i % 4) + 1),
                                         128,
                                         nullptr));
    }
    for (std::uint32_t handle : handles) {
        (void)table.lookup(handle);
    }
    for (std::uint32_t handle : handles) {
        table.release(handle);
    }
}

void bench_lifecycle_teardown(std::size_t capacity) {
    AppLifecycleController lifecycle;
    HostServiceRequestQueue requests(capacity);
    HostServiceCompletionQueue completions(capacity);
    HostHandleTable handles(capacity, capacity * 256);
    const AppInstance app = lifecycle.launch("org.example.bench", AppRole::App);
    for (std::size_t i = 0; i < capacity; ++i) {
        requests.submit(HostServiceJobKind::NetworkFetch, app.id);
        completions.push(HostServiceCompletion{
            static_cast<std::uint32_t>(i + 1),
            HostServiceJobKind::NetworkFetch,
            HostServiceStatus::Completed,
            app.id,
            0,
            0,
            128,
        });
        handles.allocate(HostServiceHandleKind::FetchResponse, app.id, 128, nullptr);
    }
    lifecycle.exit_current(&requests, &completions, &handles);
}

} // namespace

int main(int argc, char** argv) {
    const int iterations = argc >= 2 ? std::max(1, std::atoi(argv[1])) : 10000;
    const std::size_t capacity = argc >= 3 ? static_cast<std::size_t>(std::max(1, std::atoi(argv[2]))) : 32;

    print_result("app_runtime_request_queue", iterations, average_microseconds(iterations, [&] {
        bench_request_queue(capacity);
    }));
    print_result("app_runtime_completion_queue", iterations, average_microseconds(iterations, [&] {
        bench_completion_queue(capacity);
    }));
    print_result("app_runtime_handle_table", iterations, average_microseconds(iterations, [&] {
        bench_handle_table(capacity);
    }));
    print_result("app_runtime_lifecycle_teardown", iterations, average_microseconds(iterations, [&] {
        bench_lifecycle_teardown(capacity);
    }));

    return 0;
}
