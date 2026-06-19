#include "app_runtime/host_services.h"

#include <cassert>
#include <vector>

using namespace jellyframe;

namespace {

void request_queue_is_bounded_and_priority_ordered() {
    HostServiceRequestQueue queue(2);
    const auto first = queue.submit(HostServiceJobKind::NetworkFetch, 10, 0, 1);
    const auto second = queue.submit(HostServiceJobKind::ImageDecode, 10, 0, 5);
    const auto third = queue.submit(HostServiceJobKind::AudioCommand, 10);

    assert(first.accepted);
    assert(second.accepted);
    assert(!third.accepted);
    assert(third.rejected_status == HostServiceStatus::BudgetExceeded);
    assert(queue.full());

    HostServiceRequest request;
    assert(queue.pop_next(request));
    assert(request.job_id == second.job_id);
    assert(request.kind == HostServiceJobKind::ImageDecode);
    assert(request.priority == 5);

    assert(queue.pop_next(request));
    assert(request.job_id == first.job_id);
    assert(queue.empty());
    assert(!queue.pop_next(request));
}

void request_queue_cancels_pending_jobs() {
    HostServiceRequestQueue queue(4);
    const auto first = queue.submit(HostServiceJobKind::NetworkFetch, 1);
    const auto second = queue.submit(HostServiceJobKind::ImageDecode, 2);
    const auto third = queue.submit(HostServiceJobKind::BundleInstall, 2);

    assert(queue.cancel_pending(second.job_id));
    assert(!queue.cancel_pending(second.job_id));
    assert(queue.size() == 2);
    assert(queue.cancel_app_instance(2) == 1);
    assert(queue.size() == 1);

    HostServiceRequest request;
    assert(queue.pop_next(request));
    assert(request.job_id == first.job_id);
    assert(!queue.cancel_pending(third.job_id));
}

void request_queue_can_pop_by_kind_without_consuming_other_jobs() {
    HostServiceRequestQueue queue(4);
    const auto network_low = queue.submit(HostServiceJobKind::NetworkFetch, 1, 0, 1);
    const auto storage = queue.submit(HostServiceJobKind::StorageKv, 1, 0, 9);
    const auto network_high = queue.submit(HostServiceJobKind::NetworkFetch, 1, 0, 3);

    HostServiceRequest request;
    assert(queue.pop_next(HostServiceJobKind::NetworkFetch, request));
    assert(request.job_id == network_high.job_id);
    assert(request.kind == HostServiceJobKind::NetworkFetch);
    assert(queue.size() == 2);

    assert(queue.pop_next(HostServiceJobKind::StorageKv, request));
    assert(request.job_id == storage.job_id);
    assert(queue.size() == 1);

    assert(!queue.pop_next(HostServiceJobKind::ImageDecode, request));
    assert(queue.pop_next(request));
    assert(request.job_id == network_low.job_id);
    assert(queue.empty());
}

void completion_queue_drains_with_frame_budget() {
    HostServiceCompletionQueue queue(4);
    assert(queue.push(HostServiceCompletion{1, HostServiceJobKind::NetworkFetch, HostServiceStatus::Completed, 7}));
    assert(queue.push(HostServiceCompletion{2, HostServiceJobKind::ImageDecode, HostServiceStatus::Failed, 8}));
    assert(queue.push(HostServiceCompletion{3, HostServiceJobKind::BundleInstall, HostServiceStatus::Timeout, 7}));

    std::vector<HostServiceCompletion> completions;
    assert(queue.pop(2, completions) == 2);
    assert(completions.size() == 2);
    assert(completions[0].job_id == 1);
    assert(completions[1].job_id == 2);
    assert(queue.size() == 1);

    assert(queue.discard_app_instance(7) == 1);
    assert(queue.empty());
}

void completion_queue_rejects_overflow() {
    HostServiceCompletionQueue queue(1);
    assert(queue.push(HostServiceCompletion{1, HostServiceJobKind::NetworkFetch, HostServiceStatus::Completed, 1}));
    assert(!queue.push(HostServiceCompletion{2, HostServiceJobKind::NetworkFetch, HostServiceStatus::Completed, 1}));
}

void handle_table_rejects_stale_handles() {
    HostHandleTable handles(2, 1024);
    const std::uint32_t first =
        handles.allocate(HostServiceHandleKind::Surface, 1, 128, reinterpret_cast<void*>(0x10));
    assert(first != 0);
    assert(handles.active_count() == 1);
    assert(handles.used_bytes() == 128);

    const HostHandleInfo* info = handles.lookup(first);
    assert(info != nullptr);
    assert(info->kind == HostServiceHandleKind::Surface);
    assert(info->app_instance_id == 1);
    assert(info->bytes == 128);
    assert(info->payload == reinterpret_cast<void*>(0x10));

    assert(handles.release(first));
    assert(!handles.release(first));
    assert(handles.lookup(first) == nullptr);

    const std::uint32_t second = handles.allocate(HostServiceHandleKind::AudioStream, 1, 64);
    assert(second != 0);
    assert(second != first);
    assert(handles.lookup(first) == nullptr);
    assert(handles.lookup(second) != nullptr);
}

void handle_table_enforces_capacity_and_bytes() {
    HostHandleTable handles(2, 200);
    const std::uint32_t first = handles.allocate(HostServiceHandleKind::Surface, 1, 128);
    assert(first != 0);
    assert(handles.allocate(HostServiceHandleKind::FetchResponse, 1, 100) == 0);

    const std::uint32_t second = handles.allocate(HostServiceHandleKind::FetchResponse, 2, 72);
    assert(second != 0);
    assert(handles.allocate(HostServiceHandleKind::BundleRecord, 3, 1) == 0);
    assert(handles.active_count() == 2);
    assert(handles.release_app_instance(1) == 1);
    assert(handles.active_count() == 1);
    assert(handles.used_bytes() == 72);

    handles.clear();
    assert(handles.active_count() == 0);
    assert(handles.used_bytes() == 0);
    assert(handles.lookup(second) == nullptr);
}

void queue_helpers_use_capability_budgets() {
    HostAsyncCapabilities caps;
    caps.max_in_flight_jobs = 3;
    caps.max_completion_events_per_frame = 1;

    auto requests = host_service_request_queue_from_capabilities(caps);
    auto completions = host_service_completion_queue_from_capabilities(caps);

    assert(requests.capacity() == 3);
    assert(completions.capacity() == 3);
}

void cancelled_completion_preserves_request_identity() {
    HostServiceRequest request{42, HostServiceJobKind::BundleRemove, 77, 5, 1000, 3};
    const HostServiceCompletion completion = make_cancelled_completion(request);
    assert(completion.job_id == 42);
    assert(completion.kind == HostServiceJobKind::BundleRemove);
    assert(completion.status == HostServiceStatus::Cancelled);
    assert(completion.app_instance_id == 77);
    assert(completion.handle == 0);
}

} // namespace

int main() {
    request_queue_is_bounded_and_priority_ordered();
    request_queue_cancels_pending_jobs();
    request_queue_can_pop_by_kind_without_consuming_other_jobs();
    completion_queue_drains_with_frame_budget();
    completion_queue_rejects_overflow();
    handle_table_rejects_stale_handles();
    handle_table_enforces_capacity_and_bytes();
    queue_helpers_use_capability_budgets();
    cancelled_completion_preserves_request_identity();
    return 0;
}
