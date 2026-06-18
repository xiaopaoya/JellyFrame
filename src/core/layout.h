#pragma once

#include "core/arena.h"
#include "core/dom.h"
#include "core/geometry.h"
#include "core/render_tree.h"
#include "core/style.h"
#include "core/text_backend.h"

#include <memory>
#include <vector>

namespace jellyframe {

struct LayoutBox;

struct LayoutBoxDeleter {
    bool arena_owned = false;
    void operator()(LayoutBox* box) const;
};

using LayoutBoxPtr = std::unique_ptr<LayoutBox, LayoutBoxDeleter>;

struct LayoutBox {
    const Node* node = nullptr;
    Style style;
    Rect rect;
    std::vector<LayoutBoxPtr> children;
};

struct LayoutEngineOptions {
    std::size_t max_layout_boxes = 4096;
};

class LayoutEngine {
public:
    explicit LayoutEngine(const StyleResolver& style_resolver,
                          TextMeasureProvider text_measure = {},
                          LayoutEngineOptions options = {});

    LayoutBoxPtr layout(const Node& root, int viewport_width) const;
    LayoutBoxPtr layout(const Node& root, int viewport_width, MonotonicArena& arena) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width) const;
    LayoutBoxPtr layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const;

private:
    const StyleResolver& style_resolver_;
    TextMeasureProvider text_measure_;
    LayoutEngineOptions options_;

    int layout_box(LayoutBox& box, int x, int y, int width) const;
    int layout_flex_box(LayoutBox& box, int content_x, int content_y, int content_width) const;
    int layout_grid_box(LayoutBox& box, int content_x, int content_y, int content_width) const;
    int layout_inline_children(LayoutBox& box, int content_x, int content_y, int content_width) const;
    void layout_positioned_children(LayoutBox& box,
                                    int content_x,
                                    int content_y,
                                    int content_width,
                                    int content_height,
                                    int viewport_width) const;
    LayoutBoxPtr build_with_arena(const RenderObject& render_tree,
                                  int viewport_width,
                                  MonotonicArena* arena) const;
    void build_layout_tree(const RenderObject& object,
                           LayoutBox& box,
                           std::size_t& layout_box_count,
                           MonotonicArena* arena) const;
    LayoutBoxPtr make_layout_box(MonotonicArena* arena) const;
};

std::size_t count_layout_boxes(const LayoutBox& root);

} // namespace jellyframe
