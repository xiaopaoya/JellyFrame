#include "core/frame_update.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

FrameUpdateState cached_state(DomDirtyFlags flags) {
    FramePipelineCacheState cache;
    cache.has_render_tree = true;
    cache.has_layout_tree = true;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 240;
    cache.framebuffer_height = 320;
    cache.viewport = Rect{0, 0, 240, 200};
    cache.content_height = 320;
    return make_frame_update_state(flags, cache);
}

void clean_document_has_no_work() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyNone));
    check(plan.action == FrameUpdateAction::None, "clean document has no action");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::None, "clean document has no dirty rects");
}

void clean_uncached_document_gets_first_paint() {
    FrameUpdateState state = cached_state(DomDirtyNone);
    state.has_render_tree = false;
    state.has_layout_tree = false;
    state.has_layer_tree = false;
    state.has_framebuffer = false;
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "clean uncached document gets first paint");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "first paint renders full frame");
    check(plan.needs_full_framebuffer, "first paint needs framebuffer");
}

void paint_dirty_reuses_existing_pipeline() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyPaint));
    check(plan.action == FrameUpdateAction::RepaintExisting, "paint dirty repaints existing frame");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout, "paint dirty uses current layout rects");
    check(plan.can_reuse_render_and_layout, "paint dirty reuses render/layout");
    check(!plan.needs_previous_layout, "paint dirty does not need old layout");
}

void layout_dirty_rebuilds_with_previous_layout() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyText | DomDirtyLayout));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "layout dirty rebuilds pipeline");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout,
          "layout dirty compares previous and current layout");
    check(plan.needs_previous_layout, "layout dirty keeps previous layout");
    check(!plan.needs_full_framebuffer, "matching framebuffer avoids full render requirement");
}

void repaint_plan_keeps_incremental_layout_when_size_matches() {
    const FrameUpdateState state = cached_state(DomDirtyText | DomDirtyLayout);
    const FrameUpdatePlan update_plan = plan_frame_update(state);
    const FrameRepaintPlan repaint_plan = plan_frame_repaint(state, update_plan, 320);
    check(repaint_plan.can_repaint_dirty_rects, "matching resolved layout keeps dirty rect repaint");
    check(repaint_plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout,
          "matching resolved layout compares previous and current layout");
    check(!repaint_plan.needs_full_framebuffer, "matching resolved layout avoids full framebuffer");
    check(repaint_plan.target_width == 240 && repaint_plan.target_height == 320,
          "repaint plan records target size");
}

void repaint_plan_forces_full_frame_when_resolved_layout_grows() {
    const FrameUpdateState state = cached_state(DomDirtyText | DomDirtyLayout);
    const FrameUpdatePlan update_plan = plan_frame_update(state);
    const FrameRepaintPlan repaint_plan = plan_frame_repaint(state, update_plan, 420);
    check(!repaint_plan.can_repaint_dirty_rects, "grown resolved layout cannot reuse framebuffer");
    check(repaint_plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "grown resolved layout falls back to full frame");
    check(repaint_plan.needs_full_framebuffer, "grown resolved layout needs full framebuffer");
    check(repaint_plan.target_height == 420, "repaint plan uses grown content height");
}

void repaint_plan_uses_viewport_floor_when_content_shrinks() {
    const FrameUpdateState state = cached_state(DomDirtyText | DomDirtyLayout);
    const FrameUpdatePlan update_plan = plan_frame_update(state);
    const FrameRepaintPlan repaint_plan = plan_frame_repaint(state, update_plan, 80);
    check(!repaint_plan.can_repaint_dirty_rects, "shrunk resolved layout changes framebuffer size");
    check(repaint_plan.needs_full_framebuffer, "shrunk resolved layout needs full framebuffer");
    check(repaint_plan.target_height == 200, "repaint target height is at least viewport height");
}

void repaint_plan_reuses_current_layout_for_paint_dirty() {
    const FrameUpdateState state = cached_state(DomDirtyPaint);
    const FrameUpdatePlan update_plan = plan_frame_update(state);
    const FrameRepaintPlan repaint_plan = plan_frame_repaint(state, update_plan, 320);
    check(repaint_plan.can_repaint_dirty_rects, "paint dirty can repaint current layout");
    check(repaint_plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout,
          "paint dirty keeps current layout mode");
    check(!repaint_plan.needs_full_framebuffer, "paint dirty avoids full framebuffer");
}

void tree_dirty_rebuilds_without_previous_layout() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyTree | DomDirtyLayout));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "tree dirty rebuilds pipeline");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "tree dirty falls back to full frame planning");
    check(!plan.needs_previous_layout, "tree dirty does not retain previous layout");
    check(plan.needs_full_framebuffer, "tree dirty needs full framebuffer repaint");
}

void missing_framebuffer_forces_full_frame() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.has_framebuffer = false;
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "missing framebuffer rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "missing framebuffer renders full frame");
    check(plan.needs_full_framebuffer, "missing framebuffer needs full render");
}

void resized_framebuffer_forces_full_frame() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.framebuffer_height = 200;
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "resized framebuffer rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "resized framebuffer renders full frame");
    check(plan.needs_full_framebuffer, "resized framebuffer needs full render");
}

void invalid_viewport_is_conservative() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.viewport = Rect{0, 0, 0, 0};
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "invalid viewport rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "invalid viewport is full-frame conservative");
}

void target_height_uses_content_height_floor() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.content_height = 80;
    check(frame_update_target_height(state) == 200, "target height is at least viewport height");
    state.content_height = 260;
    check(frame_update_target_height(state) == 260, "target height follows larger content height");
}

void cache_snapshot_builds_state_without_tree_ownership() {
    FramePipelineCacheState cache;
    cache.has_render_tree = true;
    cache.has_layout_tree = false;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 160;
    cache.framebuffer_height = 220;
    cache.viewport = Rect{0, 0, 160, 120};
    cache.content_height = 220;

    const FrameUpdateState state = make_frame_update_state(DomDirtyStyle | DomDirtyLayout, cache);
    check(state.dirty_flags == (DomDirtyStyle | DomDirtyLayout), "snapshot keeps dirty flags");
    check(state.has_render_tree && !state.has_layout_tree && state.has_layer_tree,
          "snapshot copies cache availability");
    check(state.has_framebuffer && state.framebuffer_width == 160 && state.framebuffer_height == 220,
          "snapshot copies framebuffer dimensions");
    check(state.viewport.width == 160 && state.viewport.height == 120 && state.content_height == 220,
          "snapshot copies viewport and content height");
}

void host_frame_sequence_keeps_bounded_actions() {
    FramePipelineCacheState cache;
    cache.viewport = Rect{0, 0, 240, 200};
    cache.content_height = 200;

    FrameUpdatePlan plan = plan_frame_update(make_frame_update_state(DomDirtyNone, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "empty cache performs first paint");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "first paint is full frame");

    cache.has_render_tree = true;
    cache.has_layout_tree = true;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 240;
    cache.framebuffer_height = 200;

    plan = plan_frame_update(make_frame_update_state(DomDirtyNone, cache));
    check(plan.action == FrameUpdateAction::None, "clean cached frame stays idle");

    plan = plan_frame_update(make_frame_update_state(DomDirtyPaint, cache));
    check(plan.action == FrameUpdateAction::RepaintExisting, "paint dirty frame reuses cache");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout, "paint dirty uses current layout");

    plan = plan_frame_update(make_frame_update_state(DomDirtyText | DomDirtyLayout, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "layout dirty frame rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout,
          "layout dirty compares previous and current layout");

    plan = plan_frame_update(make_frame_update_state(DomDirtyTree | DomDirtyLayout, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "tree dirty frame rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "tree dirty frame skips previous layout comparison");

    cache.content_height = 260;
    plan = plan_frame_update(make_frame_update_state(DomDirtyPaint, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline,
          "content height growth invalidates existing framebuffer");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "content height growth falls back to full frame");
}

} // namespace

int main() {
    try {
        clean_document_has_no_work();
        clean_uncached_document_gets_first_paint();
        paint_dirty_reuses_existing_pipeline();
        layout_dirty_rebuilds_with_previous_layout();
        repaint_plan_keeps_incremental_layout_when_size_matches();
        repaint_plan_forces_full_frame_when_resolved_layout_grows();
        repaint_plan_uses_viewport_floor_when_content_shrinks();
        repaint_plan_reuses_current_layout_for_paint_dirty();
        tree_dirty_rebuilds_without_previous_layout();
        missing_framebuffer_forces_full_frame();
        resized_framebuffer_forces_full_frame();
        invalid_viewport_is_conservative();
        target_height_uses_content_height_floor();
        cache_snapshot_builds_state_without_tree_ownership();
        host_frame_sequence_keeps_bounded_actions();
    } catch (const std::exception& error) {
        std::cerr << "frame update test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "frame update tests passed\n";
    return 0;
}
