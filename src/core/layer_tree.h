#pragma once

#include "core/geometry.h"
#include "core/layout.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace wearweb {

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
    std::vector<std::unique_ptr<LayerNode>> children;
};

class LayerTreeBuilder {
public:
    std::unique_ptr<LayerNode> build(const LayoutBox& root) const;
    DisplayList flatten(const LayerNode& root) const;

private:
    void build_children(const LayoutBox& box, LayerNode& layer, std::size_t& next_source_order) const;
    void build_box(const LayoutBox& box, LayerNode& parent_layer, std::size_t& next_source_order) const;
};

std::size_t count_layers(const LayerNode& layer);
std::size_t count_layer_display_commands(const LayerNode& layer);

} // namespace wearweb
