#include "app_runtime/app_host.h"
#include "app_runtime/app_lifecycle.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "app_runtime/host_services.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
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

void bench_runtime_host_completion_pump(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{
        capacity,
        8,
        capacity,
        capacity * 256,
    });
    const AppInstance app = host.launch("org.example.bench", AppRole::App);
    for (std::size_t i = 0; i < capacity; ++i) {
        host.push_completion(HostServiceCompletion{
            static_cast<std::uint32_t>(i + 1),
            HostServiceJobKind::NetworkFetch,
            HostServiceStatus::Completed,
            app.id,
            0,
            0,
            128,
        });
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
}

void bench_network_fetch_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.network", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    network.add_fixture(NetworkFetchFixture{"app://weather", 200, "application/json", "{\"t\":21}"});
    for (std::size_t i = 0; i < capacity; ++i) {
        network.submit_fetch(host, "app://weather", 1000);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        network.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                network.release_response(host, completion.handle);
            }
        }
    }
}

void bench_kv_storage_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity * 2, 8, capacity, capacity * 512, 1});
    host.launch("org.example.storage", AppRole::App);
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, capacity, capacity * 64});
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.submit_set(host, "k" + std::to_string(i), "value");
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.submit_get(host, "k" + std::to_string(i));
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.complete_next(host);
    }
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                storage.release_value(host, completion.handle);
            }
        }
    }
}

void bench_system_event_queue(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 64, 1});
    host.launch("org.example.system-events", AppRole::App);
    AppSystemEventQueue queue(capacity, 8);
    AppSystemStateSnapshot snapshot;
    snapshot.unix_time_ms = 1800000000000ULL;
    snapshot.timezone_offset_minutes = 480;
    snapshot.battery_percent = 80;
    snapshot.network_online = true;
    for (std::size_t i = 0; i < capacity; ++i) {
        queue.push_current(host, AppSystemEventKind::TimeChanged, snapshot);
        ++snapshot.unix_time_ms;
    }
    std::vector<AppSystemEvent> accepted;
    while (!queue.empty()) {
        accepted.clear();
        queue.pump_current(host, accepted);
    }
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
    print_result("app_runtime_host_completion_pump", iterations, average_microseconds(iterations, [&] {
        bench_runtime_host_completion_pump(capacity);
    }));
    print_result("app_runtime_network_fetch_mock", iterations, average_microseconds(iterations, [&] {
        bench_network_fetch_mock(capacity);
    }));
    print_result("app_runtime_kv_storage_mock", iterations, average_microseconds(iterations, [&] {
        bench_kv_storage_mock(capacity);
    }));
    print_result("app_runtime_system_event_queue", iterations, average_microseconds(iterations, [&] {
        bench_system_event_queue(capacity);
    }));

    return 0;
}
