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
    FrameUpdateState state;
    state.dirty_flags = flags;
    state.has_render_tree = true;
    state.has_layout_tree = true;
    state.has_layer_tree = true;
    state.has_framebuffer = true;
    state.framebuffer_width = 240;
    state.framebuffer_height = 320;
    state.viewport = Rect{0, 0, 240, 200};
    state.content_height = 320;
    return state;
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

} // namespace

int main() {
    try {
        clean_document_has_no_work();
        clean_uncached_document_gets_first_paint();
        paint_dirty_reuses_existing_pipeline();
        layout_dirty_rebuilds_with_previous_layout();
        missing_framebuffer_forces_full_frame();
        resized_framebuffer_forces_full_frame();
        invalid_viewport_is_conservative();
        target_height_uses_content_height_floor();
    } catch (const std::exception& error) {
        std::cerr << "frame update test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "frame update tests passed\n";
    return 0;
}
