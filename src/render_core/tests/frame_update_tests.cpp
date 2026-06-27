#include "render_core/frame_update.h"

#include <iostream>
#include <stdexcept>
#include <string>

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
    check(plan.reason == FrameUpdateReason::CleanCached, "clean cached reason is explicit");
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
    check(plan.reason == FrameUpdateReason::FirstPaint, "first paint reason is explicit");
    check(plan.needs_full_framebuffer, "first paint needs framebuffer");
}

void paint_dirty_reuses_existing_pipeline() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyPaint));
    check(plan.action == FrameUpdateAction::RepaintExisting, "paint dirty repaints existing frame");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout, "paint dirty uses current layout rects");
    check(plan.reason == FrameUpdateReason::PaintOnlyDirty, "paint dirty reason is explicit");
    check(plan.can_reuse_render_and_layout, "paint dirty reuses render/layout");
    check(!plan.needs_previous_layout, "paint dirty does not need old layout");
}

void layout_dirty_rebuilds_with_previous_layout() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyText | DomDirtyLayout));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "layout dirty rebuilds pipeline");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout,
          "layout dirty compares previous and current layout");
    check(plan.reason == FrameUpdateReason::LayoutDirtyWithPreviousLayout,
          "layout dirty reason records previous layout reuse");
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
    check(repaint_plan.reason == FrameUpdateReason::LayoutDirtyWithPreviousLayout,
          "repaint plan preserves incremental reason");
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
    check(repaint_plan.reason == FrameUpdateReason::ResolvedFramebufferSizeMismatch,
          "grown resolved layout reports size mismatch reason");
    check(repaint_plan.needs_full_framebuffer, "grown resolved layout needs full framebuffer");
    check(repaint_plan.target_height == 420, "repaint plan uses grown content height");
}

void repaint_plan_uses_viewport_floor_when_content_shrinks() {
    const FrameUpdateState state = cached_state(DomDirtyText | DomDirtyLayout);
    const FrameUpdatePlan update_plan = plan_frame_update(state);
    const FrameRepaintPlan repaint_plan = plan_frame_repaint(state, update_plan, 80);
    check(!repaint_plan.can_repaint_dirty_rects, "shrunk resolved layout changes framebuffer size");
    check(repaint_plan.reason == FrameUpdateReason::ResolvedFramebufferSizeMismatch,
          "shrunk resolved layout reports size mismatch reason");
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
    check(repaint_plan.reason == FrameUpdateReason::PaintOnlyDirty,
          "paint dirty repaint reason is preserved");
    check(!repaint_plan.needs_full_framebuffer, "paint dirty avoids full framebuffer");
}

void tree_dirty_rebuilds_without_previous_layout() {
    const FrameUpdatePlan plan = plan_frame_update(cached_state(DomDirtyTree | DomDirtyLayout));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "tree dirty rebuilds pipeline");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "tree dirty falls back to full frame planning");
    check(plan.reason == FrameUpdateReason::TreeDirty, "tree dirty reason is explicit");
    check(!plan.needs_previous_layout, "tree dirty does not retain previous layout");
    check(plan.needs_full_framebuffer, "tree dirty needs full framebuffer repaint");
}

void missing_framebuffer_forces_full_frame() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.has_framebuffer = false;
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "missing framebuffer rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "missing framebuffer renders full frame");
    check(plan.reason == FrameUpdateReason::MissingFramebuffer, "missing framebuffer reason is explicit");
    check(plan.needs_full_framebuffer, "missing framebuffer needs full render");
}

void resized_framebuffer_forces_full_frame() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.framebuffer_height = 200;
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "resized framebuffer rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "resized framebuffer renders full frame");
    check(plan.reason == FrameUpdateReason::FramebufferSizeMismatch,
          "resized framebuffer reason is explicit");
    check(plan.needs_full_framebuffer, "resized framebuffer needs full render");
}

void invalid_viewport_is_conservative() {
    FrameUpdateState state = cached_state(DomDirtyPaint);
    state.viewport = Rect{0, 0, 0, 0};
    const FrameUpdatePlan plan = plan_frame_update(state);
    check(plan.action == FrameUpdateAction::RebuildPipeline, "invalid viewport rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame, "invalid viewport is full-frame conservative");
    check(plan.reason == FrameUpdateReason::InvalidViewport, "invalid viewport reason is explicit");
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
    check(plan.reason == FrameUpdateReason::FirstPaint, "first paint reason in sequence");

    cache.has_render_tree = true;
    cache.has_layout_tree = true;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 240;
    cache.framebuffer_height = 200;

    plan = plan_frame_update(make_frame_update_state(DomDirtyNone, cache));
    check(plan.action == FrameUpdateAction::None, "clean cached frame stays idle");
    check(plan.reason == FrameUpdateReason::CleanCached, "clean cached reason in sequence");

    plan = plan_frame_update(make_frame_update_state(DomDirtyPaint, cache));
    check(plan.action == FrameUpdateAction::RepaintExisting, "paint dirty frame reuses cache");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout, "paint dirty uses current layout");
    check(plan.reason == FrameUpdateReason::PaintOnlyDirty, "paint dirty reason in sequence");

    plan = plan_frame_update(make_frame_update_state(DomDirtyText | DomDirtyLayout, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "layout dirty frame rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout,
          "layout dirty compares previous and current layout");
    check(plan.reason == FrameUpdateReason::LayoutDirtyWithPreviousLayout,
          "layout dirty reason in sequence");

    plan = plan_frame_update(make_frame_update_state(DomDirtyTree | DomDirtyLayout, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline, "tree dirty frame rebuilds");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "tree dirty frame skips previous layout comparison");
    check(plan.reason == FrameUpdateReason::TreeDirty, "tree dirty reason in sequence");

    cache.content_height = 260;
    plan = plan_frame_update(make_frame_update_state(DomDirtyPaint, cache));
    check(plan.action == FrameUpdateAction::RebuildPipeline,
          "content height growth invalidates existing framebuffer");
    check(plan.dirty_rect_mode == FrameDirtyRectMode::FullFrame,
          "content height growth falls back to full frame");
    check(plan.reason == FrameUpdateReason::FramebufferSizeMismatch,
          "content height growth reports framebuffer size mismatch");
}

void frame_update_names_and_statistics_are_stable() {
    check(std::string(frame_update_action_name(FrameUpdateAction::RebuildPipeline)) == "rebuild-pipeline",
          "frame update action name");
    check(std::string(frame_dirty_rect_mode_name(FrameDirtyRectMode::PreviousAndCurrentLayout)) ==
              "previous-and-current-layout",
          "frame dirty mode name");
    check(std::string(frame_update_reason_name(FrameUpdateReason::ResolvedFramebufferSizeMismatch)) ==
              "resolved-framebuffer-size-mismatch",
          "frame update reason name");
    check(std::string(frame_update_reason_name(FrameUpdateReason::TextDirtyStableLayout)) ==
              "text-dirty-stable-layout",
          "text stable layout reason name");
    check(std::string(frame_update_reason_name(FrameUpdateReason::StyleDirtyStableLayout)) ==
              "style-dirty-stable-layout",
          "style stable layout reason name");

    FrameUpdateStatistics statistics;
    record_frame_update_plan(statistics, plan_frame_update(cached_state(DomDirtyNone)));
    record_frame_update_plan(statistics, plan_frame_update(cached_state(DomDirtyPaint)));
    record_frame_update_plan(statistics, plan_frame_update(cached_state(DomDirtyTree | DomDirtyLayout)));

    check(statistics.idle_frames == 1, "statistics count idle frames");
    check(statistics.repaint_existing_frames == 1, "statistics count repaint frames");
    check(statistics.rebuild_pipeline_frames == 1, "statistics count rebuild frames");
    check(frame_update_reason_count(statistics, FrameUpdateReason::CleanCached) == 1,
          "statistics count clean cached reason");
    check(frame_update_reason_count(statistics, FrameUpdateReason::PaintOnlyDirty) == 1,
          "statistics count paint dirty reason");
    check(frame_update_reason_count(statistics, FrameUpdateReason::TreeDirty) == 1,
          "statistics count tree dirty reason");
}

void frame_update_dirty_flag_statistics_are_stable() {
    FrameUpdateStatistics statistics;
    record_frame_update_plan(statistics, plan_frame_update(cached_state(DomDirtyNone)), DomDirtyNone);
    record_frame_update_plan(statistics,
                             plan_frame_update(cached_state(DomDirtyText | DomDirtyStyle | DomDirtyLayout)),
                             DomDirtyText | DomDirtyStyle | DomDirtyLayout);
    record_frame_update_plan(statistics,
                             plan_frame_update(cached_state(DomDirtyAttributes | DomDirtyPaint)),
                             DomDirtyAttributes | DomDirtyPaint);
    record_frame_update_plan(statistics,
                             plan_frame_update(cached_state(DomDirtyTree | DomDirtyLayout)),
                             DomDirtyTree | DomDirtyLayout);

    check(statistics.dirty_none_frames == 1, "dirty statistics count clean input");
    check(statistics.dirty_tree_frames == 1, "dirty statistics count tree input");
    check(statistics.dirty_attribute_frames == 1, "dirty statistics count attributes input");
    check(statistics.dirty_text_frames == 1, "dirty statistics count text input");
    check(statistics.dirty_style_frames == 1, "dirty statistics count style input");
    check(statistics.dirty_layout_frames == 2, "dirty statistics count layout input");
    check(statistics.dirty_paint_frames == 1, "dirty statistics count paint input");
    check(statistics.dirty_render_or_layout_frames == 3, "dirty statistics count render/layout input");
}

void frame_repaint_statistics_are_stable() {
    FrameRepaintStatistics statistics;
    const FrameUpdateState paint_state = cached_state(DomDirtyPaint);
    const FrameUpdatePlan paint_plan = plan_frame_update(paint_state);
    record_frame_repaint_result(statistics,
                                plan_frame_repaint(paint_state, paint_plan, 320),
                                true);

    const FrameUpdateState layout_state = cached_state(DomDirtyText | DomDirtyLayout);
    const FrameUpdatePlan layout_plan = plan_frame_update(layout_state);
    record_frame_repaint_result(statistics,
                                plan_frame_repaint(layout_state, layout_plan, 420),
                                false);

    check(statistics.dirty_rect_frames == 1, "repaint statistics count dirty rect frames");
    check(statistics.full_frame_frames == 1, "repaint statistics count full frame frames");
    check(frame_repaint_dirty_rect_reason_count(statistics, FrameUpdateReason::PaintOnlyDirty) == 1,
          "repaint statistics count dirty rect reason");
    check(frame_repaint_full_frame_reason_count(statistics, FrameUpdateReason::ResolvedFramebufferSizeMismatch) == 1,
          "repaint statistics count full frame fallback reason");
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
        frame_update_names_and_statistics_are_stable();
        frame_update_dirty_flag_statistics_are_stable();
        frame_repaint_statistics_are_stable();
    } catch (const std::exception& error) {
        std::cerr << "frame update test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "frame update tests passed\n";
    return 0;
}
