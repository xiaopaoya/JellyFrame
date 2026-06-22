#include "app_runtime/app_service_worker.h"
#include "app_runtime/app_services.h"

#include <cassert>
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
    return 0;
}
