#include "app_runtime/app_host.h"

#include <algorithm>

namespace jellyframe {

AppRuntimeHost::AppRuntimeHost(const AppRuntimeHostOptions& options)
    : requests_(options.max_in_flight_jobs),
      completions_(std::max(options.max_in_flight_jobs, options.max_completion_events_per_frame)),
      handles_(options.max_host_handles, options.max_host_handle_bytes),
      max_completion_events_per_frame_(options.max_completion_events_per_frame) {}

AppRuntimeHostOptions AppRuntimeHost::options_from_capabilities(const HostDeviceCapabilities& capabilities,
                                                                std::size_t max_host_handles,
                                                                std::size_t max_host_handle_bytes) {
    return AppRuntimeHostOptions{
        capabilities.async.max_in_flight_jobs,
        capabilities.async.max_completion_events_per_frame,
        max_host_handles,
        max_host_handle_bytes,
    };
}

AppInstance AppRuntimeHost::launch(std::string app_id, AppRole role) {
    return lifecycle_.launch(std::move(app_id), role, &requests_, &completions_, &handles_);
}

AppTeardownResult AppRuntimeHost::exit_current() {
    return lifecycle_.exit_current(&requests_, &completions_, &handles_);
}

bool AppRuntimeHost::suspend_current() {
    return lifecycle_.suspend_current();
}

bool AppRuntimeHost::resume_current() {
    return lifecycle_.resume_current();
}

HostServiceSubmitResult AppRuntimeHost::submit_current(HostServiceJobKind kind,
                                                       std::uint32_t request_handle,
                                                       std::uint8_t priority,
                                                       std::uint32_t timeout_ms) {
    const std::uint32_t app_instance_id = lifecycle_.current_app_instance_id();
    if (app_instance_id == 0) {
        return HostServiceSubmitResult{false, 0, HostServiceStatus::Cancelled};
    }
    return requests_.submit(kind, app_instance_id, request_handle, priority, timeout_ms);
}

std::uint32_t AppRuntimeHost::allocate_current_handle(HostServiceHandleKind kind,
                                                     std::uint32_t bytes,
                                                     void* payload) {
    const std::uint32_t app_instance_id = lifecycle_.current_app_instance_id();
    if (app_instance_id == 0) {
        return 0;
    }
    return handles_.allocate(kind, app_instance_id, bytes, payload);
}

bool AppRuntimeHost::push_completion(const HostServiceCompletion& completion) {
    return completions_.push(completion);
}

bool AppRuntimeHost::pop_worker_request(HostServiceRequest& request) {
    return requests_.pop_next(request);
}

AppCompletionPumpResult AppRuntimeHost::pump_frame_completions(std::vector<HostServiceCompletion>& accepted) {
    return lifecycle_.pump_completions(completions_, max_completion_events_per_frame_, accepted, &handles_);
}

} // namespace jellyframe
