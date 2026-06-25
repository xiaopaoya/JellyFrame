#include "app_runtime/app_budget.h"

namespace jellyframe {

namespace {

AppBudgetMeter make_limit_only_meter(std::size_t limit) {
    return AppBudgetMeter{0, limit};
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
    return snapshot.service_requests.exhausted() ||
        snapshot.service_completions.exhausted() ||
        snapshot.host_handles.exhausted() ||
        snapshot.host_handle_bytes.exhausted() ||
        snapshot.app_fonts.exhausted() ||
        snapshot.system_events.exhausted() ||
        snapshot.input_events_per_frame.exhausted() ||
        snapshot.timer_callbacks_per_frame.exhausted() ||
        snapshot.animation_callbacks_per_frame.exhausted() ||
        snapshot.active_animations.exhausted() ||
        snapshot.script_timers.exhausted() ||
        snapshot.script_event_listeners.exhausted() ||
        snapshot.detached_dom_nodes.exhausted();
}

} // namespace jellyframe
