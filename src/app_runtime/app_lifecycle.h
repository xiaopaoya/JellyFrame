#pragma once

#include "app_runtime/host_services.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

enum class AppRole {
    App,
    Launcher,
    Watchface,
    Settings,
};

enum class AppLifecycleState {
    Empty,
    Foreground,
    Suspended,
};

struct AppInstance {
    std::uint32_t id = 0;
    std::string app_id;
    AppRole role = AppRole::App;
    AppLifecycleState state = AppLifecycleState::Empty;

    bool active() const {
        return id != 0 && state != AppLifecycleState::Empty;
    }
};

struct AppTeardownResult {
    std::uint32_t app_instance_id = 0;
    std::size_t cancelled_requests = 0;
    std::size_t discarded_completions = 0;
    std::size_t released_handles = 0;
    std::size_t released_font_resources = 0;
    bool crashed = false;
};

struct AppCompletionPumpResult {
    std::size_t consumed = 0;
    std::size_t accepted = 0;
    std::size_t stale = 0;
    std::size_t released_stale_handles = 0;
};

class AppLifecycleController {
public:
    const AppInstance& current() const {
        return current_;
    }

    std::uint32_t current_app_instance_id() const {
        return current_.id;
    }

    AppInstance launch(std::string app_id,
                       AppRole role,
                       HostServiceRequestQueue* requests = nullptr,
                       HostServiceCompletionQueue* completions = nullptr,
                       HostHandleTable* handles = nullptr);

    AppTeardownResult exit_current(HostServiceRequestQueue* requests = nullptr,
                                   HostServiceCompletionQueue* completions = nullptr,
                                   HostHandleTable* handles = nullptr);

    bool suspend_current();
    bool resume_current();
    bool accepts_completion(const HostServiceCompletion& completion) const;

    AppCompletionPumpResult pump_completions(HostServiceCompletionQueue& completions,
                                             std::size_t max_count,
                                             std::vector<HostServiceCompletion>& accepted,
                                             HostHandleTable* handles = nullptr) const;
    AppCompletionPumpResult pump_completions(HostServiceCompletionQueue& completions,
                                             std::size_t max_count,
                                             std::vector<HostServiceCompletion>& accepted,
                                             std::vector<HostServiceCompletion>& batch,
                                             HostHandleTable* handles = nullptr) const;

private:
    static std::uint32_t next_nonzero_instance_id(std::uint32_t value);

    AppInstance current_;
    std::uint32_t next_instance_id_ = 1;
};

const char* app_role_name(AppRole role);
const char* app_lifecycle_state_name(AppLifecycleState state);

} // namespace jellyframe
