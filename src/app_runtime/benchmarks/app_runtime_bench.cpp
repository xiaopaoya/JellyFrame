#include "app_runtime/app_host.h"
#include "app_runtime/app_device_services.h"
#include "app_runtime/app_budget.h"
#include "app_runtime/app_lifecycle.h"
#include "app_runtime/app_load_telemetry.h"
#include "app_runtime/app_service_worker.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "app_runtime/xml_http_request.h"
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
    AppRuntimeHostOptions options{
        capacity,
        8,
        capacity,
        capacity * 256,
    };
    AppRuntimeHost host(options);
    AppFrameScratch scratch;
    scratch.reserve_from_options(options);
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
    while (!host.completions().empty()) {
        host.pump_frame_completions(scratch);
    }
}

class BenchServiceWorker final : public AppHostServiceWorker {
public:
    HostServiceCompletion process(const HostServiceRequest& request) override {
        HostServiceCompletion completion;
        completion.status = HostServiceStatus::Completed;
        completion.byte_count = request.timeout_ms;
        return completion;
    }
};

void bench_service_worker_pump(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 64, 1});
    host.launch("org.example.worker", AppRole::App);
    for (std::size_t i = 0; i < capacity; ++i) {
        host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, static_cast<std::uint32_t>(i + 1));
    }
    BenchServiceWorker worker;
    std::vector<HostServiceCompletion> accepted;
    accepted.reserve(8);
    while (!host.requests().empty()) {
        pump_app_host_service_worker(host,
                                     AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
                                     worker);
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
}

void bench_service_worker_group_pump(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity * 3, 8, capacity, capacity * 64, 1});
    host.launch("org.example.worker-group", AppRole::App);
    for (std::size_t i = 0; i < capacity; ++i) {
        host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, static_cast<std::uint32_t>(i + 1));
        host.submit_current(HostServiceJobKind::StorageKv, 0, 0, static_cast<std::uint32_t>(i + 1));
        host.submit_current(HostServiceJobKind::AudioCommand, 0, 0, static_cast<std::uint32_t>(i + 1));
    }
    BenchServiceWorker network;
    BenchServiceWorker storage;
    BenchServiceWorker audio;
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 1, &network},
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 1, &storage},
        AppHostServiceWorkerSlot{HostServiceJobKind::AudioCommand, 1, &audio},
    };
    std::vector<HostServiceCompletion> accepted;
    accepted.reserve(8);
    while (!host.requests().empty()) {
        pump_app_host_service_workers(host, slots, 3);
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
}

void bench_network_fetch_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.network", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    network.add_fixture(NetworkFetchFixture{"/data/weather.json", 200, "application/json", "{\"t\":21}"});
    for (std::size_t i = 0; i < capacity; ++i) {
        network.submit_fetch(host, "/data/weather.json", 1000);
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

void bench_image_decode_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.images", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 32, 32, 32 * 32 * 2, capacity});
    images.add_fixture(ImageDecodeFixture{"/icon", 16, 16, 16, HostPixelFormat::Rgb565, {}});
    for (std::size_t i = 0; i < capacity; ++i) {
        images.submit_decode(host, "/icon", 1000);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        images.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                images.release_surface(host, completion.handle);
            }
        }
    }
}

void bench_audio_command_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity * 4, 8, capacity, capacity * 64, 1});
    host.launch("org.example.audio", AppRole::App);
    AudioCommandMock audio(AudioPlaybackPolicy{true, 64, capacity});
    audio.add_source(AudioSourceFixture{"/tone.mp3", 1000});
    for (std::size_t i = 0; i < capacity; ++i) {
        audio.submit_open(host, "/tone.mp3", 80);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        audio.complete_next(host);
    }
    std::vector<std::uint32_t> handles;
    handles.reserve(capacity);
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                handles.push_back(completion.handle);
            }
        }
    }
    for (std::uint32_t handle : handles) {
        audio.submit_play(host, handle);
        audio.submit_set_volume(host, handle, 40);
        audio.submit_stop(host, handle);
    }
    for (std::size_t i = 0; i < handles.size() * 3; ++i) {
        audio.complete_next(host);
    }
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
    for (std::uint32_t handle : handles) {
        audio.post_ended(host, handle);
    }
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
    for (std::uint32_t handle : handles) {
        audio.post_error(host, handle, 500);
        audio.submit_close(host, handle);
    }
    for (std::size_t i = 0; i < handles.size(); ++i) {
        audio.complete_next(host);
    }
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
    }
}

void bench_sensor_sample_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 64, 1});
    host.launch("org.example.sensors", AppRole::App);
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, capacity});
    sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 100, 0.1f, 0.2f, 0.3f});
    for (std::size_t i = 0; i < capacity; ++i) {
        sensors.submit_sample(host, AppSensorKind::Accelerometer, 100);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        sensors.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                sensors.release_sample(host, completion.handle);
            }
        }
    }
}

void bench_location_snapshot_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 64, 1});
    host.launch("org.example.location", AppRole::App);
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, capacity});
    location.set_fixture(AppLocationSnapshotFixture{100, 31.2304, 121.4737, 4.0f, 8.0f, 0.2f});
    for (std::size_t i = 0; i < capacity; ++i) {
        location.submit_position(host, 100);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        location.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            if (completion.handle != 0) {
                location.release_snapshot(host, completion.handle);
            }
        }
    }
}

void bench_stale_pending_cleanup(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity * 5, 8, capacity, capacity * 64, 1});
    host.launch("org.example.old", AppRole::App);

    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    network.add_fixture(NetworkFetchFixture{"/data", 200, "application/json", "{}"});
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, capacity});
    images.add_fixture(ImageDecodeFixture{"/icon", 8, 8, 8, HostPixelFormat::Rgb565, {}});
    AudioCommandMock audio(AudioPlaybackPolicy{true, 64, capacity});
    audio.add_source(AudioSourceFixture{"/tone.mp3", 100});
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, capacity});
    sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 100});
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, capacity});
    location.set_fixture(AppLocationSnapshotFixture{100, 31.2304, 121.4737});

    for (std::size_t i = 0; i < capacity; ++i) {
        network.submit_fetch(host, "/data");
        images.submit_decode(host, "/icon");
        audio.submit_open(host, "/tone.mp3");
        sensors.submit_sample(host, AppSensorKind::Accelerometer);
        location.submit_position(host);
    }

    host.launch("org.example.new", AppRole::App);
    const std::size_t collected =
        network.collect_stale_pending_fetches(host) +
        images.collect_stale_pending_decodes(host) +
        audio.collect_stale_pending_commands(host) +
        sensors.collect_stale_pending_samples(host) +
        location.collect_stale_pending_snapshots(host);
    if (collected != capacity * 5) {
        std::abort();
    }
}

void bench_image_surface_cache(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.image-cache", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 32, 32, 32 * 32 * 2, capacity});
    for (std::size_t i = 0; i < capacity; ++i) {
        images.add_fixture(ImageDecodeFixture{
            "/icon" + std::to_string(i),
            16,
            16,
            16,
            HostPixelFormat::Rgb565,
            {},
        });
    }
    AppImageSurfaceCache cache;
    std::uint32_t handle = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        cache.resolve_or_request(host, images, "/icon" + std::to_string(i), &handle);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        images.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            cache.handle_completion(completion);
        }
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        cache.resolve_or_request(host, images, "/icon" + std::to_string(i), &handle);
    }
    cache.release_all(host, images);
}

void bench_image_surface_cache_eviction(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.image-cache-evict", AppRole::App);
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 32, 32, 32 * 32 * 2, capacity});
    for (std::size_t i = 0; i < capacity; ++i) {
        images.add_fixture(ImageDecodeFixture{
            "/icon" + std::to_string(i),
            16,
            16,
            16,
            HostPixelFormat::Rgb565,
            {},
        });
    }
    AppImageSurfaceCache cache(AppImageSurfaceCacheOptions{capacity / 2, 0});
    std::uint32_t handle = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        cache.resolve_or_request(host, images, "/icon" + std::to_string(i), &handle);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        images.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            cache.handle_completion(completion);
        }
    }
    cache.evict_unreferenced(host, images);
    cache.release_all(host, images);
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

void bench_local_storage_shadow(std::size_t capacity) {
    AppLocalStorageShadow storage(AppPrivateKvPolicy{true, 16, 32, capacity, capacity * 64});
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.set_item("k" + std::to_string(i), "value");
    }
    std::string value;
    for (std::size_t i = 0; i < capacity; ++i) {
        storage.get_item("k" + std::to_string(i), &value);
    }
    for (std::size_t i = 0; i < capacity; i += 2) {
        storage.remove_item("k" + std::to_string(i));
    }
}

void bench_xml_http_request_mock(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 512, 1});
    host.launch("org.example.xhr", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    network.add_fixture(NetworkFetchFixture{"/data/weather.json", 200, "application/json", "{\"t\":21}"});
    std::vector<AppXmlHttpRequest> requests(capacity);
    for (AppXmlHttpRequest& xhr : requests) {
        xhr.open("GET", "/data/weather.json", true);
        xhr.send(host, network, 1000);
    }
    for (std::size_t i = 0; i < capacity; ++i) {
        network.complete_next(host);
    }
    std::vector<HostServiceCompletion> accepted;
    AppXhrEventKind events[AppXmlHttpRequest::kMaxQueuedEvents];
    while (!host.completions().empty()) {
        accepted.clear();
        host.pump_frame_completions(accepted);
        for (const HostServiceCompletion& completion : accepted) {
            for (AppXmlHttpRequest& xhr : requests) {
                if (xhr.handle_completion(host, network, completion)) {
                    xhr.take_events(events, AppXmlHttpRequest::kMaxQueuedEvents);
                    break;
                }
            }
        }
    }
}

const std::vector<std::uint8_t>& tiny_jffont_bytes() {
    static const std::vector<std::uint8_t> bytes = {
        'J', 'F', 'F', 'O', 'N', 'T', '0', 0,
        0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x08, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0x05, 0x07, 0x06, 0x01,
        0x2d, 0x4e, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x01,
        0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
        0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10,
    };
    return bytes;
}

void bench_font_fallback_measure(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 128, 1});
    host.launch("org.example.fonts", AppRole::App);
    const std::vector<std::uint8_t>& bytes = tiny_jffont_bytes();
    host.load_current_jffont(bytes.data(), bytes.size());
    TextMeasureProvider provider = host.fonts().measure_provider();
    TextMetrics metrics;
    for (std::size_t i = 0; i < capacity; ++i) {
        provider.measure("A\xe4\xb8\xad", 8, 400, &metrics, provider.context);
    }
}

void bench_load_telemetry(std::size_t capacity) {
    DirtyRegionResult region;
    region.mode = DirtyRegionMode::DirtyRects;
    region.rects.push_back(Rect{0, 0, 24, 24});

    AppLoadTelemetryInput input;
    input.frame_policy = AppFramePolicy{true, true, true, true, false};
    input.service_policy.network_fetch = true;
    input.work.animation_callbacks_to_pump = 1;
    input.update.action = FrameUpdateAction::RepaintExisting;
    input.update.reason = FrameUpdateReason::PaintOnlyDirty;
    input.dirty_region = &region;
    input.viewport = Rect{0, 0, 240, 240};
    input.service_request_capacity = capacity;
    input.service_completion_capacity = capacity;
    for (std::size_t i = 0; i < capacity; ++i) {
        input.pending_service_requests = i % (capacity + 1);
        input.pending_service_completions = (capacity - i) % (capacity + 1);
        input.active_animations = i & 1U;
        const AppLoadTelemetry telemetry = analyze_app_load(input);
        if (telemetry.level == AppLoadLevel::Overloaded) {
            input.work.has_more_animation_callbacks = false;
        }
    }
}

void bench_app_budget_snapshot(std::size_t capacity) {
    AppRuntimeHost host(AppRuntimeHostOptions{capacity, 8, capacity, capacity * 256, 1});
    host.launch("org.example.budget", AppRole::App);
    for (std::size_t i = 0; i < capacity / 2; ++i) {
        host.submit_current(HostServiceJobKind::NetworkFetch);
        host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    }
    AppSystemEventQueue system_events(capacity, 8);
    AppSystemStateSnapshot system_snapshot;
    for (std::size_t i = 0; i < capacity / 4; ++i) {
        system_events.push_current(host, AppSystemEventKind::TimeChanged, system_snapshot);
    }

    HostBudgets budgets;
    FrameLoopPendingWork pending;
    pending.pending_input_events = capacity / 8;
    pending.pending_timer_callbacks = capacity / 8;
    pending.pending_animation_callbacks = 1;

    AppBudgetSnapshotInput input;
    input.host_budgets = &budgets;
    input.system_events = &system_events;
    input.pending_work = &pending;
    input.active_animations = 1;
    input.script_timers = capacity / 8;
    input.script_event_listeners = capacity / 4;
    input.detached_dom_nodes = capacity / 16;

    const AppBudgetSnapshot budget = collect_app_budget_snapshot(host, input);
    if (budget.app_instance_id == 0) {
        std::abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_app_runtime_microbench [iterations=10000] [capacity=32]\n";
        return 0;
    }
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
    print_result("app_runtime_service_worker_pump", iterations, average_microseconds(iterations, [&] {
        bench_service_worker_pump(capacity);
    }));
    print_result("app_runtime_service_worker_group_pump", iterations, average_microseconds(iterations, [&] {
        bench_service_worker_group_pump(capacity);
    }));
    print_result("app_runtime_network_fetch_mock", iterations, average_microseconds(iterations, [&] {
        bench_network_fetch_mock(capacity);
    }));
    print_result("app_runtime_kv_storage_mock", iterations, average_microseconds(iterations, [&] {
        bench_kv_storage_mock(capacity);
    }));
    print_result("app_runtime_image_decode_mock", iterations, average_microseconds(iterations, [&] {
        bench_image_decode_mock(capacity);
    }));
    print_result("app_runtime_audio_command_mock", iterations, average_microseconds(iterations, [&] {
        bench_audio_command_mock(capacity);
    }));
    print_result("app_runtime_sensor_sample_mock", iterations, average_microseconds(iterations, [&] {
        bench_sensor_sample_mock(capacity);
    }));
    print_result("app_runtime_location_snapshot_mock", iterations, average_microseconds(iterations, [&] {
        bench_location_snapshot_mock(capacity);
    }));
    print_result("app_runtime_stale_pending_cleanup", iterations, average_microseconds(iterations, [&] {
        bench_stale_pending_cleanup(capacity);
    }));
    print_result("app_runtime_image_surface_cache", iterations, average_microseconds(iterations, [&] {
        bench_image_surface_cache(capacity);
    }));
    print_result("app_runtime_image_surface_cache_eviction", iterations, average_microseconds(iterations, [&] {
        bench_image_surface_cache_eviction(capacity);
    }));
    print_result("app_runtime_system_event_queue", iterations, average_microseconds(iterations, [&] {
        bench_system_event_queue(capacity);
    }));
    print_result("app_runtime_local_storage_shadow", iterations, average_microseconds(iterations, [&] {
        bench_local_storage_shadow(capacity);
    }));
    print_result("app_runtime_xml_http_request_mock", iterations, average_microseconds(iterations, [&] {
        bench_xml_http_request_mock(capacity);
    }));
    print_result("app_runtime_font_fallback_measure", iterations, average_microseconds(iterations, [&] {
        bench_font_fallback_measure(capacity);
    }));
    print_result("app_runtime_load_telemetry", iterations, average_microseconds(iterations, [&] {
        bench_load_telemetry(capacity);
    }));
    print_result("app_runtime_budget_snapshot", iterations, average_microseconds(iterations, [&] {
        bench_app_budget_snapshot(capacity);
    }));

    return 0;
}
