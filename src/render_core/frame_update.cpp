#include "render_core/frame_update.h"

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

bool framebuffer_matches_size(const FrameUpdateState& state, int target_height) {
    return state.has_framebuffer &&
        state.framebuffer_width == state.viewport.width &&
        state.framebuffer_height == target_height;
}

FrameUpdateReason cache_miss_reason(const FrameUpdateState& state) {
    if (!state.has_render_tree && !state.has_layout_tree && !state.has_layer_tree && !state.has_framebuffer) {
        return FrameUpdateReason::FirstPaint;
    }
    if (!has_pipeline_cache(state)) {
        return FrameUpdateReason::MissingPipelineCache;
    }
    if (!state.has_framebuffer) {
        return FrameUpdateReason::MissingFramebuffer;
    }
    if (!framebuffer_matches_target(state)) {
        return FrameUpdateReason::FramebufferSizeMismatch;
    }
    return FrameUpdateReason::FirstPaint;
}

} // namespace

int frame_update_target_height(const FrameUpdateState& state) {
    return std::max(state.viewport.height, state.content_height);
}

const char* frame_update_action_name(FrameUpdateAction action) {
    switch (action) {
    case FrameUpdateAction::None:
        return "none";
    case FrameUpdateAction::RepaintExisting:
        return "repaint-existing";
    case FrameUpdateAction::RebuildPipeline:
        return "rebuild-pipeline";
    }
    return "unknown";
}

const char* frame_dirty_rect_mode_name(FrameDirtyRectMode mode) {
    switch (mode) {
    case FrameDirtyRectMode::None:
        return "none";
    case FrameDirtyRectMode::CurrentLayout:
        return "current-layout";
    case FrameDirtyRectMode::PreviousAndCurrentLayout:
        return "previous-and-current-layout";
    case FrameDirtyRectMode::FullFrame:
        return "full-frame";
    }
    return "unknown";
}

const char* frame_update_reason_name(FrameUpdateReason reason) {
    switch (reason) {
    case FrameUpdateReason::None:
        return "none";
    case FrameUpdateReason::CleanCached:
        return "clean-cached";
    case FrameUpdateReason::InvalidViewport:
        return "invalid-viewport";
    case FrameUpdateReason::FirstPaint:
        return "first-paint";
    case FrameUpdateReason::PaintOnlyDirty:
        return "paint-only-dirty";
    case FrameUpdateReason::TextDirtyStableLayout:
        return "text-dirty-stable-layout";
    case FrameUpdateReason::StyleDirtyStableLayout:
        return "style-dirty-stable-layout";
    case FrameUpdateReason::LayoutDirtyWithPreviousLayout:
        return "layout-dirty-with-previous-layout";
    case FrameUpdateReason::TreeDirty:
        return "tree-dirty";
    case FrameUpdateReason::MissingPipelineCache:
        return "missing-pipeline-cache";
    case FrameUpdateReason::MissingFramebuffer:
        return "missing-framebuffer";
    case FrameUpdateReason::FramebufferSizeMismatch:
        return "framebuffer-size-mismatch";
    case FrameUpdateReason::ResolvedFramebufferSizeMismatch:
        return "resolved-framebuffer-size-mismatch";
    }
    return "unknown";
}

std::size_t frame_update_reason_index(FrameUpdateReason reason) {
    switch (reason) {
    case FrameUpdateReason::None:
        return 0;
    case FrameUpdateReason::CleanCached:
        return 1;
    case FrameUpdateReason::InvalidViewport:
        return 2;
    case FrameUpdateReason::FirstPaint:
        return 3;
    case FrameUpdateReason::PaintOnlyDirty:
        return 4;
    case FrameUpdateReason::TextDirtyStableLayout:
        return 5;
    case FrameUpdateReason::StyleDirtyStableLayout:
        return 6;
    case FrameUpdateReason::LayoutDirtyWithPreviousLayout:
        return 7;
    case FrameUpdateReason::TreeDirty:
        return 8;
    case FrameUpdateReason::MissingPipelineCache:
        return 9;
    case FrameUpdateReason::MissingFramebuffer:
        return 10;
    case FrameUpdateReason::FramebufferSizeMismatch:
        return 11;
    case FrameUpdateReason::ResolvedFramebufferSizeMismatch:
        return 12;
    }
    return 0;
}

void record_frame_update_plan(FrameUpdateStatistics& statistics, const FrameUpdatePlan& plan) {
    switch (plan.action) {
    case FrameUpdateAction::None:
        ++statistics.idle_frames;
        break;
    case FrameUpdateAction::RepaintExisting:
        ++statistics.repaint_existing_frames;
        break;
    case FrameUpdateAction::RebuildPipeline:
        ++statistics.rebuild_pipeline_frames;
        break;
    }
    ++statistics.reasons[frame_update_reason_index(plan.reason)];
}

void record_frame_update_plan(FrameUpdateStatistics& statistics,
                              const FrameUpdatePlan& plan,
                              DomDirtyFlags dirty_flags) {
    record_frame_update_plan(statistics, plan);
    record_frame_dirty_flags(statistics, dirty_flags);
}

void record_frame_dirty_flags(FrameUpdateStatistics& statistics, DomDirtyFlags dirty_flags) {
    if (dirty_flags == DomDirtyNone) {
        ++statistics.dirty_none_frames;
        return;
    }
    if ((dirty_flags & DomDirtyTree) != 0U) {
        ++statistics.dirty_tree_frames;
    }
    if ((dirty_flags & DomDirtyAttributes) != 0U) {
        ++statistics.dirty_attribute_frames;
    }
    if ((dirty_flags & DomDirtyText) != 0U) {
        ++statistics.dirty_text_frames;
    }
    if ((dirty_flags & DomDirtyStyle) != 0U) {
        ++statistics.dirty_style_frames;
    }
    if ((dirty_flags & DomDirtyLayout) != 0U) {
        ++statistics.dirty_layout_frames;
    }
    if ((dirty_flags & DomDirtyPaint) != 0U) {
        ++statistics.dirty_paint_frames;
    }
    if (dirty_requires_render_or_layout(dirty_flags)) {
        ++statistics.dirty_render_or_layout_frames;
    }
}

std::size_t frame_update_reason_count(const FrameUpdateStatistics& statistics,
                                      FrameUpdateReason reason) {
    return statistics.reasons[frame_update_reason_index(reason)];
}

void record_frame_repaint_result(FrameRepaintStatistics& statistics,
                                 const FrameRepaintPlan& plan,
                                 bool used_dirty_rects) {
    const std::size_t reason_index = frame_update_reason_index(plan.reason);
    if (used_dirty_rects) {
        ++statistics.dirty_rect_frames;
        ++statistics.dirty_rect_reasons[reason_index];
    } else {
        ++statistics.full_frame_frames;
        ++statistics.full_frame_reasons[reason_index];
    }
}

std::size_t frame_repaint_dirty_rect_reason_count(const FrameRepaintStatistics& statistics,
                                                  FrameUpdateReason reason) {
    return statistics.dirty_rect_reasons[frame_update_reason_index(reason)];
}

std::size_t frame_repaint_full_frame_reason_count(const FrameRepaintStatistics& statistics,
                                                  FrameUpdateReason reason) {
    return statistics.full_frame_reasons[frame_update_reason_index(reason)];
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
        plan.reason = FrameUpdateReason::InvalidViewport;
        plan.needs_full_framebuffer = true;
        return plan;
    }

    const bool cache_ready = has_pipeline_cache(state);
    const bool framebuffer_ready = framebuffer_matches_target(state);
    if (state.dirty_flags == DomDirtyNone && cache_ready && framebuffer_ready) {
        plan.reason = FrameUpdateReason::CleanCached;
        return plan;
    }
    if (state.dirty_flags == DomDirtyNone) {
        plan.action = FrameUpdateAction::RebuildPipeline;
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.reason = cache_miss_reason(state);
        plan.needs_full_framebuffer = true;
        return plan;
    }

    const bool paint_only = !dirty_requires_render_or_layout(state.dirty_flags);

    if (paint_only && cache_ready && framebuffer_ready) {
        plan.action = FrameUpdateAction::RepaintExisting;
        plan.dirty_rect_mode = FrameDirtyRectMode::CurrentLayout;
        plan.reason = FrameUpdateReason::PaintOnlyDirty;
        plan.can_reuse_render_and_layout = true;
        return plan;
    }

    plan.action = FrameUpdateAction::RebuildPipeline;
    if ((state.dirty_flags & DomDirtyTree) != 0U) {
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.reason = FrameUpdateReason::TreeDirty;
        plan.needs_full_framebuffer = true;
        return plan;
    }
    if (cache_ready && framebuffer_ready) {
        plan.dirty_rect_mode = FrameDirtyRectMode::PreviousAndCurrentLayout;
        plan.reason = FrameUpdateReason::LayoutDirtyWithPreviousLayout;
        plan.needs_previous_layout = true;
    } else {
        plan.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
        plan.reason = cache_miss_reason(state);
        plan.needs_full_framebuffer = true;
    }
    return plan;
}

FrameRepaintPlan plan_frame_repaint(const FrameUpdateState& state,
                                    const FrameUpdatePlan& update_plan,
                                    int resolved_content_height) {
    FrameRepaintPlan repaint;
    if (!valid_viewport(state.viewport)) {
        repaint.reason = FrameUpdateReason::InvalidViewport;
        repaint.needs_full_framebuffer = true;
        return repaint;
    }

    repaint.target_width = state.viewport.width;
    repaint.target_height = std::max(state.viewport.height, resolved_content_height);

    if (update_plan.action == FrameUpdateAction::None) {
        repaint.reason = update_plan.reason;
        return repaint;
    }

    const bool framebuffer_ready = framebuffer_matches_size(state, repaint.target_height);
    const bool dirty_rect_mode =
        update_plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout ||
        update_plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout;

    if (dirty_rect_mode && framebuffer_ready) {
        repaint.dirty_rect_mode = update_plan.dirty_rect_mode;
        repaint.reason = update_plan.reason;
        repaint.can_repaint_dirty_rects = true;
        return repaint;
    }

    repaint.dirty_rect_mode = FrameDirtyRectMode::FullFrame;
    repaint.reason = dirty_rect_mode
        ? FrameUpdateReason::ResolvedFramebufferSizeMismatch
        : update_plan.reason;
    repaint.needs_full_framebuffer = true;
    return repaint;
}

} // namespace jellyframe
