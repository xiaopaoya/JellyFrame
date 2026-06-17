#pragma once

#include "core/dom.h"
#include "core/geometry.h"

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
    bool can_reuse_render_and_layout = false;
    bool needs_previous_layout = false;
    bool needs_full_framebuffer = false;
};

struct FrameRepaintPlan {
    FrameDirtyRectMode dirty_rect_mode = FrameDirtyRectMode::None;
    int target_width = 0;
    int target_height = 0;
    bool can_repaint_dirty_rects = false;
    bool needs_full_framebuffer = false;
};

FrameUpdateState make_frame_update_state(DomDirtyFlags dirty_flags,
                                         const FramePipelineCacheState& cache_state);
int frame_update_target_height(const FrameUpdateState& state);
FrameUpdatePlan plan_frame_update(const FrameUpdateState& state);
FrameRepaintPlan plan_frame_repaint(const FrameUpdateState& state,
                                    const FrameUpdatePlan& update_plan,
                                    int resolved_content_height);

} // namespace jellyframe
