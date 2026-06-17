#include "core/frame_loop.h"

#include <algorithm>

namespace jellyframe {

FrameLoopWorkPlan plan_frame_loop_work(const FrameLoopPendingWork& pending,
                                       const FrameLoopOptions& options) {
    FrameLoopWorkPlan plan;
    plan.input_events_to_dispatch =
        std::min(pending.pending_input_events, options.max_input_events_per_frame);
    plan.timer_callbacks_to_pump =
        std::min(pending.pending_timer_callbacks, options.max_timer_callbacks_per_frame);
    plan.has_more_input_events = pending.pending_input_events > plan.input_events_to_dispatch;
    plan.has_more_timer_callbacks = pending.pending_timer_callbacks > plan.timer_callbacks_to_pump;
    return plan;
}

FrameLoopPlan plan_frame_loop(const FrameLoopPendingWork& pending,
                              DomDirtyFlags dirty_flags,
                              const FramePipelineCacheState& cache_state,
                              const FrameLoopOptions& options) {
    FrameLoopPlan plan;
    plan.work = plan_frame_loop_work(pending, options);
    plan.update = plan_frame_update(make_frame_update_state(dirty_flags, cache_state));
    return plan;
}

} // namespace jellyframe
