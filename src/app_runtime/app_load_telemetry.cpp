#include "app_runtime/app_load_telemetry.h"

#include <algorithm>

namespace jellyframe {
namespace {

bool queue_near_full(std::size_t depth, std::size_t capacity) {
    return capacity != 0 && depth >= capacity - capacity / 4U;
}

bool has_ui_work(const FrameLoopWorkPlan& work) {
    return work.input_events_to_dispatch != 0 ||
        work.timer_callbacks_to_pump != 0 ||
        work.animation_callbacks_to_pump != 0;
}

bool has_work_backlog(const FrameLoopWorkPlan& work) {
    return work.has_more_input_events ||
        work.has_more_timer_callbacks ||
        work.has_more_animation_callbacks;
}

bool has_active_services(const AppServiceActivityPolicy& policy) {
    return policy.network_fetch ||
        policy.audio_playback ||
        policy.sensor_sampling ||
        policy.location_snapshots;
}

} // namespace

const char* app_load_level_name(AppLoadLevel level) {
    switch (level) {
    case AppLoadLevel::SleepOk:
        return "sleep-ok";
    case AppLoadLevel::LowFrequencyOk:
        return "low-frequency-ok";
    case AppLoadLevel::Normal:
        return "normal";
    case AppLoadLevel::BoostNeeded:
        return "boost-needed";
    case AppLoadLevel::Overloaded:
        return "overloaded";
    }
    return "unknown";
}

AppLoadTelemetry analyze_app_load(const AppLoadTelemetryInput& input) {
    AppLoadTelemetry telemetry;
    telemetry.service_queue_depth = input.pending_service_requests;
    telemetry.completion_queue_depth = input.pending_service_completions;
    telemetry.has_callback_backlog = has_work_backlog(input.work);
    telemetry.has_service_backlog =
        input.pending_service_requests != 0 || input.pending_service_completions != 0;
    telemetry.has_active_services = has_active_services(input.service_policy);
    telemetry.is_animating = input.active_animations != 0 ||
        input.work.animation_callbacks_to_pump != 0 ||
        input.work.needs_animation_frame;
    telemetry.present_blocked = input.present_pending;
    if (input.dirty_region != nullptr) {
        telemetry.dirty_area_percent = dirty_region_area_percent(*input.dirty_region, input.viewport);
    } else if (input.dirty_area_percent >= 0) {
        telemetry.dirty_area_percent = input.dirty_area_percent;
    }

    const bool has_update_work = input.update.action != FrameUpdateAction::None;
    const bool has_frame_work = has_ui_work(input.work) || has_update_work || telemetry.is_animating;
    const bool service_near_full =
        queue_near_full(input.pending_service_requests, input.service_request_capacity) ||
        queue_near_full(input.pending_service_completions, input.service_completion_capacity);
    const bool heavy_repaint =
        input.update.action == FrameUpdateAction::RebuildPipeline ||
        (input.dirty_region != nullptr && input.dirty_region->mode == DirtyRegionMode::FullFrame) ||
        (input.dirty_region == nullptr && input.dirty_region_mode == DirtyRegionMode::FullFrame) ||
        telemetry.dirty_area_percent >= 50;
    const bool saturated =
        input.present_pending ||
        service_near_full ||
        telemetry.has_callback_backlog ||
        (input.dirty_region != nullptr && telemetry.dirty_area_percent >= 90);

    if (!input.frame_policy.presents_frames &&
        !has_frame_work &&
        !telemetry.has_service_backlog &&
        !telemetry.has_active_services &&
        !input.present_pending) {
        telemetry.level = AppLoadLevel::SleepOk;
    } else if (!input.frame_policy.pumps_animation &&
               !has_update_work &&
               !telemetry.has_callback_backlog &&
               !telemetry.has_service_backlog &&
               !input.present_pending) {
        telemetry.level = AppLoadLevel::LowFrequencyOk;
    } else if (saturated && heavy_repaint) {
        telemetry.level = AppLoadLevel::Overloaded;
    } else if (saturated || heavy_repaint) {
        telemetry.level = AppLoadLevel::BoostNeeded;
    } else {
        telemetry.level = AppLoadLevel::Normal;
    }

    telemetry.sleep_ok = telemetry.level == AppLoadLevel::SleepOk;
    telemetry.low_frequency_ok =
        telemetry.level == AppLoadLevel::SleepOk ||
        telemetry.level == AppLoadLevel::LowFrequencyOk;
    telemetry.boost_recommended =
        telemetry.level == AppLoadLevel::BoostNeeded ||
        telemetry.level == AppLoadLevel::Overloaded;
    telemetry.drop_animation_frame_recommended =
        telemetry.level == AppLoadLevel::Overloaded &&
        (telemetry.is_animating || input.work.has_more_animation_callbacks || input.present_pending);
    return telemetry;
}

} // namespace jellyframe
