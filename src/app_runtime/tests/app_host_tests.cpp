#include "app_runtime/app_host.h"

#include <cassert>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{
        4,
        2,
        4,
        4096,
    });
}

void current_instance_submission_and_handles_are_scoped() {
    AppRuntimeHost host = make_host();
    assert(!host.submit_current(HostServiceJobKind::NetworkFetch).accepted);
    assert(host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 16) == 0);

    const AppInstance app = host.launch("org.example.app", AppRole::App);
    const auto submitted = host.submit_current(HostServiceJobKind::NetworkFetch, 0, 3, 1000);
    assert(submitted.accepted);
    assert(host.requests().size() == 1);

    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(request.job_id == submitted.job_id);
    assert(request.app_instance_id == app.id);
    assert(request.priority == 3);
    assert(request.timeout_ms == 1000);

    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    assert(handle != 0);
    const HostHandleInfo* info = host.handles().lookup(handle);
    assert(info != nullptr);
    assert(info->app_instance_id == app.id);
}

void launch_cleans_previous_instance_state() {
    AppRuntimeHost host = make_host();
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    const auto submitted = host.submit_current(HostServiceJobKind::NetworkFetch);
    assert(submitted.accepted);
    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 32);
    assert(handle != 0);
    assert(host.push_completion(HostServiceCompletion{submitted.job_id,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      first.id}));

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(second.id == first.id + 1);
    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(host.handles().active_count() == 0);
}

void frame_pump_limits_completions_and_filters_stale_instances() {
    AppRuntimeHost host = make_host();
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    const std::uint32_t stale_handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 128);
    assert(stale_handle != 0);

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(host.push_completion(HostServiceCompletion{1,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      first.id,
                                                      stale_handle,
                                                      0,
                                                      128}));
    assert(host.push_completion(HostServiceCompletion{2,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      second.id,
                                                      0,
                                                      0,
                                                      16}));
    assert(host.push_completion(HostServiceCompletion{3,
                                                      HostServiceJobKind::ImageDecode,
                                                      HostServiceStatus::Completed,
                                                      second.id,
                                                      0,
                                                      0,
                                                      16}));

    std::vector<HostServiceCompletion> accepted;
    const AppCompletionPumpResult first_pump = host.pump_frame_completions(accepted);
    assert(first_pump.consumed == 2);
    assert(first_pump.accepted == 1);
    assert(first_pump.stale == 1);
    assert(first_pump.released_stale_handles == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 2);
    assert(host.completions().size() == 1);

    accepted.clear();
    const AppCompletionPumpResult second_pump = host.pump_frame_completions(accepted);
    assert(second_pump.consumed == 1);
    assert(second_pump.accepted == 1);
    assert(second_pump.stale == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 3);
}

void options_follow_host_capabilities() {
    HostDeviceCapabilities caps;
    caps.async.max_in_flight_jobs = 7;
    caps.async.max_completion_events_per_frame = 3;
    const AppRuntimeHostOptions options = AppRuntimeHost::options_from_capabilities(caps, 5, 2048);
    assert(options.max_in_flight_jobs == 7);
    assert(options.max_completion_events_per_frame == 3);
    assert(options.max_host_handles == 5);
    assert(options.max_host_handle_bytes == 2048);

    AppRuntimeHost host(options);
    assert(host.requests().capacity() == 7);
    assert(host.completions().capacity() == 7);
    assert(host.handles().capacity() == 5);
    assert(host.max_completion_events_per_frame() == 3);
}

} // namespace

int main() {
    current_instance_submission_and_handles_are_scoped();
    launch_cleans_previous_instance_state();
    frame_pump_limits_completions_and_filters_stale_instances();
    options_follow_host_capabilities();
    return 0;
}
