#include "render_core/frame_loop.h"

#include "render_core/frame_scratch.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

FramePipelineCacheState cached_frame() {
    FramePipelineCacheState cache;
    cache.has_render_tree = true;
    cache.has_layout_tree = true;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 240;
    cache.framebuffer_height = 320;
    cache.viewport = Rect{0, 0, 240, 200};
    cache.content_height = 320;
    return cache;
}

void frame_loop_work_respects_per_frame_limits() {
    FrameLoopPendingWork pending;
    pending.pending_input_events = 20;
    pending.pending_timer_callbacks = 13;
    pending.pending_animation_callbacks = 9;

    FrameLoopOptions options;
    options.max_input_events_per_frame = 6;
    options.max_timer_callbacks_per_frame = 4;
    options.max_animation_callbacks_per_frame = 2;

    const FrameLoopWorkPlan plan = plan_frame_loop_work(pending, options);
    check(plan.input_events_to_dispatch == 6, "input dispatch is capped");
    check(plan.timer_callbacks_to_pump == 4, "timer pump is capped");
    check(plan.animation_callbacks_to_pump == 2, "animation frame callbacks are capped");
    check(plan.has_more_input_events, "input backlog is reported");
    check(plan.has_more_timer_callbacks, "timer backlog is reported");
    check(plan.has_more_animation_callbacks, "animation backlog is reported");
    check(plan.needs_animation_frame, "animation backlog requests another frame");
}

void frame_loop_work_allows_zero_budget_frames() {
    FrameLoopPendingWork pending;
    pending.pending_input_events = 3;
    pending.pending_timer_callbacks = 2;
    pending.pending_animation_callbacks = 1;

    FrameLoopOptions options;
    options.max_input_events_per_frame = 0;
    options.max_timer_callbacks_per_frame = 0;
    options.max_animation_callbacks_per_frame = 0;

    const FrameLoopWorkPlan plan = plan_frame_loop_work(pending, options);
    check(plan.input_events_to_dispatch == 0, "zero input budget dispatches nothing");
    check(plan.timer_callbacks_to_pump == 0, "zero timer budget pumps nothing");
    check(plan.animation_callbacks_to_pump == 0, "zero animation budget pumps nothing");
    check(plan.has_more_input_events, "zero input budget preserves backlog");
    check(plan.has_more_timer_callbacks, "zero timer budget preserves backlog");
    check(plan.has_more_animation_callbacks, "zero animation budget preserves backlog");
    check(!plan.needs_animation_frame, "zero frame-rate suppresses animation frame scheduling");
}

void frame_loop_combines_work_budget_and_update_plan() {
    FrameLoopPendingWork pending;
    pending.pending_input_events = 2;
    pending.pending_timer_callbacks = 1;

    FrameLoopOptions options;
    options.max_input_events_per_frame = 4;
    options.max_timer_callbacks_per_frame = 4;
    options.max_animation_callbacks_per_frame = 4;

    const FrameLoopPlan plan = plan_frame_loop(pending, DomDirtyPaint, cached_frame(), options);
    check(plan.work.input_events_to_dispatch == 2, "frame loop dispatches available input");
    check(plan.work.timer_callbacks_to_pump == 1, "frame loop pumps available timers");
    check(!plan.work.has_more_input_events, "no input backlog remains");
    check(!plan.work.has_more_timer_callbacks, "no timer backlog remains");
    check(plan.update.action == FrameUpdateAction::RepaintExisting,
          "paint dirty frame reuses existing pipeline");
    check(plan.update.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout,
          "paint dirty frame uses current-layout dirty rects");
}

void animation_frame_request_is_idle_when_absent() {
    FrameLoopPendingWork pending;
    FrameLoopOptions options;
    const FrameLoopWorkPlan clean_plan = plan_frame_loop_work(pending, options);
    check(clean_plan.animation_callbacks_to_pump == 0, "no animation callbacks are pumped when absent");
    check(!clean_plan.needs_animation_frame, "static frame loop does not request animation frames");

    pending.pending_animation_callbacks = 1;
    const FrameLoopWorkPlan animated_plan = plan_frame_loop_work(pending, options);
    check(animated_plan.animation_callbacks_to_pump == 1, "one animation callback is pumped");
    check(animated_plan.needs_animation_frame, "active animation requests frame scheduling");
}

void frame_scratch_reuses_and_releases_frame_storage() {
    HostBudgets budgets;
    budgets.max_dirty_rects = 3;
    budgets.max_active_animations = 2;

    FrameScratch scratch;
    scratch.reserve_from_budgets(budgets);
    check(scratch.dirty_region.rects.capacity() >= 3, "dirty rect capacity follows budget");
    check(scratch.dirty_region_scratch.node_bounds.capacity() >= 6, "dirty bounds capacity follows budget");
    check(scratch.style_overrides.capacity() >= 2, "style override capacity follows budget");

    scratch.dirty_region.mode = DirtyRegionMode::DirtyRects;
    scratch.dirty_region.rects.push_back(Rect{0, 0, 10, 10});
    scratch.dirty_region_scratch.node_bounds.push_back(DirtyNodeBounds{nullptr, Rect{0, 0, 4, 4}});
    scratch.style_overrides.push_back(StyleOverride{});
    scratch.begin_frame();
    check(scratch.dirty_region.mode == DirtyRegionMode::Clean, "begin frame resets dirty result mode");
    check(scratch.dirty_region.rects.empty(), "begin frame clears dirty rects");
    check(scratch.dirty_region_scratch.node_bounds.empty(), "begin frame clears dirty bounds scratch");
    check(scratch.style_overrides.empty(), "begin frame clears style overrides");
    check(scratch.dirty_region.rects.capacity() >= 3, "begin frame keeps dirty rect capacity");

    scratch.release();
    check(scratch.dirty_region.rects.capacity() == 0, "release drops dirty rect storage");
    check(scratch.dirty_region_scratch.node_bounds.capacity() == 0, "release drops dirty bounds storage");
    check(scratch.style_overrides.capacity() == 0, "release drops animation override storage");
}

void long_running_frame_loop_keeps_backlog_and_dirty_work_bounded() {
    FrameLoopPendingWork pending;
    pending.pending_input_events = 125;
    pending.pending_timer_callbacks = 77;

    FrameLoopOptions options;
    options.max_input_events_per_frame = 5;
    options.max_timer_callbacks_per_frame = 3;

    FramePipelineCacheState cache = cached_frame();
    std::size_t dispatched_input = 0;
    std::size_t pumped_timers = 0;
    std::size_t idle_frames = 0;
    std::size_t paint_reuse_frames = 0;
    std::size_t layout_rebuild_frames = 0;

    for (std::size_t frame = 0; frame < 64; ++frame) {
        DomDirtyFlags dirty = DomDirtyNone;
        if (frame % 11 == 0) {
            dirty = DomDirtyText | DomDirtyLayout;
        } else if (pending.pending_input_events != 0 || pending.pending_timer_callbacks != 0) {
            dirty = DomDirtyPaint;
        }

        const FrameLoopPlan plan = plan_frame_loop(pending, dirty, cache, options);
        check(plan.work.input_events_to_dispatch <= options.max_input_events_per_frame,
              "input budget stays bounded during long loop");
        check(plan.work.timer_callbacks_to_pump <= options.max_timer_callbacks_per_frame,
              "timer budget stays bounded during long loop");

        dispatched_input += plan.work.input_events_to_dispatch;
        pumped_timers += plan.work.timer_callbacks_to_pump;
        pending.pending_input_events -= plan.work.input_events_to_dispatch;
        pending.pending_timer_callbacks -= plan.work.timer_callbacks_to_pump;

        if (plan.update.action == FrameUpdateAction::None) {
            ++idle_frames;
        } else if (plan.update.action == FrameUpdateAction::RepaintExisting) {
            ++paint_reuse_frames;
            check(plan.update.can_reuse_render_and_layout,
                  "paint reuse frame keeps render/layout cache");
        } else if (plan.update.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout) {
            ++layout_rebuild_frames;
            const FrameRepaintPlan repaint = plan_frame_repaint(
                make_frame_update_state(dirty, cache), plan.update, cache.content_height);
            check(repaint.can_repaint_dirty_rects,
                  "stable layout-dirty frame can keep dirty-rect repaint");
        }
    }

    check(pending.pending_input_events == 0, "long loop drains input backlog");
    check(pending.pending_timer_callbacks == 0, "long loop drains timer backlog");
    check(dispatched_input == 125, "all input events accounted for");
    check(pumped_timers == 77, "all timer callbacks accounted for");
    check(paint_reuse_frames > 0, "long loop exercises paint-only reuse");
    check(layout_rebuild_frames > 0, "long loop exercises layout rebuild path");
    check(idle_frames > 0, "long loop eventually reaches clean cached idle frames");
}

} // namespace

int main() {
    try {
        frame_loop_work_respects_per_frame_limits();
        frame_loop_work_allows_zero_budget_frames();
        frame_loop_combines_work_budget_and_update_plan();
        animation_frame_request_is_idle_when_absent();
        frame_scratch_reuses_and_releases_frame_storage();
        long_running_frame_loop_keeps_backlog_and_dirty_work_bounded();
    } catch (const std::exception& error) {
        std::cerr << "frame loop test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "frame loop tests passed\n";
    return 0;
}
