#include "render_core/flex_grid_paint.h"

#include "render_core/layout.h"

#include <algorithm>

namespace jellyframe {

std::vector<const LayoutBox*> ordered_flex_paint_children(const LayoutBox& box) {
    if (box.style.display != Display::Flex) {
        return {};
    }
    const bool has_nonzero_order = std::any_of(box.children.begin(), box.children.end(), [](const LayoutBoxPtr& child) {
        return child->style.flex_order != 0;
    });
    if (!has_nonzero_order) {
        return {};
    }
    std::vector<const LayoutBox*> ordered;
    ordered.reserve(box.children.size());
    for (const LayoutBoxPtr& child : box.children) {
        ordered.push_back(child.get());
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const LayoutBox* left, const LayoutBox* right) {
        return left->style.flex_order < right->style.flex_order;
    });
    return ordered;
}

} // namespace jellyframe
