#pragma once

#include "app_runtime/app_host.h"
#include "app_runtime/system_events.h"
#include "render_core/frame_loop.h"
#include "render_core/host.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe {

constexpr std::size_t kAppBudgetUnknown = static_cast<std::size_t>(-1);

struct AppBudgetMeter {
    std::size_t used = 0;
    std::size_t limit = 0;

    bool has_limit() const {
        return limit != 0;
    }

    bool exhausted() const {
        return has_limit() && used >= limit;
    }
};

struct AppBudgetSnapshotInput {
    const HostBudgets* host_budgets = nullptr;
    const AppSystemEventQueue* system_events = nullptr;
    const FrameLoopPendingWork* pending_work = nullptr;
    std::size_t active_animations = kAppBudgetUnknown;
    std::size_t script_timers = kAppBudgetUnknown;
    std::size_t script_event_listeners = kAppBudgetUnknown;
    std::size_t detached_dom_nodes = kAppBudgetUnknown;
    std::size_t storage_shadow_items = kAppBudgetUnknown;
    std::size_t storage_shadow_bytes = kAppBudgetUnknown;
};

struct AppBudgetSnapshot {
    std::uint32_t app_instance_id = 0;
    AppRole role = AppRole::App;
    AppLifecycleState state = AppLifecycleState::Empty;

    AppBudgetMeter service_requests;
    AppBudgetMeter service_completions;
    AppBudgetMeter host_handles;
    AppBudgetMeter host_handle_bytes;
    AppBudgetMeter app_fonts;
    AppBudgetMeter system_events;

    AppBudgetMeter input_events_per_frame;
    AppBudgetMeter timer_callbacks_per_frame;
    AppBudgetMeter animation_callbacks_per_frame;
    AppBudgetMeter active_animations;
    AppBudgetMeter script_timers;
    AppBudgetMeter script_event_listeners;
    AppBudgetMeter detached_dom_nodes;
    AppBudgetMeter storage_shadow_items;
    AppBudgetMeter storage_shadow_bytes;

    AppBudgetMeter dom_nodes;
    AppBudgetMeter css_rules;
    AppBudgetMeter render_objects;
    AppBudgetMeter layout_boxes;
    AppBudgetMeter layers;
    AppBudgetMeter display_commands;
    AppBudgetMeter dirty_rects;
    AppBudgetMeter framebuffer_pixels;
    AppBudgetMeter resource_bytes;

    std::size_t script_execution_checks = 0;
    std::size_t script_execution_check_interval = 0;
};

enum class AppBudgetRecoveryAction {
    None,
    Warn,
    TerminateApp,
};

enum class AppBudgetRecoveryDiagnosticCode {
    ServiceRequests,
    ServiceCompletions,
    HostHandles,
    HostHandleBytes,
    AppFonts,
    SystemEvents,
    InputEventsPerFrame,
    TimerCallbacksPerFrame,
    AnimationCallbacksPerFrame,
    ActiveAnimations,
    ScriptTimers,
    ScriptEventListeners,
    DetachedDomNodes,
};

struct AppBudgetRecoveryDiagnostic {
    AppBudgetRecoveryDiagnosticCode code = AppBudgetRecoveryDiagnosticCode::ServiceRequests;
    std::size_t used = 0;
    std::size_t limit = 0;
};

struct AppBudgetRecoveryReport {
    static constexpr std::size_t kMaxDiagnostics = 8;

    AppBudgetRecoveryAction action = AppBudgetRecoveryAction::None;
    AppTeardownReason teardown_reason = AppTeardownReason::None;
    AppBudgetRecoveryDiagnostic diagnostics[kMaxDiagnostics];
    std::size_t diagnostic_count = 0;
};

AppBudgetSnapshot collect_app_budget_snapshot(const AppRuntimeHost& host,
                                              const AppBudgetSnapshotInput& input = {});

bool app_budget_snapshot_has_exhausted_runtime_budget(const AppBudgetSnapshot& snapshot);
AppBudgetRecoveryReport app_budget_recovery_for_snapshot(const AppBudgetSnapshot& snapshot);
const char* app_budget_recovery_action_name(AppBudgetRecoveryAction action);
const char* app_budget_recovery_diagnostic_code_name(AppBudgetRecoveryDiagnosticCode code);

} // namespace jellyframe
