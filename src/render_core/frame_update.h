#pragma once

#include "render_core/dom.h"
#include "render_core/geometry.h"

#include <array>
#include <cstddef>

namespace jellyframe {

enum class FrameUpdateAction {
    None,
    RepaintExisting,
    RebuildPipeline,
};

enum class FrameDirtyRectMode {
    None,
    CurrentLayout,
    PreviousAndCurrentLayout,
    FullFrame,
};

enum class FrameUpdateReason {
    None,
    CleanCached,
    InvalidViewport,
    FirstPaint,
    PaintOnlyDirty,
    TextDirtyStableLayout,
    StyleDirtyStableLayout,
    LayoutDirtyWithPreviousLayout,
    TreeDirty,
    MissingPipelineCache,
    MissingFramebuffer,
    FramebufferSizeMismatch,
    ResolvedFramebufferSizeMismatch,
};

constexpr std::size_t kFrameUpdateReasonCount =
    static_cast<std::size_t>(FrameUpdateReason::ResolvedFramebufferSizeMismatch) + 1;

struct FrameUpdateState {
    DomDirtyFlags dirty_flags = DomDirtyNone;
    bool has_render_tree = false;
    bool has_layout_tree = false;
    bool has_layer_tree = false;
    bool has_framebuffer = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    Rect viewport;
    int content_height = 0;
};

struct FramePipelineCacheState {
    bool has_render_tree = false;
    bool has_layout_tree = false;
    bool has_layer_tree = false;
    bool has_framebuffer = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    Rect viewport;
    int content_height = 0;
};

struct FrameUpdatePlan {
    FrameUpdateAction action = FrameUpdateAction::None;
    FrameDirtyRectMode dirty_rect_mode = FrameDirtyRectMode::None;
    FrameUpdateReason reason = FrameUpdateReason::None;
    bool can_reuse_render_and_layout = false;
    bool needs_previous_layout = false;
    bool needs_full_framebuffer = false;
};

struct FrameRepaintPlan {
    FrameDirtyRectMode dirty_rect_mode = FrameDirtyRectMode::None;
    FrameUpdateReason reason = FrameUpdateReason::None;
    int target_width = 0;
    int target_height = 0;
    bool can_repaint_dirty_rects = false;
    bool needs_full_framebuffer = false;
};

struct FrameUpdateStatistics {
    std::size_t idle_frames = 0;
    std::size_t repaint_existing_frames = 0;
    std::size_t rebuild_pipeline_frames = 0;
    std::array<std::size_t, kFrameUpdateReasonCount> reasons{};
    std::size_t dirty_none_frames = 0;
    std::size_t dirty_tree_frames = 0;
    std::size_t dirty_attribute_frames = 0;
    std::size_t dirty_text_frames = 0;
    std::size_t dirty_style_frames = 0;
    std::size_t dirty_layout_frames = 0;
    std::size_t dirty_paint_frames = 0;
    std::size_t dirty_render_or_layout_frames = 0;
};

const char* frame_update_action_name(FrameUpdateAction action);
const char* frame_dirty_rect_mode_name(FrameDirtyRectMode mode);
const char* frame_update_reason_name(FrameUpdateReason reason);
std::size_t frame_update_reason_index(FrameUpdateReason reason);
void record_frame_update_plan(FrameUpdateStatistics& statistics, const FrameUpdatePlan& plan);
void record_frame_update_plan(FrameUpdateStatistics& statistics,
                              const FrameUpdatePlan& plan,
                              DomDirtyFlags dirty_flags);
void record_frame_dirty_flags(FrameUpdateStatistics& statistics, DomDirtyFlags dirty_flags);
std::size_t frame_update_reason_count(const FrameUpdateStatistics& statistics,
                                      FrameUpdateReason reason);

FrameUpdateState make_frame_update_state(DomDirtyFlags dirty_flags,
                                         const FramePipelineCacheState& cache_state);
int frame_update_target_height(const FrameUpdateState& state);
FrameUpdatePlan plan_frame_update(const FrameUpdateState& state);
FrameRepaintPlan plan_frame_repaint(const FrameUpdateState& state,
                                    const FrameUpdatePlan& update_plan,
                                    int resolved_content_height);

} // namespace jellyframe
