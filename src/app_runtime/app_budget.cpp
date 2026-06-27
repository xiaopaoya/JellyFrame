#include "app_runtime/app_budget.h"

namespace jellyframe {

namespace {

AppBudgetMeter make_limit_only_meter(std::size_t limit) {
    return AppBudgetMeter{0, limit};
}

void append_recovery_diagnostic(AppBudgetRecoveryReport& report,
                                AppBudgetRecoveryDiagnosticCode code,
                                AppBudgetMeter meter,
                                AppBudgetRecoveryAction action) {
    if (!meter.exhausted()) {
        return;
    }
    if (action == AppBudgetRecoveryAction::TerminateApp) {
        report.action = AppBudgetRecoveryAction::TerminateApp;
        report.teardown_reason = AppTeardownReason::BudgetExceeded;
    } else if (report.action == AppBudgetRecoveryAction::None) {
        report.action = AppBudgetRecoveryAction::Warn;
    }
    if (report.diagnostic_count >= AppBudgetRecoveryReport::kMaxDiagnostics) {
        return;
    }
    report.diagnostics[report.diagnostic_count++] =
        AppBudgetRecoveryDiagnostic{code, meter.used, meter.limit};
}

} // namespace

AppBudgetSnapshot collect_app_budget_snapshot(const AppRuntimeHost& host,
                                              const AppBudgetSnapshotInput& input) {
    AppBudgetSnapshot snapshot;
    snapshot.app_instance_id = host.current_app_instance_id();
    snapshot.role = host.current().role;
    snapshot.state = host.current().state;

    snapshot.service_requests = AppBudgetMeter{host.requests().size(), host.requests().capacity()};
    snapshot.service_completions = AppBudgetMeter{host.completions().size(), host.completions().capacity()};
    snapshot.host_handles = AppBudgetMeter{host.handles().active_count(), host.handles().capacity()};
    snapshot.host_handle_bytes = AppBudgetMeter{host.handles().used_bytes(), host.handles().byte_budget()};
    snapshot.app_fonts = AppBudgetMeter{host.fonts().size(), host.fonts().capacity()};
    if (input.system_events != nullptr) {
        snapshot.system_events = AppBudgetMeter{input.system_events->size(), input.system_events->capacity()};
    }

    if (input.pending_work != nullptr) {
        snapshot.input_events_per_frame.used = input.pending_work->pending_input_events;
        snapshot.timer_callbacks_per_frame.used = input.pending_work->pending_timer_callbacks;
        snapshot.animation_callbacks_per_frame.used = input.pending_work->pending_animation_callbacks;
    }
    snapshot.active_animations.used =
        input.active_animations == kAppBudgetUnknown ? 0 : input.active_animations;
    snapshot.script_timers.used =
        input.script_timers == kAppBudgetUnknown ? 0 : input.script_timers;
    snapshot.script_event_listeners.used =
        input.script_event_listeners == kAppBudgetUnknown ? 0 : input.script_event_listeners;
    snapshot.detached_dom_nodes.used =
        input.detached_dom_nodes == kAppBudgetUnknown ? 0 : input.detached_dom_nodes;
    snapshot.storage_shadow_items.used =
        input.storage_shadow_items == kAppBudgetUnknown ? 0 : input.storage_shadow_items;
    snapshot.storage_shadow_bytes.used =
        input.storage_shadow_bytes == kAppBudgetUnknown ? 0 : input.storage_shadow_bytes;

    if (input.host_budgets != nullptr) {
        const HostBudgets& budgets = *input.host_budgets;
        snapshot.input_events_per_frame.limit = budgets.max_input_events_per_frame;
        snapshot.timer_callbacks_per_frame.limit = budgets.max_timer_callbacks_per_frame;
        snapshot.animation_callbacks_per_frame.limit = budgets.max_animation_callbacks_per_frame;
        snapshot.active_animations.limit = budgets.max_active_animations;
        snapshot.script_timers.limit = budgets.max_timers;
        snapshot.script_event_listeners.limit = budgets.max_event_listeners;
        snapshot.detached_dom_nodes.limit = budgets.max_detached_dom_nodes;

        snapshot.dom_nodes = make_limit_only_meter(budgets.max_dom_nodes);
        snapshot.css_rules = make_limit_only_meter(budgets.max_css_rules);
        snapshot.render_objects = make_limit_only_meter(budgets.max_render_objects);
        snapshot.layout_boxes = make_limit_only_meter(budgets.max_layout_boxes);
        snapshot.layers = make_limit_only_meter(budgets.max_layers);
        snapshot.display_commands = make_limit_only_meter(budgets.max_display_commands);
        snapshot.dirty_rects = make_limit_only_meter(budgets.max_dirty_rects);
        snapshot.framebuffer_pixels = make_limit_only_meter(budgets.max_framebuffer_pixels);
        snapshot.resource_bytes = make_limit_only_meter(budgets.max_resource_bytes);
        snapshot.script_execution_checks = budgets.max_script_execution_checks;
        snapshot.script_execution_check_interval = budgets.script_execution_check_interval;
    }
    return snapshot;
}

bool app_budget_snapshot_has_exhausted_runtime_budget(const AppBudgetSnapshot& snapshot) {
    return app_budget_recovery_for_snapshot(snapshot).action == AppBudgetRecoveryAction::TerminateApp;
}

AppBudgetRecoveryReport app_budget_recovery_for_snapshot(const AppBudgetSnapshot& snapshot) {
    AppBudgetRecoveryReport report;
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::ServiceRequests,
                               snapshot.service_requests,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::ServiceCompletions,
                               snapshot.service_completions,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::HostHandles,
                               snapshot.host_handles,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::HostHandleBytes,
                               snapshot.host_handle_bytes,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::AppFonts,
                               snapshot.app_fonts,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::SystemEvents,
                               snapshot.system_events,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::ScriptTimers,
                               snapshot.script_timers,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::ScriptEventListeners,
                               snapshot.script_event_listeners,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::DetachedDomNodes,
                               snapshot.detached_dom_nodes,
                               AppBudgetRecoveryAction::TerminateApp);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::InputEventsPerFrame,
                               snapshot.input_events_per_frame,
                               AppBudgetRecoveryAction::Warn);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::TimerCallbacksPerFrame,
                               snapshot.timer_callbacks_per_frame,
                               AppBudgetRecoveryAction::Warn);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::AnimationCallbacksPerFrame,
                               snapshot.animation_callbacks_per_frame,
                               AppBudgetRecoveryAction::Warn);
    append_recovery_diagnostic(report,
                               AppBudgetRecoveryDiagnosticCode::ActiveAnimations,
                               snapshot.active_animations,
                               AppBudgetRecoveryAction::Warn);
    return report;
}

const char* app_budget_recovery_action_name(AppBudgetRecoveryAction action) {
    switch (action) {
    case AppBudgetRecoveryAction::None:
        return "none";
    case AppBudgetRecoveryAction::Warn:
        return "warn";
    case AppBudgetRecoveryAction::TerminateApp:
        return "terminate-app";
    }
    return "unknown";
}

const char* app_budget_recovery_diagnostic_code_name(AppBudgetRecoveryDiagnosticCode code) {
    switch (code) {
    case AppBudgetRecoveryDiagnosticCode::ServiceRequests:
        return "budget-service-requests";
    case AppBudgetRecoveryDiagnosticCode::ServiceCompletions:
        return "budget-service-completions";
    case AppBudgetRecoveryDiagnosticCode::HostHandles:
        return "budget-host-handles";
    case AppBudgetRecoveryDiagnosticCode::HostHandleBytes:
        return "budget-host-handle-bytes";
    case AppBudgetRecoveryDiagnosticCode::AppFonts:
        return "budget-app-fonts";
    case AppBudgetRecoveryDiagnosticCode::SystemEvents:
        return "budget-system-events";
    case AppBudgetRecoveryDiagnosticCode::InputEventsPerFrame:
        return "budget-input-events-per-frame";
    case AppBudgetRecoveryDiagnosticCode::TimerCallbacksPerFrame:
        return "budget-timer-callbacks-per-frame";
    case AppBudgetRecoveryDiagnosticCode::AnimationCallbacksPerFrame:
        return "budget-animation-callbacks-per-frame";
    case AppBudgetRecoveryDiagnosticCode::ActiveAnimations:
        return "budget-active-animations";
    case AppBudgetRecoveryDiagnosticCode::ScriptTimers:
        return "budget-script-timers";
    case AppBudgetRecoveryDiagnosticCode::ScriptEventListeners:
        return "budget-script-event-listeners";
    case AppBudgetRecoveryDiagnosticCode::DetachedDomNodes:
        return "budget-detached-dom-nodes";
    }
    return "budget-unknown";
}

} // namespace jellyframe
