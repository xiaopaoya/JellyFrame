#pragma once

#include "render_core/arena.h"
#include "render_core/geometry.h"
#include "render_core/layout.h"
#include "render_core/style.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jellyframe {

struct LayerNode;
struct StyleOverride;

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

enum class ImageResolveKind : std::uint8_t {
    Content,
    Background,
};

using ImageHandleResolveCallback = bool (*)(const Node& node,
                                            ImageResolveKind kind,
                                            std::uint16_t background_resource_id,
                                            std::uint32_t& handle,
                                            void* context);

struct ImageHandleResolver {
    ImageHandleResolveCallback resolve = nullptr;
    void* context = nullptr;
};

using ScrollOffsetResolveCallback = int (*)(const Node& node, int max_scroll_y, void* context);

struct ScrollOffsetResolver {
    ScrollOffsetResolveCallback resolve_y = nullptr;
    void* context = nullptr;
};

struct LayerNode {
    LayerType type = LayerType::Paint;
    LayerReasons reasons = LayerReasonNone;
    const LayoutBox* box = nullptr;
    Rect bounds;
    Rect clip_rect;
    bool has_clip = false;
    float opacity = 1.0F;
    Transform2D transform;
    int transform_origin_x_percent = 50;
    int transform_origin_y_percent = 50;
    bool has_transform = false;
    int scroll_y = 0;
    int max_scroll_y = 0;
    int z_index = 0;
    std::size_t source_order = 0;
    DisplayList display_list;
    std::vector<LayerNodePtr> children;
};

struct LayerTreeOverrideScratch {
    std::vector<LayerNode*> pending;

    void clear() {
        pending.clear();
    }

    void release() {
        std::vector<LayerNode*>().swap(pending);
    }
};

struct LayerTreeBuilderOptions {
    std::size_t max_layers = 1024;
    std::size_t max_display_commands = 8192;
    DiagnosticSink* diagnostics = nullptr;
    ImageHandleResolver image_resolver;
    ScrollOffsetResolver scroll_resolver;
    TextMeasureProvider text_measure;
    bool paint_scroll_indicators = false;
};

class LayerTreeBuilder {
public:
    explicit LayerTreeBuilder(LayerTreeBuilderOptions options = {});

    LayerNodePtr build(const LayoutBox& root) const;
    LayerNodePtr build(const LayoutBox& root, MonotonicArena& arena) const;
    DisplayList flatten(const LayerNode& root) const;
    void flatten_into(const LayerNode& root, DisplayList& output) const;

private:
    LayerTreeBuilderOptions options_;

    LayerNodePtr build_with_arena(const LayoutBox& root, MonotonicArena* arena) const;
    void trim_display_list(DisplayList& display_list,
                           std::size_t command_begin,
                           std::size_t& remaining_commands,
                           bool& budget_reported) const;
    void build_children(const LayoutBox& box,
                        LayerNode& layer,
                        MonotonicArena* arena,
                        const Rect& viewport,
                        std::size_t& remaining_commands,
                        bool& display_budget_reported) const;
    LayerNodePtr make_layer_node(MonotonicArena* arena) const;
};

std::size_t count_layers(const LayerNode& layer);
std::size_t count_layer_display_commands(const LayerNode& layer);

// Applies only opacity overrides whose nodes already own a layer. Returning
// false leaves the tree unchanged so callers can take the conservative rebuild.
bool apply_opacity_overrides_to_layer_tree(LayerNode& root,
                                           const std::vector<StyleOverride>& overrides,
                                           LayerTreeOverrideScratch& scratch);

} // namespace jellyframe
