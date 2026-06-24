#include "app_runtime/app_service_worker.h"
#include "app_runtime/app_device_services.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"

#include <cassert>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host(std::size_t in_flight_capacity = 4,
                         std::size_t completion_capacity = 4) {
    return AppRuntimeHost(AppRuntimeHostOptions{
        in_flight_capacity,
        completion_capacity,
        8,
        4096,
        1,
    });
}

class EchoWorker final : public AppHostServiceWorker {
public:
    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        last_request = request;
        HostServiceCompletion completion;
        completion.status = status;
        completion.handle = handle;
        completion.error_code = error_code;
        completion.byte_count = byte_count;
        return completion;
    }

    int calls = 0;
    HostServiceRequest last_request;
    HostServiceStatus status = HostServiceStatus::Completed;
    std::uint32_t handle = 0;
    std::uint32_t error_code = 0;
    std::uint32_t byte_count = 0;
};

class NetworkFetchWorker final : public AppHostServiceWorker {
public:
    NetworkFetchWorker(AppRuntimeHost& host, NetworkFetchMock& network)
        : host_(host), network_(network) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return network_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    NetworkFetchMock& network_;
};

class StorageKvWorker final : public AppHostServiceWorker {
public:
    StorageKvWorker(AppRuntimeHost& host, AppPrivateKvStorageMock& storage)
        : host_(host), storage_(storage) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return storage_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    AppPrivateKvStorageMock& storage_;
};

class ImageDecodeWorker final : public AppHostServiceWorker {
public:
    ImageDecodeWorker(AppRuntimeHost& host, ImageDecodeMock& images)
        : host_(host), images_(images) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return images_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    ImageDecodeMock& images_;
};

class AudioCommandWorker final : public AppHostServiceWorker {
public:
    AudioCommandWorker(AppRuntimeHost& host, AudioCommandMock& audio)
        : host_(host), audio_(audio) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return audio_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    AudioCommandMock& audio_;
};

class SensorSampleWorker final : public AppHostServiceWorker {
public:
    SensorSampleWorker(AppRuntimeHost& host, AppSensorSampleMock& sensors)
        : host_(host), sensors_(sensors) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return sensors_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    AppSensorSampleMock& sensors_;
};

class LocationSnapshotWorker final : public AppHostServiceWorker {
public:
    LocationSnapshotWorker(AppRuntimeHost& host, AppLocationSnapshotMock& location)
        : host_(host), location_(location) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        ++calls;
        return location_.complete_request(host_, request);
    }

    int calls = 0;

private:
    AppRuntimeHost& host_;
    AppLocationSnapshotMock& location_;
};

AppSystemStateSnapshot make_snapshot(std::uint64_t tick) {
    AppSystemStateSnapshot snapshot;
    snapshot.unix_time_ms = 1700000000000ULL + tick * 1000ULL;
    snapshot.timezone_offset_minutes = 480;
    snapshot.battery_percent = static_cast<std::uint8_t>(80 - (tick % 8));
    snapshot.charging = (tick % 3) == 0;
    snapshot.network_online = (tick % 2) == 0;
    snapshot.screen_on = true;
    snapshot.low_power_mode = (tick % 5) == 0;
    return snapshot;
}

void worker_pump_processes_only_selected_service_kind() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.app", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);
    assert(host.submit_current(HostServiceJobKind::StorageKv).accepted);

    EchoWorker worker;
    worker.handle = 42;
    AppHostServiceWorkerPumpResult result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::StorageKv, 1},
        worker);
    assert(result.requests_processed == 1);
    assert(result.completions_posted == 1);
    assert(!result.completion_queue_full);
    assert(worker.calls == 1);
    assert(worker.last_request.kind == HostServiceJobKind::StorageKv);

    HostServiceRequest remaining;
    assert(host.pop_worker_request(HostServiceJobKind::NetworkFetch, remaining));
    assert(!host.pop_worker_request(HostServiceJobKind::StorageKv, remaining));

    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    assert(accepted.size() == 1);
    assert(accepted.front().kind == HostServiceJobKind::StorageKv);
    assert(accepted.front().handle == 42);
}

void worker_pump_normalizes_completion_identity() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.identity", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);

    EchoWorker worker;
    worker.error_code = 404;
    AppHostServiceWorkerPumpResult result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
        worker);
    assert(result.requests_processed == 1);
    assert(result.completions_posted == 1);

    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 1);
    assert(accepted.front().kind == HostServiceJobKind::NetworkFetch);
    assert(accepted.front().app_instance_id == app.id);
    assert(accepted.front().error_code == 404);
}

void worker_pump_does_not_pop_when_completion_queue_is_full() {
    AppRuntimeHost host = make_host(1, 1);
    host.launch("org.example.full", AppRole::App);
    assert(host.push_completion(HostServiceCompletion{
        99,
        HostServiceJobKind::Other,
        HostServiceStatus::Completed,
        host.current_app_instance_id(),
        0,
        0,
        0,
    }));
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);

    EchoWorker worker;
    AppHostServiceWorkerPumpResult result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
        worker);
    assert(result.requests_processed == 0);
    assert(result.completions_posted == 0);
    assert(result.completion_queue_full);
    assert(worker.calls == 0);

    HostServiceRequest request;
    assert(host.pop_worker_request(HostServiceJobKind::NetworkFetch, request));
}

void worker_pump_respects_per_tick_request_budget() {
    AppRuntimeHost host = make_host(4, 2);
    host.launch("org.example.timing", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 10).accepted);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 20).accepted);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 30).accepted);

    EchoWorker worker;
    AppFrameScratch scratch;
    scratch.reserve_from_options(AppRuntimeHostOptions{4, 2, 8, 4096, 1});

    AppHostServiceWorkerPumpResult result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
        worker);
    assert(result.requests_processed == 1);
    assert(result.completions_posted == 1);
    assert(host.requests().size() == 2);
    AppCompletionPumpResult pumped = host.pump_frame_completions(scratch);
    assert(pumped.accepted == 1);
    assert(scratch.accepted_completions.size() == 1);
    assert(scratch.accepted_completions.front().job_id == 1);

    result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
        worker);
    assert(result.requests_processed == 1);
    assert(result.completions_posted == 1);
    assert(host.requests().size() == 1);
    pumped = host.pump_frame_completions(scratch);
    assert(pumped.accepted == 1);
    assert(scratch.accepted_completions.front().job_id == 2);

    result = pump_app_host_service_worker(
        host,
        AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 1},
        worker);
    assert(result.requests_processed == 1);
    assert(result.completions_posted == 1);
    assert(host.requests().empty());
    pumped = host.pump_frame_completions(scratch);
    assert(pumped.accepted == 1);
    assert(scratch.accepted_completions.front().job_id == 3);
}

void worker_pump_remains_stable_across_many_ticks() {
    constexpr std::size_t kRequestCount = 96;
    AppRuntimeHost host = make_host(128, 4);
    const AppInstance app = host.launch("org.example.soak", AppRole::App);
    for (std::size_t index = 0; index < kRequestCount; ++index) {
        assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 10).accepted);
    }

    EchoWorker worker;
    AppFrameScratch scratch;
    scratch.reserve_from_options(AppRuntimeHostOptions{128, 4, 8, 4096, 1});

    std::uint32_t next_job_id = 1;
    std::size_t accepted_total = 0;
    for (std::size_t tick = 0; tick < 64 && accepted_total < kRequestCount; ++tick) {
        AppHostServiceWorkerPumpResult result = pump_app_host_service_worker(
            host,
            AppHostServiceWorkerPumpOptions{HostServiceJobKind::NetworkFetch, 3},
            worker);
        assert(result.requests_processed <= 3);
        assert(result.completions_posted == result.requests_processed);
        assert(!result.completion_queue_full);

        const AppCompletionPumpResult pumped = host.pump_frame_completions(scratch);
        assert(result.requests_processed != 0 || pumped.accepted != 0);
        assert(pumped.dropped_stale == 0);
        for (const HostServiceCompletion& completion : scratch.accepted_completions) {
            assert(completion.app_instance_id == app.id);
            assert(completion.kind == HostServiceJobKind::NetworkFetch);
            assert(completion.job_id == next_job_id++);
        }
        accepted_total += pumped.accepted;
    }

    assert(host.requests().empty());
    assert(worker.calls == static_cast<int>(kRequestCount));
}

void worker_group_pump_processes_multiple_services_with_fixed_budgets() {
    AppRuntimeHost host = make_host(8, 4);
    host.launch("org.example.group", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 10).accepted);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, 20).accepted);
    assert(host.submit_current(HostServiceJobKind::StorageKv, 0, 0, 30).accepted);

    EchoWorker network;
    EchoWorker storage;
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 1, &network},
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 1, &storage},
    };
    const AppHostServiceWorkerGroupPumpResult result =
        pump_app_host_service_workers(host, slots, 2);

    assert(result.workers_considered == 2);
    assert(result.workers_pumped == 2);
    assert(result.empty_workers == 0);
    assert(result.requests_processed == 2);
    assert(result.completions_posted == 2);
    assert(!result.completion_queue_full);
    assert(network.calls == 1);
    assert(storage.calls == 1);
    assert(host.requests().size() == 1);

    HostServiceRequest remaining;
    assert(host.pop_worker_request(HostServiceJobKind::NetworkFetch, remaining));
    assert(remaining.timeout_ms == 20);
}

void worker_group_pump_stops_before_next_worker_when_completion_queue_is_full() {
    AppRuntimeHost host = make_host(4, 1);
    host.launch("org.example.group-full", AppRole::App);
    assert(host.push_completion(HostServiceCompletion{
        99,
        HostServiceJobKind::Other,
        HostServiceStatus::Completed,
        host.current_app_instance_id(),
        0,
        0,
        0,
    }));
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);
    assert(host.submit_current(HostServiceJobKind::StorageKv).accepted);

    EchoWorker network;
    EchoWorker storage;
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 1, &network},
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 1, &storage},
    };
    const AppHostServiceWorkerGroupPumpResult result =
        pump_app_host_service_workers(host, slots, 2);

    assert(result.workers_considered == 1);
    assert(result.workers_pumped == 0);
    assert(result.requests_processed == 0);
    assert(result.completions_posted == 0);
    assert(result.completion_queue_full);
    assert(network.calls == 0);
    assert(storage.calls == 0);
    assert(host.requests().size() == 2);
}

void worker_group_pump_ignores_empty_slots_without_allocation_or_side_effects() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.group-empty", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);

    EchoWorker network;
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 1, nullptr},
        AppHostServiceWorkerSlot{HostServiceJobKind::AudioCommand, 0, &network},
    };
    const AppHostServiceWorkerGroupPumpResult result =
        pump_app_host_service_workers(host, slots, 2);

    assert(result.workers_considered == 0);
    assert(result.workers_pumped == 0);
    assert(result.requests_processed == 0);
    assert(result.completions_posted == 0);
    assert(network.calls == 0);
    assert(host.requests().size() == 1);
}

void worker_group_pump_handles_real_network_and_storage_mocks_across_ticks() {
    constexpr std::size_t kIterations = 24;
    AppRuntimeHost host = make_host(kIterations * 3, 6);
    host.launch("org.example.real-workers", AppRole::App);

    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    assert(network.add_fixture(NetworkFetchFixture{"/data/status.json", 200, "application/json", "{\"ok\":true}"}));
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, kIterations + 2, kIterations * 48});

    for (std::size_t index = 0; index < kIterations; ++index) {
        assert(network.submit_fetch(host, "/data/status.json", 1000).accepted());
        const std::string key = "k" + std::to_string(index);
        assert(storage.submit_set(host, key, "value").accepted());
        assert(storage.submit_get(host, key).accepted());
    }

    NetworkFetchWorker network_worker(host, network);
    StorageKvWorker storage_worker(host, storage);
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 2, &network_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 2, &storage_worker},
    };
    AppFrameScratch scratch;
    scratch.reserve_from_options(AppRuntimeHostOptions{kIterations * 3, 6, 8, 4096, 1});

    std::size_t network_completions = 0;
    std::size_t storage_completions = 0;
    std::size_t released_network_handles = 0;
    std::size_t released_storage_handles = 0;
    for (std::size_t tick = 0; tick < 128 && (network_completions < kIterations ||
                                              storage_completions < kIterations * 2); ++tick) {
        const AppHostServiceWorkerGroupPumpResult result = pump_app_host_service_workers(host, slots, 2);
        assert(result.requests_processed <= 4);
        assert(result.completions_posted == result.requests_processed);
        assert(!result.completion_queue_full);

        const AppCompletionPumpResult pumped = host.pump_frame_completions(scratch);
        assert(pumped.dropped_stale == 0);
        for (const HostServiceCompletion& completion : scratch.accepted_completions) {
            assert(completion.status == HostServiceStatus::Completed);
            if (completion.kind == HostServiceJobKind::NetworkFetch) {
                ++network_completions;
                assert(completion.handle != 0);
                assert(network.response(completion.handle) != nullptr);
                assert(network.release_response(host, completion.handle));
                ++released_network_handles;
            } else if (completion.kind == HostServiceJobKind::StorageKv) {
                ++storage_completions;
                if (completion.handle != 0) {
                    assert(storage.value(completion.handle) != nullptr);
                    assert(storage.release_value(host, completion.handle));
                    ++released_storage_handles;
                }
            } else {
                assert(false && "unexpected service completion kind");
            }
        }
    }

    assert(host.requests().empty());
    assert(network_worker.calls == static_cast<int>(kIterations));
    assert(storage_worker.calls == static_cast<int>(kIterations * 2));
    assert(network_completions == kIterations);
    assert(storage_completions == kIterations * 2);
    assert(released_network_handles == kIterations);
    assert(released_storage_handles == kIterations);
    assert(host.handles().active_count() == 0);
    assert(storage.pending_count() == 0);
}

void worker_group_pump_handles_mixed_media_and_system_events_across_ticks() {
    constexpr std::size_t kIterations = 20;
    AppRuntimeHost host = make_host(kIterations * 6, 6);
    host.launch("org.example.rtos-workers", AppRole::App);

    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    assert(network.add_fixture(NetworkFetchFixture{"/data/status.json", 200, "application/json", "{\"ok\":true}"}));
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, kIterations + 2, kIterations * 48});
    ImageDecodeMock images(ImageDecodePolicy{true, 64, 16, 16, 16 * 16 * 2, kIterations});
    assert(images.add_fixture(ImageDecodeFixture{"/img/icon.raw", 8, 8, 8, HostPixelFormat::Rgb565, {}}));
    AudioCommandMock audio(AudioPlaybackPolicy{true, 128, 4});
    assert(audio.add_source(AudioSourceFixture{"/audio/tone.wav", 1000}));
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, kIterations});
    assert(sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 1000, 0.1f, 0.2f, 0.3f}));
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, kIterations});
    assert(location.set_fixture(AppLocationSnapshotFixture{1000, 31.2304, 121.4737, 4.0f, 8.0f, 0.2f}));
    AppSystemEventQueue system_events(8, 3);

    for (std::size_t index = 0; index < kIterations; ++index) {
        assert(network.submit_fetch(host, "/data/status.json", 1000).accepted());
        assert(storage.submit_set(host, "k" + std::to_string(index), "v").accepted());
        assert(images.submit_decode(host, "/img/icon.raw", 1000).accepted());
        assert(audio.submit_open(host, "/audio/tone.wav", 100, 1000).accepted());
        assert(sensors.submit_sample(host, AppSensorKind::Accelerometer, 1000).accepted());
        assert(location.submit_position(host, 1000).accepted());
    }

    NetworkFetchWorker network_worker(host, network);
    StorageKvWorker storage_worker(host, storage);
    ImageDecodeWorker image_worker(host, images);
    AudioCommandWorker audio_worker(host, audio);
    SensorSampleWorker sensor_worker(host, sensors);
    LocationSnapshotWorker location_worker(host, location);
    AppHostServiceWorkerSlot slots[] = {
        AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 1, &network_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::StorageKv, 1, &storage_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::ImageDecode, 1, &image_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::AudioCommand, 1, &audio_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::SensorSample, 1, &sensor_worker},
        AppHostServiceWorkerSlot{HostServiceJobKind::LocationSnapshot, 1, &location_worker},
    };

    AppFrameScratch scratch;
    scratch.reserve_from_options(AppRuntimeHostOptions{kIterations * 6, 6, 8, 4096, 1});
    std::vector<AppSystemEvent> accepted_events;
    accepted_events.reserve(3);

    std::size_t network_completions = 0;
    std::size_t storage_completions = 0;
    std::size_t image_completions = 0;
    std::size_t audio_completions = 0;
    std::size_t sensor_completions = 0;
    std::size_t location_completions = 0;
    std::size_t system_events_accepted = 0;
    for (std::size_t tick = 0; tick < 128 && (network_completions < kIterations ||
                                              storage_completions < kIterations ||
                                              image_completions < kIterations ||
                                              audio_completions < kIterations ||
                                              sensor_completions < kIterations ||
                                              location_completions < kIterations); ++tick) {
        assert(system_events.try_push_current(host,
                                              AppSystemEventKind::NetworkStatusChanged,
                                              make_snapshot(tick)) == AppSystemEventPushStatus::Accepted);

        const AppHostServiceWorkerGroupPumpResult result = pump_app_host_service_workers(host, slots, 6);
        assert(result.requests_processed <= 6);
        assert(result.completions_posted == result.requests_processed);
        assert(!result.completion_queue_full);

        const AppCompletionPumpResult pumped = host.pump_frame_completions(scratch);
        assert(pumped.dropped_stale == 0);
        for (const HostServiceCompletion& completion : scratch.accepted_completions) {
            assert(completion.status == HostServiceStatus::Completed);
            if (completion.kind == HostServiceJobKind::NetworkFetch) {
                ++network_completions;
                assert(completion.handle != 0);
                assert(network.release_response(host, completion.handle));
            } else if (completion.kind == HostServiceJobKind::StorageKv) {
                ++storage_completions;
            } else if (completion.kind == HostServiceJobKind::ImageDecode) {
                ++image_completions;
                assert(completion.handle != 0);
                assert(images.release_surface(host, completion.handle));
            } else if (completion.kind == HostServiceJobKind::AudioCommand) {
                ++audio_completions;
                assert(completion.handle != 0);
                assert(audio.release_stream(host, completion.handle));
            } else if (completion.kind == HostServiceJobKind::SensorSample) {
                ++sensor_completions;
                assert(completion.handle != 0);
                assert(sensors.release_sample(host, completion.handle));
            } else if (completion.kind == HostServiceJobKind::LocationSnapshot) {
                ++location_completions;
                assert(completion.handle != 0);
                assert(location.release_snapshot(host, completion.handle));
            } else {
                assert(false && "unexpected mixed worker completion kind");
            }
        }
        network.collect_released_responses(host);
        images.collect_released_surfaces(host);
        audio.collect_released_streams(host);
        sensors.collect_released_samples(host);
        location.collect_released_snapshots(host);

        const AppSystemEventPumpResult event_result = system_events.pump_current(host, accepted_events);
        assert(event_result.stale == 0);
        system_events_accepted += event_result.accepted;
        accepted_events.clear();
    }

    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(system_events.empty());
    assert(host.handles().active_count() == 0);
    assert(network_worker.calls == static_cast<int>(kIterations));
    assert(storage_worker.calls == static_cast<int>(kIterations));
    assert(image_worker.calls == static_cast<int>(kIterations));
    assert(audio_worker.calls == static_cast<int>(kIterations));
    assert(sensor_worker.calls == static_cast<int>(kIterations));
    assert(location_worker.calls == static_cast<int>(kIterations));
    assert(network_completions == kIterations);
    assert(storage_completions == kIterations);
    assert(image_completions == kIterations);
    assert(audio_completions == kIterations);
    assert(sensor_completions == kIterations);
    assert(location_completions == kIterations);
    assert(system_events_accepted >= kIterations);
}

} // namespace

int main() {
    worker_pump_processes_only_selected_service_kind();
    worker_pump_normalizes_completion_identity();
    worker_pump_does_not_pop_when_completion_queue_is_full();
    worker_pump_respects_per_tick_request_budget();
    worker_pump_remains_stable_across_many_ticks();
    worker_group_pump_processes_multiple_services_with_fixed_budgets();
    worker_group_pump_stops_before_next_worker_when_completion_queue_is_full();
    worker_group_pump_ignores_empty_slots_without_allocation_or_side_effects();
    worker_group_pump_handles_real_network_and_storage_mocks_across_ticks();
    worker_group_pump_handles_mixed_media_and_system_events_across_ticks();
    return 0;
}
