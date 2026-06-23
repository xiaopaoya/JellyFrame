#include "app_runtime/app_lifecycle.h"

namespace jellyframe {

std::uint32_t AppLifecycleController::next_nonzero_instance_id(std::uint32_t value) {
    ++value;
    return value == 0 ? 1 : value;
}

AppInstance AppLifecycleController::launch(std::string app_id,
                                           AppRole role,
                                           HostServiceRequestQueue* requests,
                                           HostServiceCompletionQueue* completions,
                                           HostHandleTable* handles) {
    terminate_current(AppTeardownReason::AppSwitch, requests, completions, handles);
    const std::uint32_t instance_id = next_instance_id_;
    next_instance_id_ = next_nonzero_instance_id(next_instance_id_);
    current_ = AppInstance{instance_id, std::move(app_id), role, AppLifecycleState::Foreground};
    return current_;
}

AppTeardownResult AppLifecycleController::exit_current(HostServiceRequestQueue* requests,
                                                       HostServiceCompletionQueue* completions,
                                                       HostHandleTable* handles) {
    return terminate_current(AppTeardownReason::NormalExit, requests, completions, handles);
}

AppTeardownResult AppLifecycleController::terminate_current(AppTeardownReason reason,
                                                            HostServiceRequestQueue* requests,
                                                            HostServiceCompletionQueue* completions,
                                                            HostHandleTable* handles) {
    AppTeardownResult result;
    result.reason = reason;
    if (!current_.active()) {
        current_ = {};
        return result;
    }

    result.app_instance_id = current_.id;
    result.crashed = app_teardown_reason_is_crash(reason);
    if (requests != nullptr) {
        result.cancelled_requests = requests->cancel_app_instance(current_.id);
    }
    if (completions != nullptr) {
        result.discarded_completions = completions->discard_app_instance(current_.id);
    }
    if (handles != nullptr) {
        result.released_handles = handles->release_app_instance(current_.id);
    }
    current_ = {};
    return result;
}

bool AppLifecycleController::suspend_current() {
    if (current_.state != AppLifecycleState::Foreground) {
        return false;
    }
    current_.state = AppLifecycleState::Suspended;
    return true;
}

bool AppLifecycleController::resume_current() {
    if (current_.state != AppLifecycleState::Suspended) {
        return false;
    }
    current_.state = AppLifecycleState::Foreground;
    return true;
}

bool AppLifecycleController::accepts_completion(const HostServiceCompletion& completion) const {
    return current_.active() && completion.app_instance_id == current_.id;
}

AppCompletionPumpResult AppLifecycleController::pump_completions(HostServiceCompletionQueue& completions,
                                                                 std::size_t max_count,
                                                                 std::vector<HostServiceCompletion>& accepted,
                                                                 HostHandleTable* handles) const {
    std::vector<HostServiceCompletion> batch;
    return pump_completions(completions, max_count, accepted, batch, handles);
}

AppCompletionPumpResult AppLifecycleController::pump_completions(HostServiceCompletionQueue& completions,
                                                                 std::size_t max_count,
                                                                 std::vector<HostServiceCompletion>& accepted,
                                                                 std::vector<HostServiceCompletion>& batch,
                                                                 HostHandleTable* handles) const {
    batch.clear();
    AppCompletionPumpResult result;
    result.consumed = completions.pop(max_count, batch);
    accepted.reserve(accepted.size() + batch.size());
    for (const HostServiceCompletion& completion : batch) {
        if (accepts_completion(completion)) {
            accepted.push_back(completion);
            ++result.accepted;
            continue;
        }
        ++result.stale;
        if (handles != nullptr && completion.handle != 0) {
            const HostHandleInfo* info = handles->lookup(completion.handle);
            if (info != nullptr && info->app_instance_id == completion.app_instance_id &&
                handles->release(completion.handle)) {
                ++result.released_stale_handles;
            }
        }
    }
    return result;
}

const char* app_role_name(AppRole role) {
    switch (role) {
    case AppRole::App:
        return "app";
    case AppRole::Launcher:
        return "launcher";
    case AppRole::Watchface:
        return "watchface";
    case AppRole::Settings:
        return "settings";
    }
    return "app";
}

const char* app_lifecycle_state_name(AppLifecycleState state) {
    switch (state) {
    case AppLifecycleState::Empty:
        return "empty";
    case AppLifecycleState::Foreground:
        return "foreground";
    case AppLifecycleState::Suspended:
        return "suspended";
    }
    return "empty";
}

const char* app_teardown_reason_name(AppTeardownReason reason) {
    switch (reason) {
    case AppTeardownReason::None:
        return "none";
    case AppTeardownReason::NormalExit:
        return "normal-exit";
    case AppTeardownReason::AppSwitch:
        return "app-switch";
    case AppTeardownReason::UserKill:
        return "user-kill";
    case AppTeardownReason::RuntimeError:
        return "runtime-error";
    case AppTeardownReason::ScriptWatchdog:
        return "script-watchdog";
    case AppTeardownReason::BudgetExceeded:
        return "budget-exceeded";
    case AppTeardownReason::LoadFailure:
        return "load-failure";
    case AppTeardownReason::SystemPolicy:
        return "system-policy";
    }
    return "none";
}

bool app_teardown_reason_is_crash(AppTeardownReason reason) {
    switch (reason) {
    case AppTeardownReason::RuntimeError:
    case AppTeardownReason::ScriptWatchdog:
    case AppTeardownReason::BudgetExceeded:
    case AppTeardownReason::LoadFailure:
        return true;
    case AppTeardownReason::None:
    case AppTeardownReason::NormalExit:
    case AppTeardownReason::AppSwitch:
    case AppTeardownReason::UserKill:
    case AppTeardownReason::SystemPolicy:
        return false;
    }
    return false;
}

} // namespace jellyframe
