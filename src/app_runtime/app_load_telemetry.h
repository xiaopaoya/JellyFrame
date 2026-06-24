#pragma once

#include "app_runtime/app_frame_policy.h"
#include "render_core/dirty_region.h"
#include "render_core/frame_loop.h"

#include <cstddef>

namespace jellyframe {

enum class AppLoadLevel {
    SleepOk,
    LowFrequencyOk,
    Normal,
    BoostNeeded,
    Overloaded,
};

struct AppLoadTelemetryInput {
    AppFramePolicy frame_policy;
    AppServiceActivityPolicy service_policy;
    FrameLoopWorkPlan work;
    FrameUpdatePlan update;
    const DirtyRegionResult* dirty_region = nullptr;
    Rect viewport;
    DirtyRegionMode dirty_region_mode = DirtyRegionMode::Clean;
    int dirty_area_percent = -1;
    std::size_t pending_service_requests = 0;
    std::size_t service_request_capacity = 0;
    std::size_t pending_service_completions = 0;
    std::size_t service_completion_capacity = 0;
    std::size_t active_animations = 0;
    bool present_pending = false;
};

struct AppLoadTelemetry {
    AppLoadLevel level = AppLoadLevel::SleepOk;
    int dirty_area_percent = 0;
    std::size_t service_queue_depth = 0;
    std::size_t completion_queue_depth = 0;
    bool has_callback_backlog = false;
    bool has_service_backlog = false;
    bool has_active_services = false;
    bool is_animating = false;
    bool present_blocked = false;
    bool sleep_ok = true;
    bool low_frequency_ok = true;
    bool boost_recommended = false;
    bool drop_animation_frame_recommended = false;
};

const char* app_load_level_name(AppLoadLevel level);
AppLoadTelemetry analyze_app_load(const AppLoadTelemetryInput& input);

} // namespace jellyframe
