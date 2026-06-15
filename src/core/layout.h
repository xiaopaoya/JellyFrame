#pragma once

#include "core/dom.h"
#include "core/geometry.h"
#include "core/render_tree.h"
#include "core/style.h"
#include "core/text_backend.h"

#include <memory>
#include <vector>

namespace wearweb {

struct LayoutBox {
    const Node* node = nullptr;
    Style style;
    Rect rect;
    std::vector<std::unique_ptr<LayoutBox>> children;
};

class LayoutEngine {
public:
    explicit LayoutEngine(const StyleResolver& style_resolver, TextMeasureProvider text_measure = {});

    std::unique_ptr<LayoutBox> layout(const Node& root, int viewport_width) const;
    std::unique_ptr<LayoutBox> layout(const RenderObject& render_tree, int viewport_width) const;

private:
    const StyleResolver& style_resolver_;
    TextMeasureProvider text_measure_;

    int layout_box(LayoutBox& box, int x, int y, int width) const;
    int layout_flex_box(LayoutBox& box, int content_x, int content_y, int content_width) const;
    int layout_grid_box(LayoutBox& box, int content_x, int content_y, int content_width) const;
    int layout_inline_children(LayoutBox& box, int content_x, int content_y, int content_width) const;
    void build_layout_tree(const RenderObject& object, LayoutBox& box) const;
};

} // namespace wearweb
