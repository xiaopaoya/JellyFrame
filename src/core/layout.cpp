#include "core/layout.h"

#include <algorithm>

namespace wearweb {
namespace {

int estimate_text_width(const std::string& text, int font_size) {
    int width = 0;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch < 0x80) {
            width += std::max(1, (font_size * 2) / 3);
            ++index;
        } else {
            width += font_size;
            if ((ch & 0xe0U) == 0xc0U) {
                index += 2;
            } else if ((ch & 0xf0U) == 0xe0U) {
                index += 3;
            } else if ((ch & 0xf8U) == 0xf0U) {
                index += 4;
            } else {
                ++index;
            }
        }
    }
    return width + std::max(6, font_size / 2);
}

int horizontal_edges(const EdgeSizes& edges) {
    return edges.left + edges.right;
}

int vertical_edges(const EdgeSizes& edges) {
    return edges.top + edges.bottom;
}

int text_line_height(int font_size) {
    return font_size + std::max(6, font_size / 3);
}

bool participates_in_inline_flow(const LayoutBox& box) {
    return (box.node != nullptr && box.node->type == NodeType::Text) ||
        box.style.display == Display::Inline || box.style.display == Display::InlineBlock;
}

bool has_only_inline_children(const LayoutBox& box) {
    if (box.children.empty()) {
        return false;
    }
    for (const auto& child : box.children) {
        if (!participates_in_inline_flow(*child)) {
            return false;
        }
    }
    return true;
}

void shift_box(LayoutBox& box, int dx, int dy) {
    box.rect.x += dx;
    box.rect.y += dy;
    for (auto& child : box.children) {
        shift_box(*child, dx, dy);
    }
}

} // namespace

LayoutEngine::LayoutEngine(const StyleResolver& style_resolver)
    : style_resolver_(style_resolver) {}

std::unique_ptr<LayoutBox> LayoutEngine::layout(const Node& root, int viewport_width) const {
    RenderTreeBuilder render_tree_builder(style_resolver_);
    auto render_tree = render_tree_builder.build(root);
    return layout(*render_tree, viewport_width);
}

std::unique_ptr<LayoutBox> LayoutEngine::layout(const RenderObject& render_tree, int viewport_width) const {
    auto root_box = std::make_unique<LayoutBox>();
    root_box->node = render_tree.node;
    root_box->style = render_tree.style;
    build_layout_tree(render_tree, *root_box);
    root_box->rect.height = layout_box(*root_box, 0, 0, viewport_width);
    return root_box;
}

void LayoutEngine::build_layout_tree(const RenderObject& object, LayoutBox& box) const {
    for (const auto& child : object.children) {
        auto child_box = std::make_unique<LayoutBox>();
        child_box->node = child->node;
        child_box->style = child->style;
        build_layout_tree(*child, *child_box);
        box.children.push_back(std::move(child_box));
    }
}

int LayoutEngine::layout_box(LayoutBox& box, int x, int y, int width) const {
    const int margin_left = box.style.margin_left_auto ? 0 : box.style.margin.left;
    const int margin_right = box.style.margin_right_auto ? 0 : box.style.margin.right;
    const int border_box_y = y + box.style.margin.top;
    const int available_content_width = box.style.width >= 0
        ? (box.style.box_sizing_border_box
              ? std::max(0, box.style.width - horizontal_edges(box.style.border_width) -
                                horizontal_edges(box.style.padding))
              : box.style.width)
        : std::max(0, width - margin_left - margin_right -
                         horizontal_edges(box.style.border_width) - horizontal_edges(box.style.padding));
    int content_width = available_content_width;
    if (box.style.max_width >= 0) {
        const int max_content_width = box.style.box_sizing_border_box
            ? std::max(0, box.style.max_width - horizontal_edges(box.style.border_width) -
                              horizontal_edges(box.style.padding))
            : box.style.max_width;
        content_width = std::min(content_width, max_content_width);
    }
    const int measured_border_box_width = content_width + horizontal_edges(box.style.padding) +
        horizontal_edges(box.style.border_width);
    int border_box_width = std::max(box.style.min_width,
        box.style.width >= 0 && box.style.box_sizing_border_box ? box.style.width : measured_border_box_width);
    const int auto_space = std::max(0, width - border_box_width - margin_left - margin_right);
    int border_box_x = x + margin_left;
    if (box.style.margin_left_auto && box.style.margin_right_auto) {
        border_box_x = x + auto_space / 2;
    } else if (box.style.margin_left_auto) {
        border_box_x = x + auto_space;
    }
    const int content_x = border_box_x + box.style.border_width.left + box.style.padding.left;
    int cursor_y = border_box_y + box.style.border_width.top + box.style.padding.top;

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        const int raw_text_width = estimate_text_width(box.node->text, box.style.font_size);
        const int text_indent = std::max(0, std::min(box.style.text_indent, content_width));
        const int usable_text_width = std::max(0, content_width - text_indent);
        const int text_width = std::max(box.style.min_width, std::min(usable_text_width, raw_text_width));
        const int line_height = box.style.line_height > 0 ? box.style.line_height : text_line_height(box.style.font_size);
        const int line_count = usable_text_width > 0
            ? std::max(1, (raw_text_width + usable_text_width - 1) / usable_text_width)
            : 1;
        const int text_height = std::max(box.style.min_height,
            box.style.height >= 0 ? box.style.height : line_height * line_count);
        int text_x = border_box_x + text_indent;
        if (box.style.text_align == TextAlign::Center) {
            text_x += std::max(0, (usable_text_width - text_width) / 2);
        } else if (box.style.text_align == TextAlign::End) {
            text_x += std::max(0, usable_text_width - text_width);
        }
        box.rect = Rect{text_x, border_box_y, text_width, text_height};
        return text_height;
    }

    int max_child_width = 0;
    const int children_height = box.style.display == Display::Flex
        ? layout_flex_box(box, content_x, cursor_y, content_width)
        : has_only_inline_children(box)
        ? layout_inline_children(box, content_x, cursor_y, content_width)
        : [&] {
            int height = 0;
            for (auto& child : box.children) {
                const int child_height = layout_box(*child, content_x, cursor_y, content_width);
                cursor_y += child_height;
                height += child_height;
                max_child_width = std::max(max_child_width,
                    child->rect.width + child->style.margin.left + child->style.margin.right);
            }
            return height;
        }();
    if (box.style.width < 0 && (box.style.display == Display::Inline || box.style.display == Display::InlineBlock)) {
        if (!box.children.empty()) {
            int min_child_x = box.children.front()->rect.x - box.children.front()->style.margin.left;
            int max_child_x = box.children.front()->rect.x + box.children.front()->rect.width +
                box.children.front()->style.margin.right;
            for (const auto& child : box.children) {
                min_child_x = std::min(min_child_x, child->rect.x - child->style.margin.left);
                max_child_x = std::max(max_child_x, child->rect.x + child->rect.width + child->style.margin.right);
            }
            max_child_width = std::max(0, max_child_x - min_child_x);
        }
        content_width = std::min(available_content_width, max_child_width);
        border_box_width = std::max(box.style.min_width,
            content_width + horizontal_edges(box.style.padding) + horizontal_edges(box.style.border_width));
        if (!box.children.empty()) {
            int min_child_x = box.children.front()->rect.x - box.children.front()->style.margin.left;
            for (const auto& child : box.children) {
                min_child_x = std::min(min_child_x, child->rect.x - child->style.margin.left);
            }
            const int dx = content_x - min_child_x;
            if (dx != 0) {
                for (auto& child : box.children) {
                    shift_box(*child, dx, 0);
                }
            }
        }
    }

    const int content_height = std::max(box.style.min_height, box.style.height >= 0 ? box.style.height : children_height);
    const int border_box_height = vertical_edges(box.style.border_width) + vertical_edges(box.style.padding) + content_height;
    const int total_height = box.style.margin.top + border_box_height + box.style.margin.bottom;
    box.rect = Rect{border_box_x, border_box_y, border_box_width, border_box_height};
    return total_height;
}

int LayoutEngine::layout_inline_children(LayoutBox& box, int content_x, int content_y, int content_width) const {
    int cursor_x = content_x + std::max(0, std::min(box.style.text_indent, content_width));
    int line_y = content_y;
    int line_height = 0;
    int line_start_x = cursor_x;
    std::size_t line_start_index = 0;

    const auto finish_line = [&](std::size_t line_end_index, int used_width) {
        if (line_end_index <= line_start_index || used_width <= 0) {
            return;
        }
        int dx = 0;
        const int line_capacity = std::max(0, content_x + content_width - line_start_x);
        if (box.style.text_align == TextAlign::Center) {
            dx = std::max(0, (line_capacity - used_width) / 2);
        } else if (box.style.text_align == TextAlign::End) {
            dx = std::max(0, line_capacity - used_width);
        }
        if (dx == 0) {
            return;
        }
        for (std::size_t index = line_start_index; index < line_end_index; ++index) {
            shift_box(*box.children[index], dx, 0);
        }
    };

    for (std::size_t index = 0; index < box.children.size(); ++index) {
        auto& child = box.children[index];
        layout_box(*child, 0, 0, content_width);
        const int child_outer_width = child->style.margin.left + child->rect.width + child->style.margin.right;
        const int child_outer_height = child->style.margin.top + child->rect.height + child->style.margin.bottom;
        const int remaining_width = std::max(0, content_x + content_width - cursor_x);

        if (cursor_x > line_start_x && child_outer_width > remaining_width) {
            finish_line(index, cursor_x - line_start_x);
            line_y += std::max(1, line_height);
            line_height = 0;
            cursor_x = content_x;
            line_start_x = cursor_x;
            line_start_index = index;
        }

        const int target_x = cursor_x + child->style.margin.left;
        const int target_y = line_y + child->style.margin.top;
        shift_box(*child, target_x - child->rect.x, target_y - child->rect.y);
        cursor_x += child_outer_width;
        line_height = std::max(line_height, child_outer_height);
    }

    finish_line(box.children.size(), cursor_x - line_start_x);
    return line_y - content_y + std::max(0, line_height);
}

int LayoutEngine::layout_flex_box(LayoutBox& box, int content_x, int content_y, int content_width) const {
    if (box.children.empty()) {
        return 0;
    }

    int total_child_width = 0;
    int max_child_height = 0;
    for (auto& child : box.children) {
        layout_box(*child, 0, 0, content_width);
        total_child_width += child->rect.width + child->style.margin.left + child->style.margin.right;
        max_child_height = std::max(max_child_height, child->rect.height + child->style.margin.top + child->style.margin.bottom);
    }

    int gap = 0;
    int cursor_x = content_x;
    if (box.style.justify_content == JustifyContent::Center) {
        cursor_x += std::max(0, (content_width - total_child_width) / 2);
    } else if (box.style.justify_content == JustifyContent::SpaceAround) {
        gap = box.children.empty() ? 0 : std::max(0, (content_width - total_child_width) / static_cast<int>(box.children.size()));
        cursor_x += gap / 2;
    } else if (box.style.justify_content == JustifyContent::SpaceBetween && box.children.size() > 1) {
        gap = std::max(0, (content_width - total_child_width) / static_cast<int>(box.children.size() - 1));
    }

    const int container_height = std::max(box.style.min_height, box.style.height >= 0 ? box.style.height : max_child_height);
    for (auto& child : box.children) {
        int target_y = content_y;
        if (box.style.align_items == AlignItems::Center) {
            target_y += std::max(0, (container_height - child->rect.height) / 2);
        } else if (box.style.align_items == AlignItems::End) {
            target_y += std::max(0, container_height - child->rect.height);
        }

        const int dx = cursor_x + child->style.margin.left - child->rect.x;
        const int dy = target_y + child->style.margin.top - child->rect.y;
        shift_box(*child, dx, dy);
        cursor_x += child->rect.width + child->style.margin.left + child->style.margin.right + gap;
    }

    return container_height;
}

} // namespace wearweb
