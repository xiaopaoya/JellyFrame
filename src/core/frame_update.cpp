#include "core/frame_update.h"

#include <algorithm>

namespace jellyframe {
namespace {

bool valid_viewport(Rect viewport) {
    return viewport.width > 0 && viewport.height > 0;
}

bool has_pipeline_cache(const FrameUpdateState& state) {
    return state.has_render_tree && state.has_layout_tree && state.has_layer_tree;
}

bool framebuffer_matches_target(const FrameUpdateState& state) {
    return state.has_framebuffer &&
        state.framebuffer_width == state.viewport.width &&
        state.framebuffer_height == frame_update_target_height(state);
}

} // namespace

int frame_update_target_height(const FrameUpdateState& state) {
    return std::max(state.viewport.height, state.content_height);
}

FrameUpdateState make_frame_update_state(DomDirtyFlags dirty_flags,
                                         const FramePipelineCacheState& cache_state) {
    FrameUpdateState state;
    state.dirty_flags = dirty_flags;
    state.has_render_tree = cache_state.has_render_tree;
    state.has_layout_tree = cache_state.has_layout_tree;
    state.has_layer_tree = cache_state.has_layer_tree;
    state.has_framebuffer = cache_state.has_framebuffer;
    state.framebuffer_width = cache_state.framebuffer_width;
    state.framebuffer_height = cache_state.framebuffer_height;
    state.viewport = cache_state.viewport;
    state.content_height = cache_state.content_height;
    return state;
}

FrameUpdatePlan plan_frame_update(const FrameUpdateState& state) {
    FrameUpdatePlan plan;
    if (!valid_viewport(state.viewport) || frame_update_target_height(state) <= 0) {
        plan.action = FrameUpdateAction::RebuildPipeline;
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.needs_full_framebuffer = true;
        return plan;
    }

    const bool cache_ready = has_pipeline_cache(state);
    const bool framebuffer_ready = framebuffer_matches_target(state);
    if (state.dirty_flags == DomDirtyNone && cache_ready && framebuffer_ready) {
        return plan;
    }
    if (state.dirty_flags == DomDirtyNone) {
        plan.action = FrameUpdateAction::RebuildPipeline;
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.needs_full_framebuffer = true;
        return plan;
    }

    const bool paint_only = !dirty_requires_render_or_layout(state.dirty_flags);

    if (paint_only && cache_ready && framebuffer_ready) {
        plan.action = FrameUpdateAction::RepaintExisting;
        plan.dirty_rect_mode = FrameDirtyRectMode::CurrentLayout;
        plan.can_reuse_render_and_layout = true;
        return plan;
    }

    plan.action = FrameUpdateAction::RebuildPipeline;
    if (cache_ready && framebuffer_ready) {
        plan.dirty_rect_mode = FrameDirtyRectMode::PreviousAndCurrentLayout;
        plan.needs_previous_layout = true;
    } else {
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.needs_full_framebuffer = true;
    }
    return plan;
}

} // namespace jellyframe
