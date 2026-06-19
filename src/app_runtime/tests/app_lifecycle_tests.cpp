#include "app_runtime/app_lifecycle.h"

#include <cassert>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void launch_assigns_foreground_instances() {
    AppLifecycleController lifecycle;
    const AppInstance launcher = lifecycle.launch("org.example.launcher", AppRole::Launcher);
    assert(launcher.id == 1);
    assert(launcher.role == AppRole::Launcher);
    assert(launcher.state == AppLifecycleState::Foreground);
    assert(lifecycle.current_app_instance_id() == 1);

    const AppInstance app = lifecycle.launch("org.example.app", AppRole::App);
    assert(app.id == 2);
    assert(app.app_id == "org.example.app");
    assert(app.role == AppRole::App);
    assert(app_lifecycle_state_name(app.state) == std::string("foreground"));
}

void launch_tears_down_previous_instance_resources() {
    AppLifecycleController lifecycle;
    HostServiceRequestQueue requests(4);
    HostServiceCompletionQueue completions(4);
    HostHandleTable handles(4, 4096);

    const AppInstance first = lifecycle.launch("org.example.first", AppRole::App);
    const auto pending = requests.submit(HostServiceJobKind::NetworkFetch, first.id);
    assert(pending.accepted);
    assert(completions.push(HostServiceCompletion{pending.job_id,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  first.id}));
    const std::uint32_t handle = handles.allocate(HostServiceHandleKind::FetchResponse, first.id, 128);
    assert(handle != 0);

    const AppInstance second =
        lifecycle.launch("org.example.second", AppRole::App, &requests, &completions, &handles);
    assert(second.id == first.id + 1);
    assert(requests.empty());
    assert(completions.empty());
    assert(handles.active_count() == 0);
    assert(handles.lookup(handle) == nullptr);
}

void exit_current_cleans_only_active_instance() {
    AppLifecycleController lifecycle;
    HostServiceRequestQueue requests(4);
    HostServiceCompletionQueue completions(4);
    HostHandleTable handles(4, 4096);

    const AppInstance active = lifecycle.launch("org.example.active", AppRole::App);
    const auto active_request = requests.submit(HostServiceJobKind::NetworkFetch, active.id);
    const auto other_request = requests.submit(HostServiceJobKind::NetworkFetch, active.id + 99);
    assert(active_request.accepted);
    assert(other_request.accepted);
    assert(completions.push(HostServiceCompletion{active_request.job_id,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  active.id}));
    assert(completions.push(HostServiceCompletion{other_request.job_id,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  active.id + 99}));
    const std::uint32_t active_handle = handles.allocate(HostServiceHandleKind::Surface, active.id, 64);
    const std::uint32_t other_handle = handles.allocate(HostServiceHandleKind::Surface, active.id + 99, 64);
    assert(active_handle != 0);
    assert(other_handle != 0);

    const AppTeardownResult result = lifecycle.exit_current(&requests, &completions, &handles);
    assert(result.app_instance_id == active.id);
    assert(result.cancelled_requests == 1);
    assert(result.discarded_completions == 1);
    assert(result.released_handles == 1);
    assert(lifecycle.current_app_instance_id() == 0);
    assert(requests.size() == 1);
    assert(completions.size() == 1);
    assert(handles.lookup(active_handle) == nullptr);
    assert(handles.lookup(other_handle) != nullptr);
}

void completion_pump_accepts_current_and_releases_stale_handles() {
    AppLifecycleController lifecycle;
    HostServiceCompletionQueue completions(4);
    HostHandleTable handles(4, 4096);

    const AppInstance current = lifecycle.launch("org.example.current", AppRole::App);
    const std::uint32_t stale_handle = handles.allocate(HostServiceHandleKind::FetchResponse, current.id + 3, 256);
    assert(stale_handle != 0);
    assert(completions.push(HostServiceCompletion{1,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  current.id + 3,
                                                  stale_handle,
                                                  0,
                                                  256}));
    assert(completions.push(HostServiceCompletion{2,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  current.id,
                                                  0,
                                                  0,
                                                  16}));

    std::vector<HostServiceCompletion> accepted;
    const AppCompletionPumpResult result = lifecycle.pump_completions(completions, 4, accepted, &handles);
    assert(result.consumed == 2);
    assert(result.accepted == 1);
    assert(result.stale == 1);
    assert(result.released_stale_handles == 1);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 2);
    assert(handles.lookup(stale_handle) == nullptr);
}

void stale_completion_cannot_release_current_instance_handle() {
    AppLifecycleController lifecycle;
    HostServiceCompletionQueue completions(2);
    HostHandleTable handles(2, 4096);

    const AppInstance current = lifecycle.launch("org.example.current", AppRole::App);
    const std::uint32_t current_handle = handles.allocate(HostServiceHandleKind::FetchResponse, current.id, 128);
    assert(current_handle != 0);
    assert(completions.push(HostServiceCompletion{1,
                                                  HostServiceJobKind::NetworkFetch,
                                                  HostServiceStatus::Completed,
                                                  current.id + 1,
                                                  current_handle,
                                                  0,
                                                  128}));

    std::vector<HostServiceCompletion> accepted;
    const AppCompletionPumpResult result = lifecycle.pump_completions(completions, 2, accepted, &handles);
    assert(result.consumed == 1);
    assert(result.accepted == 0);
    assert(result.stale == 1);
    assert(result.released_stale_handles == 0);
    assert(handles.lookup(current_handle) != nullptr);
}

void suspend_resume_are_explicit_state_transitions() {
    AppLifecycleController lifecycle;
    assert(!lifecycle.suspend_current());

    lifecycle.launch("org.example.app", AppRole::App);
    assert(lifecycle.suspend_current());
    assert(lifecycle.current().state == AppLifecycleState::Suspended);
    assert(!lifecycle.suspend_current());
    assert(lifecycle.resume_current());
    assert(lifecycle.current().state == AppLifecycleState::Foreground);
    assert(!lifecycle.resume_current());
}

} // namespace

int main() {
    launch_assigns_foreground_instances();
    launch_tears_down_previous_instance_resources();
    exit_current_cleans_only_active_instance();
    completion_pump_accepts_current_and_releases_stale_handles();
    stale_completion_cannot_release_current_instance_handle();
    suspend_resume_are_explicit_state_transitions();
    return 0;
}
