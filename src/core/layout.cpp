#include "core/layout.h"

#include "core/form_control.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace jellyframe {
namespace {

int horizontal_edges(const EdgeSizes& edges) {
    return edges.left + edges.right;
}

int vertical_edges(const EdgeSizes& edges) {
    return edges.top + edges.bottom;
}

bool has_aspect_ratio(const Style& style) {
    return style.aspect_ratio_width > 0 && style.aspect_ratio_height > 0;
}

std::uint32_t consume_utf8_codepoint(const std::string& text, std::size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U && index + 1 < text.size()) {
        width = 2;
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
    } else if ((lead & 0xf0U) == 0xe0U && index + 2 < text.size()) {
        width = 3;
        codepoint = ((lead & 0x0fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
    } else if ((lead & 0xf8U) == 0xf0U && index + 3 < text.size()) {
        width = 4;
        codepoint = ((lead & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
    }
    index += std::min(width, text.size() - index);
    return codepoint;
}

bool is_cjk_codepoint(std::uint32_t codepoint) {
    return (codepoint >= 0x3400U && codepoint <= 0x4dbfU) ||
        (codepoint >= 0x4e00U && codepoint <= 0x9fffU) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU);
}

bool has_text_wrap_opportunity(const std::string& text) {
    int cjk_count = 0;
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        if (codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
            codepoint == '-' || codepoint == '/') {
            return true;
        }
        if (is_cjk_codepoint(codepoint)) {
            ++cjk_count;
            if (cjk_count > 1) {
                return true;
            }
        }
    }
    return false;
}

constexpr int kMaxGridColumns = 32;

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

void LayoutBoxDeleter::operator()(LayoutBox* box) const {
    if (!arena_owned) {
        delete box;
    }
}

LayoutEngine::LayoutEngine(const StyleResolver& style_resolver,
                           TextMeasureProvider text_measure,
                           LayoutEngineOptions options)
    : style_resolver_(style_resolver), text_measure_(text_measure), options_(options) {}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width) const {
    RenderTreeBuilder render_tree_builder(style_resolver_);
    auto render_tree = render_tree_builder.build(root);
    return layout(*render_tree, viewport_width);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, MonotonicArena& arena) const {
    RenderTreeBuilder render_tree_builder(style_resolver_);
    auto render_tree = render_tree_builder.build(root, arena);
    return layout(*render_tree, viewport_width, arena);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width) const {
    return build_with_arena(render_tree, viewport_width, nullptr);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const {
    return build_with_arena(render_tree, viewport_width, &arena);
}

LayoutBoxPtr LayoutEngine::build_with_arena(const RenderObject& render_tree,
                                            int viewport_width,
                                            MonotonicArena* arena) const {
    auto root_box = make_layout_box(arena);
    root_box->node = render_tree.node;
    root_box->style = render_tree.style;
    std::size_t layout_box_count = 1;
    build_layout_tree(render_tree, *root_box, layout_box_count, arena);
    root_box->rect.height = layout_box(*root_box, 0, 0, viewport_width);
    return root_box;
}

void LayoutEngine::build_layout_tree(const RenderObject& object,
                                     LayoutBox& box,
                                     std::size_t& layout_box_count,
                                     MonotonicArena* arena) const {
    const std::size_t max_layout_boxes = std::max<std::size_t>(1, options_.max_layout_boxes);
    for (const auto& child : object.children) {
        if (layout_box_count >= max_layout_boxes) {
            return;
        }
        auto child_box = make_layout_box(arena);
        ++layout_box_count;
        child_box->node = child->node;
        child_box->style = child->style;
        build_layout_tree(*child, *child_box, layout_box_count, arena);
        box.children.push_back(std::move(child_box));
    }
}

LayoutBoxPtr LayoutEngine::make_layout_box(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
    }
    return LayoutBoxPtr(&arena->create<LayoutBox>(), LayoutBoxDeleter{true});
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
        const TextMetrics metrics = measure_text(text_measure_, box.node->text, box.style.font_size, box.style.font_weight);
        const int raw_text_width = metrics.width;
        const int text_indent = std::max(0, std::min(box.style.text_indent, content_width));
        const int usable_text_width = std::max(0, content_width - text_indent);
        const int text_width = std::max(box.style.min_width, std::min(usable_text_width, raw_text_width));
        const int line_height = box.style.line_height > 0 ? box.style.line_height : metrics.line_height;
        const bool can_wrap = has_text_wrap_opportunity(box.node->text);
        const int line_count = can_wrap && usable_text_width > 0
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
        : box.style.display == Display::Grid
        ? layout_grid_box(box, content_x, cursor_y, content_width)
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

    const int intrinsic_control_height = box.node != nullptr && is_form_control(*box.node)
        ? (box.style.line_height > 0
              ? box.style.line_height
              : fallback_text_metrics({}, box.style.font_size, box.style.font_weight).line_height)
        : 0;
    const int aspect_ratio_height = has_aspect_ratio(box.style) && content_width > 0
        ? std::max(1, (content_width * box.style.aspect_ratio_height + box.style.aspect_ratio_width / 2) /
                         box.style.aspect_ratio_width)
        : 0;
    const int content_height = std::max(box.style.min_height,
        box.style.height >= 0 ? box.style.height : std::max({children_height, intrinsic_control_height, aspect_ratio_height}));
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
    if (box.children.size() > 1) {
        total_child_width += box.style.column_gap * static_cast<int>(box.children.size() - 1);
    }

    if (box.style.flex_wrap) {
        int cursor_x = content_x;
        int line_y = content_y;
        int line_height = 0;
        int max_line_width = std::max(1, content_width);
        for (auto& child : box.children) {
            const int child_width = child->rect.width + child->style.margin.left + child->style.margin.right;
            const int child_height = child->rect.height + child->style.margin.top + child->style.margin.bottom;
            const bool should_wrap = cursor_x > content_x && cursor_x + child_width > content_x + max_line_width;
            if (should_wrap) {
                line_y += std::max(1, line_height) + box.style.row_gap;
                cursor_x = content_x;
                line_height = 0;
            }
            const int dx = cursor_x + child->style.margin.left - child->rect.x;
            const int dy = line_y + child->style.margin.top - child->rect.y;
            shift_box(*child, dx, dy);
            cursor_x += child_width + box.style.column_gap;
            line_height = std::max(line_height, child_height);
        }
        return line_y - content_y + std::max(0, line_height);
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
        cursor_x += child->rect.width + child->style.margin.left + child->style.margin.right + gap + box.style.column_gap;
    }

    return container_height;
}

int LayoutEngine::layout_grid_box(LayoutBox& box, int content_x, int content_y, int content_width) const {
    if (box.children.empty()) {
        return 0;
    }

    const int column_gap = std::max(0, box.style.column_gap);
    const int row_gap = std::max(0, box.style.row_gap);
    int column_count = 1;
    std::vector<int> column_widths;
    if (box.style.grid_template_column_count > 0) {
        column_count = std::min(box.style.grid_template_column_count, kMaxGridColumns);
        column_widths.assign(static_cast<std::size_t>(column_count), 0);
        int fixed_width = 0;
        int flexible_count = 0;
        for (int column = 0; column < column_count; ++column) {
            const int width = box.style.grid_template_column_widths[static_cast<std::size_t>(column)];
            if (width > 0) {
                column_widths[static_cast<std::size_t>(column)] = width;
                fixed_width += width;
            } else {
                ++flexible_count;
            }
        }
        const int total_gap_width = column_gap * std::max(0, column_count - 1);
        const int flexible_width = flexible_count > 0
            ? std::max(1, (content_width - fixed_width - total_gap_width) / flexible_count)
            : 0;
        for (int& width : column_widths) {
            if (width <= 0) {
                width = flexible_width;
            }
        }
    } else {
        const int min_track = std::max(1, box.style.grid_min_track_width > 0 ? box.style.grid_min_track_width : content_width);
        column_count = std::max(1, (content_width + column_gap) / (min_track + column_gap));
        column_count = std::min(column_count, static_cast<int>(box.children.size()));
        column_count = std::min(column_count, kMaxGridColumns);
        const int total_gap_width = column_gap * std::max(0, column_count - 1);
        const int column_width = std::max(1, (content_width - total_gap_width) / column_count);
        column_widths.assign(static_cast<std::size_t>(column_count), column_width);
    }

    const auto item_width_for = [&](int column, int span) {
        int width = 0;
        for (int offset = 0; offset < span; ++offset) {
            width += column_widths[static_cast<std::size_t>(column + offset)];
        }
        width += column_gap * std::max(0, span - 1);
        return std::max(1, width);
    };

    const auto column_x_for = [&](int column) {
        int offset = 0;
        for (int i = 0; i < column; ++i) {
            offset += column_widths[static_cast<std::size_t>(i)] + column_gap;
        }
        return content_x + offset;
    };

    struct Placement {
        LayoutBox* child = nullptr;
        int row = 0;
        int column = 0;
        int column_span = 1;
        int row_span = 1;
    };

    std::vector<std::uint64_t> occupied;
    std::vector<int> row_heights;
    std::vector<Placement> placements;
    placements.reserve(box.children.size());

    const auto ensure_rows = [&](int rows) {
        while (static_cast<int>(occupied.size()) < rows) {
            occupied.push_back(0);
            row_heights.push_back(std::max(0, box.style.grid_auto_row_min));
        }
    };

    const auto can_place = [&](int row, int column, int column_span, int row_span) {
        if (column + column_span > column_count) {
            return false;
        }
        const std::uint64_t mask = ((std::uint64_t{1} << column_span) - 1U) << column;
        for (int r = row; r < row + row_span; ++r) {
            if (r < static_cast<int>(occupied.size()) &&
                (occupied[static_cast<std::size_t>(r)] & mask) != 0) {
                return false;
            }
        }
        return true;
    };

    for (auto& child : box.children) {
        const int column_span = std::max(1, std::min(child->style.grid_column_span, column_count));
        const int row_span = std::max(1, child->style.grid_row_span);
        int placed_row = 0;
        int placed_column = 0;
        bool placed = false;
        while (!placed) {
            ensure_rows(placed_row + row_span);
            for (int column = 0; column <= column_count - column_span; ++column) {
                if (can_place(placed_row, column, column_span, row_span)) {
                    placed_column = column;
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                ++placed_row;
            }
        }

        ensure_rows(placed_row + row_span);
        const std::uint64_t mask = ((std::uint64_t{1} << column_span) - 1U) << placed_column;
        for (int r = placed_row; r < placed_row + row_span; ++r) {
            occupied[static_cast<std::size_t>(r)] |= mask;
        }

        const int item_width = item_width_for(placed_column, column_span);
        const int original_width = child->style.width;
        const bool original_box_sizing = child->style.box_sizing_border_box;
        if (child->style.width < 0) {
            child->style.width = item_width;
            child->style.box_sizing_border_box = true;
        }
        const int child_height = layout_box(*child, 0, 0, item_width);
        child->style.width = original_width;
        child->style.box_sizing_border_box = original_box_sizing;
        const int min_allocated_height = box.style.grid_auto_row_min * row_span + row_gap * (row_span - 1);
        const int allocated_height = std::max(child_height, min_allocated_height);
        const int per_row_height = std::max(1, (allocated_height - row_gap * (row_span - 1) + row_span - 1) / row_span);
        for (int r = placed_row; r < placed_row + row_span; ++r) {
            row_heights[static_cast<std::size_t>(r)] =
                std::max(row_heights[static_cast<std::size_t>(r)], per_row_height);
        }

        placements.push_back(Placement{child.get(), placed_row, placed_column, column_span, row_span});
    }

    std::vector<int> row_offsets(row_heights.size(), 0);
    int total_height = 0;
    for (std::size_t row = 0; row < row_heights.size(); ++row) {
        row_offsets[row] = total_height;
        total_height += row_heights[row];
        if (row + 1 < row_heights.size()) {
            total_height += row_gap;
        }
    }

    for (const Placement& placement : placements) {
        int allocated_height = 0;
        for (int r = 0; r < placement.row_span; ++r) {
            allocated_height += row_heights[static_cast<std::size_t>(placement.row + r)];
        }
        allocated_height += row_gap * (placement.row_span - 1);
        const int target_x = column_x_for(placement.column) + placement.child->style.margin.left;
        const int target_y = content_y + row_offsets[static_cast<std::size_t>(placement.row)] +
            placement.child->style.margin.top;
        shift_box(*placement.child, target_x - placement.child->rect.x, target_y - placement.child->rect.y);
        placement.child->rect.width = item_width_for(placement.column, placement.column_span);
        if (placement.child->style.height < 0) {
            placement.child->rect.height = std::max(placement.child->rect.height, allocated_height);
        }
    }

    return total_height;
}

} // namespace jellyframe
