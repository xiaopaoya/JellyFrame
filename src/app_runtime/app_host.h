#pragma once

#include "app_runtime/app_font_set.h"
#include "app_runtime/app_lifecycle.h"
#include "app_runtime/host_services.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

struct AppRuntimeHostOptions {
    std::size_t max_in_flight_jobs = 0;
    std::size_t max_completion_events_per_frame = 0;
    std::size_t max_host_handles = 0;
    std::size_t max_host_handle_bytes = 0;
    std::size_t max_app_fonts = 0;
};

struct AppFrameScratch {
    std::vector<HostServiceCompletion> accepted_completions;
    // Raw entries drained from the host queue before lifecycle filtering.
    std::vector<HostServiceCompletion> drained_completions;

    void reserve_from_options(const AppRuntimeHostOptions& options) {
        if (options.max_completion_events_per_frame != 0) {
            accepted_completions.reserve(options.max_completion_events_per_frame);
            drained_completions.reserve(options.max_completion_events_per_frame);
        }
    }

    void begin_frame() {
        accepted_completions.clear();
        drained_completions.clear();
    }

    void end_frame() {
        accepted_completions.clear();
        drained_completions.clear();
    }

    void release() {
        std::vector<HostServiceCompletion>().swap(accepted_completions);
        std::vector<HostServiceCompletion>().swap(drained_completions);
    }
};

class AppRuntimeHost {
public:
    explicit AppRuntimeHost(const AppRuntimeHostOptions& options = {});

    static AppRuntimeHostOptions options_from_capabilities(const HostDeviceCapabilities& capabilities,
                                                           std::size_t max_host_handles = 0,
                                                           std::size_t max_host_handle_bytes = 0);

    const AppInstance& current() const {
        return lifecycle_.current();
    }

    std::uint32_t current_app_instance_id() const {
        return current_app_instance_id_.load(std::memory_order_acquire);
    }

    AppInstance launch(std::string app_id, AppRole role);
    AppTeardownResult exit_current();
    AppTeardownResult terminate_current(AppTeardownReason reason);
    AppTeardownResult crash_current();
    bool suspend_current();
    bool resume_current();

    HostServiceSubmitResult submit_current(HostServiceJobKind kind,
                                           std::uint32_t input_handle = 0,
                                           std::uint8_t priority = 0,
                                           std::uint32_t timeout_ms = 0,
                                           std::uint32_t client_token = 0);

    std::uint32_t allocate_current_handle(HostServiceHandleKind kind,
                                          std::uint32_t bytes = 0,
                                          void* payload = nullptr);
    AppFontLoadResult load_current_jffont(const std::uint8_t* data, std::size_t size);
    AppFontLoadResult load_current_jffont(const std::uint8_t* data, std::size_t size, std::string_view family);
    AppFontLoadResult attach_current_jffont_view(const std::uint8_t* data, std::size_t size);
    AppFontLoadResult attach_current_jffont_view(const std::uint8_t* data, std::size_t size, std::string_view family);
    std::size_t clear_current_fonts();

    bool push_completion(const HostServiceCompletion& completion);
    bool pop_worker_request(HostServiceRequest& request);
    bool pop_worker_request(HostServiceJobKind kind, HostServiceRequest& request);

    AppCompletionPumpResult pump_frame_completions(std::vector<HostServiceCompletion>& accepted);
    AppCompletionPumpResult pump_frame_completions(AppFrameScratch& scratch);

    HostServiceRequestQueue& requests() {
        return requests_;
    }

    HostServiceCompletionQueue& completions() {
        return completions_;
    }

    HostHandleTable& handles() {
        return handles_;
    }

    AppFontSet& fonts() {
        return fonts_;
    }

    const HostServiceRequestQueue& requests() const {
        return requests_;
    }

    const HostServiceCompletionQueue& completions() const {
        return completions_;
    }

    const HostHandleTable& handles() const {
        return handles_;
    }

    const AppFontSet& fonts() const {
        return fonts_;
    }

    std::size_t max_completion_events_per_frame() const {
        return max_completion_events_per_frame_;
    }

private:
    AppLifecycleController lifecycle_;
    std::atomic<std::uint32_t> current_app_instance_id_{0};
    HostServiceRequestQueue requests_;
    HostServiceCompletionQueue completions_;
    HostHandleTable handles_;
    AppFontSet fonts_;
    std::size_t max_completion_events_per_frame_ = 0;
};

} // namespace jellyframe
