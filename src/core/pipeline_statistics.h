#pragma once

#include "core/arena.h"
#include "core/dom.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"

#include <cstddef>

namespace jellyframe {

struct PipelineArenaStatistics {
    std::size_t used_bytes = 0;
    std::size_t capacity_bytes = 0;
    std::size_t wasted_bytes = 0;
    std::size_t block_count = 0;
};

struct PipelineStatistics {
    DomStatistics dom;
    std::size_t render_objects = 0;
    std::size_t layout_boxes = 0;
    std::size_t layers = 0;
    std::size_t display_commands = 0;
    std::size_t flattened_display_commands = 0;
    std::size_t framebuffer_bytes = 0;
    std::size_t resource_bytes = 0;
    PipelineArenaStatistics render_arena;
    PipelineArenaStatistics layout_arena;
    PipelineArenaStatistics layer_arena;
    std::size_t estimated_heap_bytes = 0;
};

struct PipelineStatisticsInput {
    const Node* document = nullptr;
    const RenderObject* render_tree = nullptr;
    const LayoutBox* layout_tree = nullptr;
    const LayerNode* layer_tree = nullptr;
    const DisplayList* flattened_display_list = nullptr;
    const FrameBuffer* framebuffer = nullptr;
    const MonotonicArena* render_arena = nullptr;
    const MonotonicArena* layout_arena = nullptr;
    const MonotonicArena* layer_arena = nullptr;
    std::size_t resource_bytes = 0;
};

PipelineStatistics collect_pipeline_statistics(const PipelineStatisticsInput& input);
std::size_t framebuffer_byte_size(const FrameBuffer& framebuffer);

} // namespace jellyframe
