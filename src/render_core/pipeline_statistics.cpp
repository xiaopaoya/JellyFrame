#include "render_core/pipeline_statistics.h"

#include <algorithm>

namespace jellyframe {
namespace {

PipelineArenaStatistics arena_statistics(const MonotonicArena* arena) {
    if (arena == nullptr) {
        return {};
    }
    const std::size_t used = arena->used_bytes();
    const std::size_t capacity = arena->capacity_bytes();
    return PipelineArenaStatistics{used, capacity, capacity > used ? capacity - used : 0, arena->block_count()};
}

std::size_t estimate_dom_bytes(const DomStatistics& statistics) {
    return statistics.node_count * sizeof(Node) +
        statistics.attribute_count * sizeof(AttributeList::Entry);
}

std::size_t estimate_pipeline_heap_bytes(const PipelineStatistics& statistics) {
    const std::size_t render_bytes = statistics.render_arena.used_bytes > 0
        ? statistics.render_arena.used_bytes
        : statistics.render_objects * sizeof(RenderObject);
    const std::size_t layout_bytes = statistics.layout_arena.used_bytes > 0
        ? statistics.layout_arena.used_bytes
        : statistics.layout_boxes * sizeof(LayoutBox);
    const std::size_t layer_bytes = statistics.layer_arena.used_bytes > 0
        ? statistics.layer_arena.used_bytes
        : statistics.layers * sizeof(LayerNode);
    return estimate_dom_bytes(statistics.dom) +
        render_bytes +
        layout_bytes +
        layer_bytes +
        statistics.display_commands * sizeof(DisplayCommand) +
        statistics.framebuffer_bytes +
        statistics.resource_bytes;
}

} // namespace

std::size_t framebuffer_byte_size(const FrameBuffer& framebuffer) {
    if (framebuffer.width <= 0 || framebuffer.height <= 0) {
        return 0;
    }
    return framebuffer.pixels.size() * sizeof(Color);
}

PipelineStatistics collect_pipeline_statistics(const PipelineStatisticsInput& input) {
    PipelineStatistics statistics;
    if (input.document != nullptr) {
        statistics.dom = compute_dom_statistics(*input.document);
    }
    if (input.render_tree != nullptr) {
        statistics.render_objects = count_render_objects(*input.render_tree);
    }
    if (input.layout_tree != nullptr) {
        statistics.layout_boxes = count_layout_boxes(*input.layout_tree);
    }
    if (input.layer_tree != nullptr) {
        statistics.layers = count_layers(*input.layer_tree);
        statistics.display_commands = count_layer_display_commands(*input.layer_tree);
    }
    if (input.flattened_display_list != nullptr) {
        statistics.flattened_display_commands = input.flattened_display_list->size();
    }
    if (input.framebuffer != nullptr) {
        statistics.framebuffer_bytes = framebuffer_byte_size(*input.framebuffer);
    }
    statistics.resource_bytes = input.resource_bytes;
    statistics.render_arena = arena_statistics(input.render_arena);
    statistics.layout_arena = arena_statistics(input.layout_arena);
    statistics.layer_arena = arena_statistics(input.layer_arena);
    statistics.estimated_heap_bytes = estimate_pipeline_heap_bytes(statistics);
    return statistics;
}

} // namespace jellyframe
