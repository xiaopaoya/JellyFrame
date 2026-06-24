#include "app_runtime/app_load_telemetry.h"

#include <cassert>
#include <string>

using namespace jellyframe;

namespace {

AppFramePolicy visible_policy() {
    return AppFramePolicy{true, true, true, true, false};
}

AppServiceActivityPolicy idle_services() {
    return AppServiceActivityPolicy{};
}

FrameUpdatePlan clean_update() {
    FrameUpdatePlan plan;
    plan.reason = FrameUpdateReason::CleanCached;
    return plan;
}

void screen_off_idle_is_sleep_ok() {
    AppLoadTelemetryInput input;
    input.frame_policy = AppFramePolicy{};
    input.service_policy = idle_services();
    input.update = clean_update();

    const AppLoadTelemetry telemetry = analyze_app_load(input);
    assert(telemetry.level == AppLoadLevel::SleepOk);
    assert(telemetry.sleep_ok);
    assert(telemetry.low_frequency_ok);
    assert(!telemetry.boost_recommended);
    assert(std::string(app_load_level_name(telemetry.level)) == "sleep-ok");
}

void low_power_visible_idle_can_run_slow() {
    AppLoadTelemetryInput input;
    input.frame_policy = AppFramePolicy{true, true, false, true, false};
    input.service_policy = idle_services();
    input.update = clean_update();

    const AppLoadTelemetry telemetry = analyze_app_load(input);
    assert(telemetry.level == AppLoadLevel::LowFrequencyOk);
    assert(!telemetry.sleep_ok);
    assert(telemetry.low_frequency_ok);
}

void full_frame_rebuild_recommends_boost() {
    DirtyRegionResult region;
    region.mode = DirtyRegionMode::FullFrame;
    region.rects.push_back(Rect{0, 0, 100, 100});

    AppLoadTelemetryInput input;
    input.frame_policy = visible_policy();
    input.service_policy = idle_services();
    input.viewport = Rect{0, 0, 100, 100};
    input.dirty_region = &region;
    input.update.action = FrameUpdateAction::RebuildPipeline;
    input.update.reason = FrameUpdateReason::TreeDirty;

    const AppLoadTelemetry telemetry = analyze_app_load(input);
    assert(telemetry.level == AppLoadLevel::BoostNeeded);
    assert(telemetry.boost_recommended);
    assert(telemetry.dirty_area_percent == 100);
    assert(!telemetry.drop_animation_frame_recommended);
}

void backlog_and_full_frame_animation_is_overloaded() {
    DirtyRegionResult region;
    region.mode = DirtyRegionMode::FullFrame;
    region.rects.push_back(Rect{0, 0, 100, 100});

    AppLoadTelemetryInput input;
    input.frame_policy = visible_policy();
    input.service_policy = idle_services();
    input.viewport = Rect{0, 0, 100, 100};
    input.dirty_region = &region;
    input.update.action = FrameUpdateAction::RebuildPipeline;
    input.work.animation_callbacks_to_pump = 4;
    input.work.has_more_animation_callbacks = true;
    input.active_animations = 3;

    const AppLoadTelemetry telemetry = analyze_app_load(input);
    assert(telemetry.level == AppLoadLevel::Overloaded);
    assert(telemetry.boost_recommended);
    assert(telemetry.drop_animation_frame_recommended);
    assert(telemetry.has_callback_backlog);
    assert(telemetry.is_animating);
}

void service_queue_pressure_recommends_boost_without_frame_work() {
    AppLoadTelemetryInput input;
    input.frame_policy = visible_policy();
    input.service_policy = AppServiceActivityPolicy{true, false, false, false, false};
    input.update = clean_update();
    input.pending_service_requests = 3;
    input.service_request_capacity = 4;

    const AppLoadTelemetry telemetry = analyze_app_load(input);
    assert(telemetry.level == AppLoadLevel::BoostNeeded);
    assert(telemetry.has_service_backlog);
    assert(telemetry.has_active_services);
    assert(!telemetry.drop_animation_frame_recommended);
}

} // namespace

int main() {
    screen_off_idle_is_sleep_ok();
    low_power_visible_idle_can_run_slow();
    full_frame_rebuild_recommends_boost();
    backlog_and_full_frame_animation_is_overloaded();
    service_queue_pressure_recommends_boost_without_frame_work();
    return 0;
}
