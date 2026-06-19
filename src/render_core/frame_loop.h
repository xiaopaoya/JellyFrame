#pragma once

#include "render_core/frame_update.h"

#include <cstddef>

namespace jellyframe {

struct FrameLoopOptions {
    std::size_t max_input_events_per_frame = 8;
    std::size_t max_timer_callbacks_per_frame = 8;
};

struct FrameLoopPendingWork {
    std::size_t pending_input_events = 0;
    std::size_t pending_timer_callbacks = 0;
};

struct FrameLoopWorkPlan {
    std::size_t input_events_to_dispatch = 0;
    std::size_t timer_callbacks_to_pump = 0;
    bool has_more_input_events = false;
    bool has_more_timer_callbacks = false;
};

struct FrameLoopPlan {
    FrameLoopWorkPlan work;
    FrameUpdatePlan update;
};

FrameLoopWorkPlan plan_frame_loop_work(const FrameLoopPendingWork& pending,
                                       const FrameLoopOptions& options = {});

FrameLoopPlan plan_frame_loop(const FrameLoopPendingWork& pending,
                              DomDirtyFlags dirty_flags,
                              const FramePipelineCacheState& cache_state,
                              const FrameLoopOptions& options = {});

} // namespace jellyframe
