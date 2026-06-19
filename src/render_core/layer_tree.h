#pragma once

#include "render_core/arena.h"
#include "render_core/geometry.h"
#include "render_core/layout.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jellyframe {

struct LayerNode;

struct LayerNodeDeleter {
    bool arena_owned = false;
    void operator()(LayerNode* layer) const;
};

using LayerNodePtr = std::unique_ptr<LayerNode, LayerNodeDeleter>;

enum class LayerType {
    Root,
    Paint,
    Clip,
    Stacking,
    Composited,
};

enum LayerReason : std::uint32_t {
    LayerReasonNone = 0,
    LayerReasonRoot = 1U << 0,
    LayerReasonOverflowClip = 1U << 1,
    LayerReasonOpacity = 1U << 2,
    LayerReasonTransform = 1U << 3,
    LayerReasonPositioned = 1U << 4,
    LayerReasonZIndex = 1U << 5,
    LayerReasonShadow = 1U << 6,
    LayerReasonRoundedClip = 1U << 7,
};

using LayerReasons = std::uint32_t;

struct LayerNode {
    LayerType type = LayerType::Paint;
    LayerReasons reasons = LayerReasonNone;
    const LayoutBox* box = nullptr;
    Rect bounds;
    Rect clip_rect;
    bool has_clip = false;
    float opacity = 1.0F;
    int z_index = 0;
    std::size_t source_order = 0;
    DisplayList display_list;
    std::vector<LayerNodePtr> children;
};

struct LayerTreeBuilderOptions {
    std::size_t max_layers = 1024;
    std::size_t max_display_commands = 8192;
    DiagnosticSink* diagnostics = nullptr;
};

class LayerTreeBuilder {
public:
    explicit LayerTreeBuilder(LayerTreeBuilderOptions options = {});

    LayerNodePtr build(const LayoutBox& root) const;
    LayerNodePtr build(const LayoutBox& root, MonotonicArena& arena) const;
    DisplayList flatten(const LayerNode& root) const;

private:
    LayerTreeBuilderOptions options_;

    LayerNodePtr build_with_arena(const LayoutBox& root, MonotonicArena* arena) const;
    void trim_display_list(DisplayList& display_list) const;
    void build_children(const LayoutBox& box,
                        LayerNode& layer,
                   std::size_t& next_source_order,
                   std::size_t& layer_count,
                   bool& layer_budget_reported,
                   MonotonicArena* arena) const;
    void build_box(const LayoutBox& box,
                   LayerNode& parent_layer,
                   std::size_t& next_source_order,
                   std::size_t& layer_count,
                   bool& layer_budget_reported,
                   MonotonicArena* arena) const;
    LayerNodePtr make_layer_node(MonotonicArena* arena) const;
};

std::size_t count_layers(const LayerNode& layer);
std::size_t count_layer_display_commands(const LayerNode& layer);

} // namespace jellyframe
