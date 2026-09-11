#include "render_core/flex_grid_paint.h"

#include "render_core/layout.h"

#include <algorithm>

namespace jellyframe {

std::vector<const LayoutBox*> ordered_flex_children_for_order(const LayoutBox& box,
                                                              bool include_out_of_flow) {
    if (box.style.display != Display::Flex) {
        return {};
    }
    const auto is_ordered_child = [include_out_of_flow](const LayoutBoxPtr& child) {
        const bool out_of_flow = child->style.position == "absolute" || child->style.position == "fixed";
        return (include_out_of_flow || !out_of_flow) && child->style.flex_order != 0;
    };
    const bool has_nonzero_order = std::any_of(box.children.begin(), box.children.end(), is_ordered_child);
    if (!has_nonzero_order) {
        return {};
    }
    std::vector<const LayoutBox*> ordered;
    ordered.reserve(box.children.size());
    for (const LayoutBoxPtr& child : box.children) {
        const bool out_of_flow = child->style.position == "absolute" || child->style.position == "fixed";
        if (include_out_of_flow || !out_of_flow) {
            ordered.push_back(child.get());
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const LayoutBox* left, const LayoutBox* right) {
        return left->style.flex_order < right->style.flex_order;
    });
    return ordered;
}

std::vector<const LayoutBox*> ordered_flex_paint_children(const LayoutBox& box) {
    return ordered_flex_children_for_order(box, true);
}

} // namespace jellyframe
