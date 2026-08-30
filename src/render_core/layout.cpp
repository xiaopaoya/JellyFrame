#include "render_core/layout.h"

#include "render_core/form_control.h"
#include "render_core/text_normalization.h"
#include "render_core/text_scan.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

namespace jellyframe {
namespace {

int clamp_layout_value(std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(value,
                                                     std::numeric_limits<int>::min(),
                                                     std::numeric_limits<int>::max()));
}

int bounded_add(int left, int right) {
    return clamp_layout_value(static_cast<std::int64_t>(left) + right);
}

int bounded_subtract(int left, int right) {
    return clamp_layout_value(static_cast<std::int64_t>(left) - right);
}

int bounded_non_negative_add(int left, int right) {
    return std::max(0, bounded_add(left, right));
}

int bounded_non_negative_subtract(int left, int right) {
    return std::max(0, bounded_subtract(left, right));
}

[[maybe_unused]] int bounded_non_negative_multiply(int left, int right) {
    if (left <= 0 || right <= 0) {
        return 0;
    }
    return clamp_layout_value(static_cast<std::int64_t>(left) * right);
}

int horizontal_edges(const EdgeSizes& edges) {
    return bounded_add(edges.left, edges.right);
}

int vertical_edges(const EdgeSizes& edges) {
    return bounded_add(edges.top, edges.bottom);
}

int resolve_percent(int basis, int percent) {
    const std::int64_t non_negative_basis = std::max(0, basis);
    const std::int64_t scaled = non_negative_basis * std::max(0, percent) + 50;
    return static_cast<int>(std::clamp<std::int64_t>(scaled / 100, 0,
                                                     std::numeric_limits<int>::max()));
}

std::string quote_detail_value(const std::string& value, std::size_t max_chars = 48) {
    std::string output;
    output.reserve(std::min(value.size(), max_chars) + 2);
    output.push_back('"');
    std::size_t emitted = 0;
    for (char ch : value) {
        if (emitted >= max_chars) {
            output += "...";
            break;
        }
        if (ch == '"' || ch == '\\') {
            output.push_back('\\');
            output.push_back(ch);
        } else if (ch == '\n' || ch == '\r' || ch == '\t') {
            output.push_back(' ');
        } else {
            output.push_back(ch);
        }
        ++emitted;
    }
    output.push_back('"');
    return output;
}

std::string text_overflow_detail(const LayoutBox& box,
                                 const std::string& text,
                                 int measured_width,
                                 int available_width,
                                 int content_width,
                                 int text_indent) {
    std::ostringstream detail;
    detail << "text=" << quote_detail_value(text)
           << " measuredWidth=" << measured_width
           << " availableWidth=" << available_width
           << " contentWidth=" << content_width
           << " textIndent=" << text_indent
           << " fontSize=" << box.style.font_size
           << " fontWeight=" << box.style.font_weight
           << " node=" << quote_detail_value(dom_node_label(box.node), 48)
           << " path=" << quote_detail_value(dom_node_path(box.node), 160);
    return detail.str();
}

int resolved_content_width(const Style& style, int containing_width) {
    if (style.width_percent >= 0) {
        const int border_box_width = resolve_percent(containing_width, style.width_percent);
        return style.box_sizing_border_box
            ? std::max(0, border_box_width - horizontal_edges(style.border_width) - horizontal_edges(style.padding))
            : border_box_width;
    }
    if (style.width >= 0) {
        return style.box_sizing_border_box
            ? std::max(0, style.width - horizontal_edges(style.border_width) - horizontal_edges(style.padding))
            : style.width;
    }
    return std::max(0, containing_width - horizontal_edges(style.border_width) - horizontal_edges(style.padding));
}

int resolved_min_width(const Style& style, int containing_width) {
    if (style.min_width_percent >= 0) {
        return resolve_percent(containing_width, style.min_width_percent);
    }
    return style.min_width;
}

int resolved_max_content_width(const Style& style, int containing_width) {
    int max_width = style.max_width;
    if (style.max_width_percent >= 0) {
        max_width = resolve_percent(containing_width, style.max_width_percent);
    }
    if (max_width < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? std::max(0, max_width - horizontal_edges(style.border_width) - horizontal_edges(style.padding))
        : max_width;
}

int specified_content_height(const Style& style, int containing_height) {
    int height = style.height;
    if (style.height_percent >= 0) {
        height = resolve_percent(containing_height, style.height_percent);
    }
    if (height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? std::max(0, height - vertical_edges(style.border_width) - vertical_edges(style.padding))
        : height;
}

int specified_content_min_height(const Style& style, int containing_height) {
    int min_height = style.min_height;
    if (style.min_height_percent >= 0) {
        min_height = resolve_percent(containing_height, style.min_height_percent);
    }
    if (min_height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? std::max(0, min_height - vertical_edges(style.border_width) - vertical_edges(style.padding))
        : min_height;
}

int resolved_max_content_height(const Style& style, int containing_height) {
    int max_height = style.max_height;
    if (style.max_height_percent >= 0) {
        max_height = resolve_percent(containing_height, style.max_height_percent);
    }
    if (max_height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? std::max(0, max_height - vertical_edges(style.border_width) - vertical_edges(style.padding))
        : max_height;
}

bool has_aspect_ratio(const Style& style) {
    return style.aspect_ratio_width > 0 && style.aspect_ratio_height > 0;
}

bool is_out_of_flow_positioned(const Style& style) {
    return style.position == "absolute" || style.position == "fixed";
}

void shift_box(LayoutBox& box, int dx, int dy);

int horizontal_position_offset(const Style& style, int area_width, int box_width) {
    if (style.inset_left_specified) {
        return style.inset_left + style.margin.left;
    }
    if (style.inset_right_specified) {
        return area_width - style.inset_right - box_width - style.margin.right;
    }
    return style.margin.left;
}

int vertical_position_offset(const Style& style, int area_height, int box_height) {
    if (style.inset_top_specified) {
        return style.inset_top + style.margin.top;
    }
    if (style.inset_bottom_specified && area_height > 0) {
        return area_height - style.inset_bottom - box_height - style.margin.bottom;
    }
    return style.margin.top;
}

void apply_relative_position_offset(LayoutBox& box) {
    if (box.style.position != "relative") {
        return;
    }
    int dx = 0;
    int dy = 0;
    if (box.style.inset_left_specified) {
        dx = box.style.inset_left;
    } else if (box.style.inset_right_specified) {
        dx = -box.style.inset_right;
    }
    if (box.style.inset_top_specified) {
        dy = box.style.inset_top;
    } else if (box.style.inset_bottom_specified) {
        dy = -box.style.inset_bottom;
    }
    if (dx != 0 || dy != 0) {
        shift_box(box, dx, dy);
    }
}

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
struct FlexLayoutItem {
    LayoutBox* child = nullptr;
    int base_width = 0;
    int target_width = 0;
    bool force_width = false;
};

int flex_grow_factor(const FlexLayoutItem& item) {
    return item.child == nullptr ? 0 : std::max(0, item.child->style.flex_grow);
}

std::int64_t flex_shrink_weight(const FlexLayoutItem& item) {
    if (item.child == nullptr) {
        return 0;
    }
    const int shrink = std::max(0, item.child->style.flex_shrink);
    return static_cast<std::int64_t>(shrink) * std::max(1, item.base_width);
}

void distribute_flex_row_widths(std::vector<FlexLayoutItem>& items,
                                int free_space,
                                int total_grow,
                                std::int64_t total_shrink_weight) {
    if (free_space > 0 && total_grow > 0) {
        int remaining_space = free_space;
        int remaining_grow = total_grow;
        for (FlexLayoutItem& item : items) {
            const int grow = flex_grow_factor(item);
            if (grow <= 0) {
                continue;
            }
            const int share = remaining_grow == grow
                ? remaining_space
                : static_cast<int>((static_cast<std::int64_t>(remaining_space) * grow) / remaining_grow);
            remaining_space -= share;
            remaining_grow -= grow;
            item.target_width = std::max(0, item.base_width + share);
            item.force_width = true;
        }
    } else if (free_space < 0 && total_shrink_weight > 0) {
        const int deficit = -free_space;
        int remaining_deficit = deficit;
        std::int64_t remaining_weight = total_shrink_weight;
        for (FlexLayoutItem& item : items) {
            const std::int64_t weight = flex_shrink_weight(item);
            if (weight <= 0) {
                continue;
            }
            const int share = remaining_weight == weight
                ? remaining_deficit
                : static_cast<int>((static_cast<std::int64_t>(remaining_deficit) * weight) / remaining_weight);
            remaining_deficit -= share;
            remaining_weight -= weight;
            const int min_width = item.child == nullptr ? 0 : std::max(0, item.child->style.min_width);
            item.target_width = std::max(min_width, item.base_width - share);
            item.force_width = true;
        }
    }
}

struct FlexJustifyPlacement {
    int cursor = 0;
    int extra_gap = 0;
};

FlexJustifyPlacement flex_justify_placement(JustifyContent justify_content,
                                            int main_start,
                                            int main_size,
                                            int total_child_size,
                                            int in_flow_count) {
    FlexJustifyPlacement placement;
    placement.cursor = main_start;
    const int free_space = std::max(0, main_size - total_child_size);
    if (justify_content == JustifyContent::End) {
        placement.cursor += free_space;
    } else if (justify_content == JustifyContent::Center) {
        placement.cursor += free_space / 2;
    } else if (justify_content == JustifyContent::SpaceAround) {
        placement.extra_gap = in_flow_count == 0 ? 0 : free_space / in_flow_count;
        placement.cursor += placement.extra_gap / 2;
    } else if (justify_content == JustifyContent::SpaceBetween && in_flow_count > 1) {
        placement.extra_gap = free_space / (in_flow_count - 1);
    } else if (justify_content == JustifyContent::SpaceEvenly && in_flow_count > 0) {
        placement.extra_gap = free_space / (in_flow_count + 1);
        placement.cursor += placement.extra_gap;
    }
    return placement;
}

int flex_aligned_y(AlignItems align_items, int content_y, int container_height, int child_height) {
    if (align_items == AlignItems::Center) {
        return content_y + std::max(0, (container_height - child_height) / 2);
    }
    if (align_items == AlignItems::End) {
        return content_y + std::max(0, container_height - child_height);
    }
    return content_y;
}

int flex_aligned_x(AlignItems align_items, int content_x, int container_width, int child_width) {
    if (align_items == AlignItems::Center) {
        return content_x + std::max(0, (container_width - child_width) / 2);
    }
    if (align_items == AlignItems::End) {
        return content_x + std::max(0, container_width - child_width);
    }
    return content_x;
}

AlignItems flex_cross_axis_alignment(const LayoutBox& child, AlignItems parent_alignment) {
    return child.style.align_self == AlignItems::Auto ? parent_alignment : child.style.align_self;
}

bool flex_cross_size_is_auto(const Style& style, bool row_direction) {
    return row_direction
        ? style.height < 0 && style.height_percent < 0
        : style.width < 0 && style.width_percent < 0;
}

int flex_bounded_sum(int first, int second) {
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(first) + second,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

int flex_stretch_dimension(const Style& style, int outer_size, bool vertical) {
    const int margins = vertical
        ? flex_bounded_sum(style.margin.top, style.margin.bottom)
        : flex_bounded_sum(style.margin.left, style.margin.right);
    const int edges = vertical
        ? flex_bounded_sum(vertical_edges(style.border_width), vertical_edges(style.padding))
        : flex_bounded_sum(horizontal_edges(style.border_width), horizontal_edges(style.padding));
    const int target_outer_size = static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(outer_size) - margins, 0, std::numeric_limits<int>::max()));
    return style.box_sizing_border_box
        ? target_outer_size
        : std::max(0, target_outer_size - edges);
}

void set_flex_stretch_cross_size(Style& style, int outer_size, bool row_direction) {
    if (row_direction) {
        style.height = flex_stretch_dimension(style, outer_size, true);
        style.height_percent = -1;
    } else {
        style.width = flex_stretch_dimension(style, outer_size, false);
        style.width_percent = -1;
    }
}

int flex_outer_cross_size(const LayoutBox& child, bool row_direction) {
    const int margins = row_direction
        ? flex_bounded_sum(child.style.margin.top, child.style.margin.bottom)
        : flex_bounded_sum(child.style.margin.left, child.style.margin.right);
    return flex_bounded_sum(row_direction ? child.rect.height : child.rect.width, margins);
}

std::vector<LayoutBox*> ordered_flex_children(LayoutBox& box) {
    if (box.style.display != Display::Flex) {
        return {};
    }
    const bool has_nonzero_order = std::any_of(box.children.begin(), box.children.end(), [](const LayoutBoxPtr& child) {
        return !is_out_of_flow_positioned(child->style) && child->style.flex_order != 0;
    });
    if (!has_nonzero_order) {
        return {};
    }
    std::vector<LayoutBox*> ordered;
    ordered.reserve(box.children.size());
    for (const LayoutBoxPtr& child : box.children) {
        if (!is_out_of_flow_positioned(child->style)) {
            ordered.push_back(child.get());
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const LayoutBox* left, const LayoutBox* right) {
        return left->style.flex_order < right->style.flex_order;
    });
    return ordered;
}

template <typename LayoutChildForWidth>
int layout_wrapped_flex_children(LayoutBox& box,
                                 int content_x,
                                 int content_y,
                                 int content_width,
                                 const LayoutChildForWidth& layout_child_for_width,
                                 const std::vector<LayoutBox*>& ordered_children) {
    struct FlexWrapLine {
        int y = 0;
        int height = 0;
        std::vector<LayoutBox*> children;
    };
    const int fixed_height = specified_content_height(box.style, 0);
    const int minimum_height = std::max(0, specified_content_min_height(box.style, 0));
    const bool distribute_lines = box.style.align_content != JustifyContent::Start &&
        (fixed_height >= 0 || minimum_height > 0);
    std::vector<FlexWrapLine> lines;
    lines.reserve(box.children.size());
    lines.push_back(FlexWrapLine{content_y, 0, {}});
    int cursor_x = content_x;
    int line_y = content_y;
    int line_height = 0;
    const int max_line_width = std::max(1, content_width);
    const auto place_child = [&](LayoutBox& child) {
        const bool use_basis = child.style.flex_basis >= 0;
        layout_child_for_width(child, use_basis ? child.style.flex_basis : content_width, use_basis);
        const int child_width = bounded_add(
            bounded_add(child.rect.width, child.style.margin.left), child.style.margin.right);
        const int child_height = bounded_add(
            bounded_add(child.rect.height, child.style.margin.top), child.style.margin.bottom);
        const bool should_wrap = cursor_x > content_x &&
            bounded_add(cursor_x, child_width) > bounded_add(content_x, max_line_width);
        if (should_wrap) {
            lines.back().height = line_height;
            const int line_advance = bounded_add(std::max(1, line_height), box.style.row_gap);
            lines.push_back(FlexWrapLine{bounded_add(line_y, line_advance), 0, {}});
            line_y = bounded_add(line_y, line_advance);
            cursor_x = content_x;
            line_height = 0;
        }
        const int dx = bounded_subtract(bounded_add(cursor_x, child.style.margin.left), child.rect.x);
        const int dy = bounded_subtract(bounded_add(line_y, child.style.margin.top), child.rect.y);
        shift_box(child, dx, dy);
        lines.back().children.push_back(&child);
        cursor_x = bounded_add(cursor_x, bounded_add(child_width, box.style.column_gap));
        line_height = std::max(line_height, child_height);
    };
    if (ordered_children.empty()) {
        for (const LayoutBoxPtr& child : box.children) {
            if (!is_out_of_flow_positioned(child->style)) {
                place_child(*child);
            }
        }
    } else {
        for (LayoutBox* child : ordered_children) {
            place_child(*child);
        }
    }
    const int natural_height = bounded_add(bounded_subtract(line_y, content_y), std::max(0, line_height));
    lines.back().height = line_height;

    // A wrapped line has its own cross-axis alignment context. Relayout only
    // auto-sized stretch items; explicit dimensions remain authoritative.
    for (FlexWrapLine& line : lines) {
        const int line_cross_size = std::max(1, line.height);
        for (LayoutBox* child : line.children) {
            const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
            if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, true)) {
                const int old_height = child->style.height;
                const int old_height_percent = child->style.height_percent;
                set_flex_stretch_cross_size(child->style, line_cross_size, true);
                layout_child_for_width(*child, std::max(0, child->rect.width), false);
                child->style.height = old_height;
                child->style.height_percent = old_height_percent;
            }
            const int child_outer_height = flex_outer_cross_size(*child, true);
            int target_y = line.y;
            if (alignment == AlignItems::Center) {
                target_y = bounded_add(target_y, std::max(0, (line_cross_size - child_outer_height) / 2));
            } else if (alignment == AlignItems::End) {
                target_y = bounded_add(target_y, std::max(0, line_cross_size - child_outer_height));
            }
            const int dy = bounded_subtract(bounded_add(target_y, child->style.margin.top), child->rect.y);
            if (dy != 0) {
                shift_box(*child, 0, dy);
            }
        }
    }
    if (!distribute_lines) {
        return natural_height;
    }

    const int container_height = fixed_height >= 0 ? fixed_height : std::max(minimum_height, natural_height);
    const FlexJustifyPlacement placement = flex_justify_placement(box.style.align_content,
                                                                  content_y,
                                                                  container_height,
                                                                  natural_height,
                                                                  static_cast<int>(lines.size()));
    int target_y = placement.cursor;
    for (FlexWrapLine& line : lines) {
        const int dy = target_y - line.y;
        if (dy != 0) {
            for (LayoutBox* child : line.children) {
                shift_box(*child, 0, dy);
            }
        }
        target_y = bounded_add(target_y,
                               bounded_add(std::max(1, line.height),
                                           bounded_add(box.style.row_gap, placement.extra_gap)));
    }
    return container_height;
}

constexpr int kMaxGridColumns = 32;
constexpr int kMaxGridRows = 128;

struct GridColumns {
    int count = 1;
    int gap = 0;
    std::vector<int> widths;
};

GridColumns resolve_grid_columns(const Style& style, int content_width, int in_flow_count) {
    GridColumns columns;
    columns.gap = std::max(0, style.column_gap);
    if (style.grid_template_column_count > 0) {
        columns.count = std::min(style.grid_template_column_count, kMaxGridColumns);
        columns.widths.assign(static_cast<std::size_t>(columns.count), 0);
        int fixed_width = 0;
        int flexible_count = 0;
        for (int column = 0; column < columns.count; ++column) {
            const int width = style.grid_template_column_widths[static_cast<std::size_t>(column)];
            if (width > 0) {
                columns.widths[static_cast<std::size_t>(column)] = width;
                fixed_width += width;
            } else {
                ++flexible_count;
            }
        }
        const int total_gap_width = columns.gap * std::max(0, columns.count - 1);
        const int flexible_width = flexible_count > 0
            ? std::max(1, (content_width - fixed_width - total_gap_width) / flexible_count)
            : 0;
        for (int& width : columns.widths) {
            if (width <= 0) {
                width = flexible_width;
            }
        }
    } else {
        const int min_track = std::max(1, style.grid_min_track_width > 0 ? style.grid_min_track_width : content_width);
        columns.count = std::max(1, (content_width + columns.gap) / (min_track + columns.gap));
        columns.count = std::min(columns.count, in_flow_count);
        columns.count = std::min(columns.count, kMaxGridColumns);
        const int total_gap_width = columns.gap * std::max(0, columns.count - 1);
        const int column_width = std::max(1, (content_width - total_gap_width) / columns.count);
        columns.widths.assign(static_cast<std::size_t>(columns.count), column_width);
    }
    return columns;
}

int grid_item_width(const GridColumns& columns, int column, int span) {
    int width = 0;
    for (int offset = 0; offset < span; ++offset) {
        width += columns.widths[static_cast<std::size_t>(column + offset)];
    }
    width += columns.gap * std::max(0, span - 1);
    return std::max(1, width);
}

int grid_column_x(const GridColumns& columns, int content_x, int column) {
    int offset = 0;
    for (int i = 0; i < column; ++i) {
        offset += columns.widths[static_cast<std::size_t>(i)] + columns.gap;
    }
    return content_x + offset;
}

struct GridPlacement {
    LayoutBox* child = nullptr;
    int row = 0;
    int column = 0;
    int column_span = 1;
    int row_span = 1;
    bool fallback_block = false;
};

struct GridPlacementState {
    std::vector<std::uint64_t> occupied;
    std::vector<int> row_heights;
    std::vector<bool> row_fixed;
};

std::uint64_t grid_occupancy_mask(int column, int column_span) {
    return ((std::uint64_t{1} << column_span) - 1U) << column;
}

void ensure_grid_rows(GridPlacementState& state, int rows, int min_row_height) {
    const int bounded_rows = std::min(rows, kMaxGridRows);
    while (static_cast<int>(state.occupied.size()) < bounded_rows) {
        state.occupied.push_back(0);
        state.row_heights.push_back(std::max(0, min_row_height));
        state.row_fixed.push_back(false);
    }
}

bool can_place_grid_item(const GridPlacementState& state,
                         int column_count,
                         int row,
                         int column,
                         int column_span,
                         int row_span) {
    if (column + column_span > column_count) {
        return false;
    }
    const std::uint64_t mask = grid_occupancy_mask(column, column_span);
    for (int r = row; r < row + row_span; ++r) {
        if (r < static_cast<int>(state.occupied.size()) &&
            (state.occupied[static_cast<std::size_t>(r)] & mask) != 0) {
            return false;
        }
    }
    return true;
}

void mark_grid_item_occupied(GridPlacementState& state,
                             int row,
                             int column,
                             int column_span,
                             int row_span) {
    const std::uint64_t mask = grid_occupancy_mask(column, column_span);
    for (int r = row; r < row + row_span; ++r) {
        state.occupied[static_cast<std::size_t>(r)] |= mask;
    }
}

GridPlacement place_grid_item(GridPlacementState& state,
                              int column_count,
                              int column_span,
                              int row_span,
                              int min_row_height,
                              int requested_column,
                              int requested_row) {
    GridPlacement placement;
    placement.column_span = column_span;
    placement.row_span = row_span;

    // The occupancy grid has a fixed, explicit memory bound. Do not clamp a
    // later row back onto the final tracked row: that creates overlapping
    // boxes and may index past the retained row vectors for large spans.
    if (row_span > kMaxGridRows ||
        (requested_row >= 0 &&
         (requested_row >= kMaxGridRows || requested_row > kMaxGridRows - row_span))) {
        placement.fallback_block = true;
        return placement;
    }

    if (requested_column >= 0) {
        placement.column = std::min(requested_column, std::max(0, column_count - column_span));
    }
    if (requested_row >= 0) {
        placement.row = requested_row;
    }

    if (requested_row >= 0 && requested_column >= 0) {
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
        return placement;
    }

    if (requested_row >= 0) {
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        for (int column = 0; column <= column_count - column_span; ++column) {
            if (can_place_grid_item(state, column_count, placement.row, column, column_span, row_span)) {
                placement.column = column;
                mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
                return placement;
            }
        }
        mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
        return placement;
    }

    bool placed = false;
    while (!placed) {
        if (placement.row + row_span > kMaxGridRows) {
            placement.fallback_block = true;
            return placement;
        }
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        const int column_begin = requested_column >= 0 ? placement.column : 0;
        const int column_end = requested_column >= 0 ? placement.column : column_count - column_span;
        for (int column = column_begin; column <= column_end; ++column) {
            if (can_place_grid_item(state, column_count, placement.row, column, column_span, row_span)) {
                placement.column = column;
                placed = true;
                break;
            }
        }
        if (!placed) {
            ++placement.row;
        }
    }
    ensure_grid_rows(state, placement.row + row_span, min_row_height);
    mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
    return placement;
}
#endif

bool participates_in_inline_flow(const LayoutBox& box) {
    return (box.node != nullptr && box.node->type == NodeType::Text) ||
        box.style.display == Display::Inline || box.style.display == Display::InlineBlock;
}

bool has_only_inline_children(const LayoutBox& box) {
    if (box.children.empty()) {
        return false;
    }
    bool has_in_flow_child = false;
    for (const auto& child : box.children) {
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        has_in_flow_child = true;
        if (!participates_in_inline_flow(*child)) {
            return false;
        }
    }
    return has_in_flow_child;
}

void shift_box(LayoutBox& box, int dx, int dy) {
    box.rect.x = bounded_add(box.rect.x, dx);
    box.rect.y = bounded_add(box.rect.y, dy);
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
    return layout(root, viewport_width, 240);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, int viewport_height) const {
    RenderTreeBuilder render_tree_builder(style_resolver_);
    auto render_tree = render_tree_builder.build(root);
    return layout(*render_tree, viewport_width, viewport_height);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, MonotonicArena& arena) const {
    return layout(root, viewport_width, 240, arena);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, int viewport_height, MonotonicArena& arena) const {
    RenderTreeBuilder render_tree_builder(style_resolver_);
    auto render_tree = render_tree_builder.build(root, arena);
    return layout(*render_tree, viewport_width, viewport_height, arena);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width) const {
    return layout(render_tree, viewport_width, 240);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width, int viewport_height) const {
    return build_with_arena(render_tree, viewport_width, viewport_height, nullptr);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const {
    return layout(render_tree, viewport_width, 240, arena);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree,
                                  int viewport_width,
                                  int viewport_height,
                                  MonotonicArena& arena) const {
    return build_with_arena(render_tree, viewport_width, viewport_height, &arena);
}

LayoutBoxPtr LayoutEngine::build_with_arena(const RenderObject& render_tree,
                                            int viewport_width,
                                            int viewport_height,
                                            MonotonicArena* arena) const {
    auto root_box = make_layout_box(arena);
    root_box->node = render_tree.node;
    root_box->style = render_tree.style;
    build_layout_tree(render_tree, *root_box, arena);
    root_box->rect.height = layout_box(*root_box, 0, 0, viewport_width, viewport_height, 1);
    return root_box;
}

void LayoutEngine::build_layout_tree(const RenderObject& object, LayoutBox& box, MonotonicArena* arena) const {
    struct PendingObject {
        const RenderObject* object = nullptr;
        LayoutBox* box = nullptr;
    };

    const std::size_t max_layout_boxes = std::max<std::size_t>(1, options_.max_layout_boxes);
    std::size_t layout_box_count = 1;
    bool budget_reported = false;
    std::vector<PendingObject> pending;
    pending.push_back(PendingObject{&object, &box});
    while (!pending.empty()) {
        const PendingObject current = pending.back();
        pending.pop_back();
        const RenderObject& render_object = *current.object;
        LayoutBox& layout_box = *current.box;
        std::vector<PendingObject> child_work;
        child_work.reserve(render_object.children.size());
        for (const auto& child : render_object.children) {
            if (layout_box_count >= max_layout_boxes) {
                if (!budget_reported) {
                    report_diagnostic(options_.diagnostics,
                                      DiagnosticStage::Layout,
                                      DiagnosticSeverity::Warning,
                                      "layout-box-limit",
                                      "Layout box budget was reached; remaining render objects were skipped",
                                      "Increase max_layout_boxes or simplify nested layout.");
                    budget_reported = true;
                }
                break;
            }
            auto child_box = make_layout_box(arena);
            LayoutBox* child_box_raw = child_box.get();
            ++layout_box_count;
            child_box->node = child->node;
            child_box->style = child->style;
            layout_box.children.push_back(std::move(child_box));
            child_work.push_back(PendingObject{child.get(), child_box_raw});
        }
        for (auto it = child_work.rbegin(); it != child_work.rend(); ++it) {
            pending.push_back(*it);
        }
    }
}

LayoutBoxPtr LayoutEngine::make_layout_box(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
    }
    return LayoutBoxPtr(&arena->create<LayoutBox>(), LayoutBoxDeleter{true});
}

int LayoutEngine::layout_box(LayoutBox& box, int x, int y, int width, int height, std::size_t depth) const {
    if (depth > std::max<std::size_t>(1, options_.max_layout_depth)) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          "layout-depth-limit",
                          "Layout depth budget was reached; remaining nested layout was skipped",
                          "Increase max_layout_depth or simplify deeply nested layout.");
        box.rect = Rect{x, y, 0, 0};
        return 0;
    }

    const int margin_left = box.style.margin_left_auto ? 0 : box.style.margin.left;
    const int margin_right = box.style.margin_right_auto ? 0 : box.style.margin.right;
    const int border_box_y = bounded_add(y, box.style.margin.top);
    const int containing_content_width = bounded_non_negative_subtract(
        bounded_subtract(width, margin_left), margin_right);
    const int available_content_width = resolved_content_width(box.style, containing_content_width);
    int content_width = available_content_width;
    const int max_content_width = resolved_max_content_width(box.style, containing_content_width);
    if (max_content_width >= 0) {
        content_width = std::min(content_width, max_content_width);
    }
    const int measured_border_box_width = bounded_non_negative_add(
        bounded_non_negative_add(content_width, horizontal_edges(box.style.padding)),
        horizontal_edges(box.style.border_width));
    const int min_width = resolved_min_width(box.style, containing_content_width);
    const bool fixed_border_box_width =
        box.style.box_sizing_border_box && (box.style.width >= 0 || box.style.width_percent >= 0);
    const int fixed_border_box_width_px = box.style.width_percent >= 0
        ? resolve_percent(containing_content_width, box.style.width_percent)
        : box.style.width;
    int preferred_border_box_width = fixed_border_box_width ? fixed_border_box_width_px : measured_border_box_width;
    if (max_content_width >= 0) {
        preferred_border_box_width = std::min(preferred_border_box_width, measured_border_box_width);
    }
    int border_box_width = std::max(min_width, preferred_border_box_width);
    const int auto_space = bounded_non_negative_subtract(
        bounded_non_negative_subtract(
            bounded_non_negative_subtract(width, border_box_width), margin_left), margin_right);
    int border_box_x = bounded_add(x, margin_left);
    if (box.style.margin_left_auto && box.style.margin_right_auto) {
        border_box_x = bounded_add(x, auto_space / 2);
    } else if (box.style.margin_left_auto) {
        border_box_x = bounded_add(x, auto_space);
    }
    const int content_x = bounded_add(bounded_add(border_box_x, box.style.border_width.left),
                                      box.style.padding.left);
    const int content_y = bounded_add(bounded_add(border_box_y, box.style.border_width.top),
                                      box.style.padding.top);
    int cursor_y = content_y;

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        return layout_text_box(box, border_box_x, border_box_y, content_width, min_width, height);
    }

    int max_child_width = 0;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    const int children_height = box.style.display == Display::Flex
        ? layout_flex_box(box, content_x, cursor_y, content_width, height, depth)
        : box.style.display == Display::Grid
        ? layout_grid_box(box, content_x, cursor_y, content_width, height, depth)
        : has_only_inline_children(box)
#else
    const int children_height = has_only_inline_children(box)
#endif
        ? layout_inline_children(box, content_x, cursor_y, content_width, depth)
        : [&] {
            int flow_height = 0;
            for (auto& child : box.children) {
                if (is_out_of_flow_positioned(child->style)) {
                    continue;
                }
                const int child_height = layout_box(*child, content_x, cursor_y, content_width, height, depth + 1);
                cursor_y += child_height;
                flow_height += child_height;
                const int child_left = safe_edge(child->rect.x, -child->style.margin.left);
                const int child_right = safe_edge(safe_edge(child->rect.x, child->rect.width), child->style.margin.right);
                max_child_width = std::max(max_child_width, child_right - child_left);
            }
            return flow_height;
        }();
    if (box.style.width < 0 && box.style.width_percent < 0 &&
        (box.style.display == Display::Inline || box.style.display == Display::InlineBlock)) {
        if (!box.children.empty()) {
            int min_child_x = safe_edge(box.children.front()->rect.x, -box.children.front()->style.margin.left);
            int max_child_x = safe_edge(safe_edge(box.children.front()->rect.x, box.children.front()->rect.width),
                                        box.children.front()->style.margin.right);
            for (const auto& child : box.children) {
                min_child_x = std::min(min_child_x, safe_edge(child->rect.x, -child->style.margin.left));
                max_child_x = std::max(max_child_x,
                    safe_edge(safe_edge(child->rect.x, child->rect.width), child->style.margin.right));
            }
            max_child_width = std::max(0, max_child_x - min_child_x);
        }
        content_width = std::min(available_content_width, max_child_width);
        border_box_width = std::max(min_width,
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
        ? std::max(1, static_cast<int>(std::min<std::int64_t>(
              std::numeric_limits<int>::max(),
              (static_cast<std::int64_t>(content_width) * box.style.aspect_ratio_height +
               box.style.aspect_ratio_width / 2) / box.style.aspect_ratio_width)))
        : 0;
    const int fixed_content_height = specified_content_height(box.style, height);
    int content_height = std::max(specified_content_min_height(box.style, height),
        fixed_content_height >= 0
            ? fixed_content_height
            : std::max({children_height, intrinsic_control_height, aspect_ratio_height}));
    const int max_content_height = resolved_max_content_height(box.style, height);
    if (max_content_height >= 0) {
        content_height = std::min(content_height, max_content_height);
    }
    const int border_box_height = bounded_non_negative_add(
        bounded_non_negative_add(vertical_edges(box.style.border_width), vertical_edges(box.style.padding)),
        content_height);
    const int total_height = bounded_add(bounded_add(box.style.margin.top, border_box_height),
                                         box.style.margin.bottom);
    box.rect = Rect{border_box_x, border_box_y, border_box_width, border_box_height};
    layout_positioned_children(box, content_x, content_y, content_width, content_height, width, depth);
    apply_relative_position_offset(box);
    return total_height;
}

int LayoutEngine::layout_text_box(LayoutBox& box,
                                  int border_box_x,
                                  int border_box_y,
                                  int content_width,
                                  int min_width,
                                  int height) const {
    const std::string text = transformed_render_text(*box.node, box.style.text_transform);
    const TextMetrics metrics = measure_text_with_letter_spacing(text_measure_,
                                                                 text,
                                                                 box.style.font_size,
                                                                 box.style.font_weight,
                                                                 box.style.font_family_hash,
                                                                 box.style.letter_spacing);
    const int raw_text_width = metrics.width;
    const int text_indent = std::max(0, std::min(box.style.text_indent, content_width));
    const int usable_text_width = std::max(0, content_width - text_indent);
    const int text_width = std::max(min_width, std::min(usable_text_width, raw_text_width));
    const int line_height = box.style.line_height > 0 ? box.style.line_height : metrics.line_height;
    const bool can_wrap = !box.style.white_space_nowrap &&
        (box.style.overflow_wrap_anywhere || has_text_wrap_opportunity(text));
    if (usable_text_width > 0 && raw_text_width > usable_text_width &&
        (box.style.white_space_nowrap || box.style.text_overflow_ellipsis || !can_wrap)) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          box.style.text_overflow_ellipsis ? "layout-text-overflow-ellipsis" : "layout-text-overflow",
                          box.style.text_overflow_ellipsis
                              ? "Text measured wider than its layout box and will be truncated with an ellipsis"
                              : "Text measured wider than its layout box and will be clipped or visually degraded",
                          text_overflow_detail(box,
                                               text,
                                               raw_text_width,
                                               usable_text_width,
                                               content_width,
                                               text_indent));
    }
    int line_count = 1;
    if (can_wrap && usable_text_width > 0) {
        const std::vector<std::string> lines = box.style.overflow_wrap_anywhere
            ? wrap_text_anywhere(text_measure_,
                                 text,
                                 box.style.font_size,
                                 box.style.font_weight,
                                 box.style.font_family_hash,
                                 box.style.letter_spacing,
                                 usable_text_width)
            : wrap_text_at_opportunities(text_measure_,
                                         text,
                                         box.style.font_size,
                                         box.style.font_weight,
                                         box.style.font_family_hash,
                                         box.style.letter_spacing,
                                         usable_text_width);
        line_count = clamp_layout_value(static_cast<std::int64_t>(std::min<std::size_t>(
            lines.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))));
        line_count = std::max(1, line_count);
    }
    const int fixed_text_height = specified_content_height(box.style, height);
    int text_height = std::max(specified_content_min_height(box.style, height),
        fixed_text_height >= 0
            ? fixed_text_height
            : bounded_non_negative_multiply(std::max(0, line_height), line_count));
    const int max_text_height = resolved_max_content_height(box.style, height);
    if (max_text_height >= 0) {
        text_height = std::min(text_height, max_text_height);
    }
    int text_x = bounded_add(border_box_x, text_indent);
    if (box.style.text_align == TextAlign::Center) {
        text_x = bounded_add(text_x, std::max(0, (usable_text_width - text_width) / 2));
    } else if (box.style.text_align == TextAlign::End) {
        text_x = bounded_add(text_x, std::max(0, usable_text_width - text_width));
    }
    box.rect = Rect{text_x, border_box_y, text_width, text_height};
    return text_height;
}

int LayoutEngine::layout_inline_children(LayoutBox& box,
                                         int content_x,
                                         int content_y,
                                         int content_width,
                                         std::size_t depth) const {
    int cursor_x = bounded_add(content_x, std::max(0, std::min(box.style.text_indent, content_width)));
    int line_y = content_y;
    int line_height = 0;
    int line_start_x = cursor_x;
    std::size_t line_start_index = 0;

    const auto finish_line = [&](std::size_t line_end_index, int used_width) {
        if (line_end_index <= line_start_index || used_width <= 0) {
            return;
        }
        int dx = 0;
        const int line_capacity = std::max(0,
            bounded_subtract(bounded_add(content_x, content_width), line_start_x));
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
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        layout_box(*child, 0, 0, content_width, 0, depth + 1);
        const int child_outer_width = bounded_add(
            bounded_add(child->style.margin.left, child->rect.width), child->style.margin.right);
        const int child_outer_height = bounded_add(
            bounded_add(child->style.margin.top, child->rect.height), child->style.margin.bottom);
        const int remaining_width = std::max(0,
            bounded_subtract(bounded_add(content_x, content_width), cursor_x));

        if (cursor_x > line_start_x && child_outer_width > remaining_width) {
            finish_line(index, cursor_x - line_start_x);
            line_y = bounded_add(line_y, std::max(1, line_height));
            line_height = 0;
            cursor_x = content_x;
            line_start_x = cursor_x;
            line_start_index = index;
        }

        const int target_x = bounded_add(cursor_x, child->style.margin.left);
        const int target_y = bounded_add(line_y, child->style.margin.top);
        shift_box(*child, bounded_subtract(target_x, child->rect.x),
                  bounded_subtract(target_y, child->rect.y));
        cursor_x = bounded_add(cursor_x, child_outer_width);
        line_height = std::max(line_height, child_outer_height);
    }

    finish_line(box.children.size(), cursor_x - line_start_x);
    return bounded_add(bounded_subtract(line_y, content_y), std::max(0, line_height));
}

void LayoutEngine::layout_positioned_children(LayoutBox& box,
                                              int content_x,
                                              int content_y,
                                              int content_width,
                                              int content_height,
                                              int viewport_width,
                                              std::size_t depth) const {
    for (auto& child : box.children) {
        if (!is_out_of_flow_positioned(child->style)) {
            continue;
        }

        const bool fixed = child->style.position == "fixed";
        const int area_x = fixed ? 0 : content_x;
        const int area_y = fixed ? 0 : content_y;
        const int area_width = std::max(1, fixed ? viewport_width : content_width);
        const int area_height = fixed ? 0 : content_height;

        const int original_width = child->style.width;
        const int original_width_percent = child->style.width_percent;
        const bool original_box_sizing = child->style.box_sizing_border_box;
        if (child->style.width < 0 && child->style.width_percent < 0 &&
            child->style.inset_left_specified && child->style.inset_right_specified) {
            child->style.width = std::max(0, area_width - child->style.inset_left - child->style.inset_right -
                                             child->style.margin.left - child->style.margin.right);
            child->style.width_percent = -1;
            child->style.box_sizing_border_box = true;
        }

        layout_box(*child, 0, 0, area_width, area_height, depth + 1);
        child->style.width = original_width;
        child->style.width_percent = original_width_percent;
        child->style.box_sizing_border_box = original_box_sizing;

        const int target_x = area_x + horizontal_position_offset(child->style, area_width, child->rect.width);
        const int target_y = area_y + vertical_position_offset(child->style, area_height, child->rect.height);
        shift_box(*child, target_x - child->rect.x, target_y - child->rect.y);
    }
}

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
int LayoutEngine::layout_flex_box(LayoutBox& box,
                                  int content_x,
                                  int content_y,
                                  int content_width,
                                  int containing_height,
                                  std::size_t depth) const {
    const auto in_flow_count = static_cast<int>(std::count_if(box.children.begin(), box.children.end(),
        [](const LayoutBoxPtr& child) { return !is_out_of_flow_positioned(child->style); }));
    if (in_flow_count == 0) {
        return 0;
    }
    const std::vector<LayoutBox*> ordered_children = ordered_flex_children(box);
    const auto for_each_in_flow_child = [&](const auto& callback) {
        if (ordered_children.empty()) {
            for (const LayoutBoxPtr& child : box.children) {
                if (!is_out_of_flow_positioned(child->style)) {
                    callback(*child);
                }
            }
        } else {
            for (LayoutBox* child : ordered_children) {
                callback(*child);
            }
        }
    };

    if (box.style.flex_direction == FlexDirection::Column) {
        struct ColumnFlexItem {
            LayoutBox* child = nullptr;
            int base_height = 0;
            int target_height = 0;
            bool force_height = false;
        };

        const int fixed_height = specified_content_height(box.style, containing_height);
        const int minimum_height = std::max(0, specified_content_min_height(box.style, containing_height));
        const int child_containing_height = fixed_height >= 0 ? fixed_height : 0;
        const auto layout_child_for_size = [&](LayoutBox& child, int target_height, bool force_height) {
            const int old_height = child.style.height;
            const int old_height_percent = child.style.height_percent;
            if (force_height) {
                child.style.height = std::max(0, target_height);
                child.style.height_percent = -1;
            }
            const int child_height = layout_box(child, 0, 0, content_width, child_containing_height, depth + 1);
            child.style.height = old_height;
            child.style.height_percent = old_height_percent;
            return child_height;
        };

        std::vector<ColumnFlexItem> items;
        items.reserve(box.children.size());
        int total_base_height = 0;
        int total_margin_height = 0;
        int total_grow = 0;
        std::int64_t total_shrink_weight = 0;

        for_each_in_flow_child([&](LayoutBox& child) {
            const bool use_basis = child.style.flex_basis >= 0;
            const bool flexible_zero_basis = !use_basis && child.style.flex_grow > 0 && child.style.height < 0;
            const int probe_height = use_basis ? child.style.flex_basis : flexible_zero_basis ? 0 : 0;
            const bool force_height = use_basis || flexible_zero_basis;
            layout_child_for_size(child, probe_height, force_height);

            int base_height = use_basis ? child.style.flex_basis : flexible_zero_basis ? 0 : child.rect.height;
            if (child.style.height >= 0 && !use_basis) {
                base_height = child.style.height;
            }
            base_height = std::max(std::max(0, child.style.min_height), base_height);
            ColumnFlexItem item;
            item.child = &child;
            item.base_height = base_height;
            item.target_height = base_height;
            item.force_height = force_height;
            items.push_back(item);

            total_base_height += base_height;
            total_margin_height += child.style.margin.top + child.style.margin.bottom;
            total_grow += std::max(0, child.style.flex_grow);
            total_shrink_weight += static_cast<std::int64_t>(std::max(0, child.style.flex_shrink)) *
                std::max(1, base_height);
        });

        const int total_gap_height = bounded_non_negative_multiply(
            box.style.row_gap, std::max(0, in_flow_count - 1));
        const int natural_height = total_base_height + total_margin_height + total_gap_height;
        const int container_height = fixed_height >= 0 ? fixed_height : std::max(minimum_height, natural_height);
        const int available_item_height = std::max(0, container_height - total_margin_height - total_gap_height);
        const int free_space = available_item_height - total_base_height;

        if (free_space > 0 && total_grow > 0) {
            int remaining_space = free_space;
            int remaining_grow = total_grow;
            for (ColumnFlexItem& item : items) {
                const int grow = std::max(0, item.child->style.flex_grow);
                if (grow <= 0) {
                    continue;
                }
                const int share = remaining_grow == grow
                    ? remaining_space
                    : static_cast<int>((static_cast<std::int64_t>(remaining_space) * grow) / remaining_grow);
                remaining_space -= share;
                remaining_grow -= grow;
                item.target_height = std::max(0, item.base_height + share);
                item.force_height = true;
            }
        } else if (free_space < 0 && total_shrink_weight > 0) {
            int remaining_deficit = -free_space;
            std::int64_t remaining_weight = total_shrink_weight;
            for (ColumnFlexItem& item : items) {
                const std::int64_t weight = static_cast<std::int64_t>(std::max(0, item.child->style.flex_shrink)) *
                    std::max(1, item.base_height);
                if (weight <= 0) {
                    continue;
                }
                const int share = remaining_weight == weight
                    ? remaining_deficit
                    : static_cast<int>((static_cast<std::int64_t>(remaining_deficit) * weight) / remaining_weight);
                remaining_deficit -= share;
                remaining_weight -= weight;
                item.target_height = std::max(std::max(0, item.child->style.min_height), item.base_height - share);
                item.force_height = true;
            }
        }

        int total_child_height = total_margin_height + total_gap_height;
        for (ColumnFlexItem& item : items) {
            layout_child_for_size(*item.child, item.target_height, item.force_height);
            total_child_height += item.child->rect.height;
        }

        const FlexJustifyPlacement placement = flex_justify_placement(box.style.justify_content,
                                                                      content_y,
                                                                      container_height,
                                                                      total_child_height,
                                                                      in_flow_count);
        int cursor_y = placement.cursor;
        for (ColumnFlexItem& item : items) {
            LayoutBox* child = item.child;
            const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
            if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, false)) {
                const int old_width = child->style.width;
                const int old_width_percent = child->style.width_percent;
                set_flex_stretch_cross_size(child->style, content_width, false);
                layout_child_for_size(*child, item.target_height, item.force_height);
                child->style.width = old_width;
                child->style.width_percent = old_width_percent;
            }
            const int target_x = flex_aligned_x(alignment,
                                                content_x,
                                                content_width,
                                                flex_outer_cross_size(*child, false));
            const int dx = target_x + child->style.margin.left - child->rect.x;
            const int dy = cursor_y + child->style.margin.top - child->rect.y;
            shift_box(*child, dx, dy);
            cursor_y += child->rect.height + child->style.margin.top + child->style.margin.bottom +
                placement.extra_gap + box.style.row_gap;
        }
        return container_height;
    }

    const auto layout_child_for_width = [&](LayoutBox& child, int target_width, bool force_width) {
        const int old_width = child.style.width;
        const int old_width_percent = child.style.width_percent;
        if (force_width) {
            child.style.width = std::max(0, target_width);
            child.style.width_percent = -1;
        }
        const int height = layout_box(child, 0, 0, std::max(0, target_width), 0, depth + 1);
        child.style.width = old_width;
        child.style.width_percent = old_width_percent;
        return height;
    };

    if (box.style.flex_wrap) {
        return layout_wrapped_flex_children(box, content_x, content_y, content_width,
                                            layout_child_for_width, ordered_children);
    }

    std::vector<FlexLayoutItem> items;
    items.reserve(box.children.size());
    int total_base_width = 0;
    int total_margin_width = 0;
    int max_child_height = 0;
    int total_grow = 0;
    std::int64_t total_shrink_weight = 0;

    for_each_in_flow_child([&](LayoutBox& child) {
        const bool use_basis = child.style.flex_basis >= 0;
        const bool flexible_zero_basis = !use_basis && child.style.flex_grow > 0 && child.style.width < 0;
        const int probe_width = use_basis ? child.style.flex_basis : flexible_zero_basis ? 0 : content_width;
        const bool force_width = use_basis || flexible_zero_basis;
        layout_child_for_width(child, probe_width, force_width);

        int base_width = use_basis ? child.style.flex_basis : flexible_zero_basis ? 0 : child.rect.width;
        if (child.style.width >= 0 && !use_basis) {
            base_width = child.style.width;
        }
        base_width = std::max(std::max(0, child.style.min_width), base_width);
        FlexLayoutItem item;
        item.child = &child;
        item.base_width = base_width;
        item.target_width = base_width;
        item.force_width = force_width;
        items.push_back(item);

        total_base_width += base_width;
        total_margin_width += child.style.margin.left + child.style.margin.right;
        total_grow += flex_grow_factor(item);
        total_shrink_weight += flex_shrink_weight(item);
    });

    const int total_gap_width = bounded_non_negative_multiply(
        box.style.column_gap, std::max(0, in_flow_count - 1));
    const int available_item_width = std::max(0, content_width - total_margin_width - total_gap_width);
    const int free_space = available_item_width - total_base_width;
    distribute_flex_row_widths(items, free_space, total_grow, total_shrink_weight);

    int total_child_width = total_margin_width + total_gap_width;
    for (FlexLayoutItem& item : items) {
        layout_child_for_width(*item.child, item.target_width, item.force_width);
        total_child_width += item.child->rect.width;
        max_child_height = std::max(max_child_height,
            item.child->rect.height + item.child->style.margin.top + item.child->style.margin.bottom);
    }

    const FlexJustifyPlacement placement = flex_justify_placement(box.style.justify_content,
                                                                  content_x,
                                                                  content_width,
                                                                  total_child_width,
                                                                  in_flow_count);
    int cursor_x = placement.cursor;

    const int container_height = std::max(box.style.min_height, box.style.height >= 0 ? box.style.height : max_child_height);
    for (FlexLayoutItem& item : items) {
        LayoutBox* child = item.child;
        const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
        if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, true)) {
            const int old_height = child->style.height;
            const int old_height_percent = child->style.height_percent;
            set_flex_stretch_cross_size(child->style, container_height, true);
            layout_child_for_width(*child, item.target_width, item.force_width);
            child->style.height = old_height;
            child->style.height_percent = old_height_percent;
        }
        const int child_outer_height = flex_outer_cross_size(*child, true);
        const int target_y = flex_aligned_y(alignment,
                                            content_y,
                                            container_height,
                                            child_outer_height) + child->style.margin.top;

        const int dx = cursor_x + child->style.margin.left - child->rect.x;
        const int dy = target_y - child->rect.y;
        shift_box(*child, dx, dy);
        cursor_x += child->rect.width + child->style.margin.left + child->style.margin.right +
            placement.extra_gap + box.style.column_gap;
    }

    return container_height;
}

int LayoutEngine::layout_grid_box(LayoutBox& box,
                                  int content_x,
                                  int content_y,
                                  int content_width,
                                  int containing_height,
                                  std::size_t depth) const {
    const auto in_flow_count = static_cast<int>(std::count_if(box.children.begin(), box.children.end(),
        [](const LayoutBoxPtr& child) { return !is_out_of_flow_positioned(child->style); }));
    if (in_flow_count == 0) {
        return 0;
    }

    const int row_gap = std::max(0, box.style.row_gap);
    const GridColumns columns = resolve_grid_columns(box.style, content_width, in_flow_count);

    GridPlacementState placement_state;
    if (box.style.grid_template_row_count > 0) {
        const int template_rows = std::min({static_cast<int>(box.style.grid_template_row_count),
                                            static_cast<int>(box.style.grid_template_row_heights.size()),
                                            kMaxGridRows});
        placement_state.occupied.assign(static_cast<std::size_t>(template_rows), 0);
        placement_state.row_heights.reserve(static_cast<std::size_t>(template_rows));
        placement_state.row_fixed.reserve(static_cast<std::size_t>(template_rows));
        for (int row = 0; row < template_rows; ++row) {
            const int track = box.style.grid_template_row_heights[static_cast<std::size_t>(row)];
            placement_state.row_heights.push_back(std::max(0, track));
            placement_state.row_fixed.push_back(track > 0);
        }
        const int fixed_content_height = specified_content_height(box.style, containing_height);
        if (fixed_content_height >= 0) {
            int fixed_height = bounded_non_negative_multiply(row_gap, std::max(0, template_rows - 1));
            int flexible_count = 0;
            for (int row = 0; row < template_rows; ++row) {
                if (placement_state.row_fixed[static_cast<std::size_t>(row)]) {
                    fixed_height += placement_state.row_heights[static_cast<std::size_t>(row)];
                } else {
                    ++flexible_count;
                }
            }
            if (flexible_count > 0) {
                const int flexible_height = std::max(1, (fixed_content_height - fixed_height) / flexible_count);
                for (int row = 0; row < template_rows; ++row) {
                    if (!placement_state.row_fixed[static_cast<std::size_t>(row)]) {
                        placement_state.row_heights[static_cast<std::size_t>(row)] = flexible_height;
                    }
                }
            }
        }
    }
    std::vector<GridPlacement> placements;
    placements.reserve(static_cast<std::size_t>(in_flow_count));
    bool grid_placement_budget_reported = false;

    for (auto& child : box.children) {
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        const int column_span = std::max(1, std::min(static_cast<int>(child->style.grid_column_span), columns.count));
        const int row_span = std::max(1, static_cast<int>(child->style.grid_row_span));
        GridPlacement placement = place_grid_item(placement_state,
                                                  columns.count,
                                                  column_span,
                                                  row_span,
                                                  box.style.grid_auto_row_min,
                                                  child->style.grid_column_start,
                                                  child->style.grid_row_start);
        placement.child = child.get();

        if (placement.fallback_block) {
            if (!grid_placement_budget_reported) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Layout,
                                  DiagnosticSeverity::Warning,
                                  "grid-placement-budget",
                                  "Grid placement exceeded the bounded row budget; later items use block-flow fallback",
                                  "maximum tracked rows=" + std::to_string(kMaxGridRows));
                grid_placement_budget_reported = true;
            }
            layout_box(*child, 0, 0, content_width, 0, depth + 1);
            placements.push_back(placement);
            continue;
        }

        const int item_width = grid_item_width(columns, placement.column, column_span);
        const int original_width = child->style.width;
        const int original_width_percent = child->style.width_percent;
        const bool original_box_sizing = child->style.box_sizing_border_box;
        if (child->style.width < 0 && child->style.width_percent < 0) {
            child->style.width = item_width;
            child->style.width_percent = -1;
            child->style.box_sizing_border_box = true;
        }
        const int child_height = layout_box(*child, 0, 0, item_width, 0, depth + 1);
        child->style.width = original_width;
        child->style.width_percent = original_width_percent;
        child->style.box_sizing_border_box = original_box_sizing;
        const int min_allocated_height = box.style.grid_auto_row_min * row_span + row_gap * (row_span - 1);
        const int allocated_height = std::max(child_height, min_allocated_height);
        const int per_row_height = std::max(1, (allocated_height - row_gap * (row_span - 1) + row_span - 1) / row_span);
        for (int r = placement.row; r < placement.row + row_span; ++r) {
            if (!placement_state.row_fixed[static_cast<std::size_t>(r)]) {
                placement_state.row_heights[static_cast<std::size_t>(r)] =
                    std::max(placement_state.row_heights[static_cast<std::size_t>(r)], per_row_height);
            }
        }

        placements.push_back(placement);
    }

    std::vector<int> row_offsets(placement_state.row_heights.size(), 0);
    int total_height = 0;
    for (std::size_t row = 0; row < placement_state.row_heights.size(); ++row) {
        row_offsets[row] = total_height;
        total_height += placement_state.row_heights[row];
        if (row + 1 < placement_state.row_heights.size()) {
            total_height += row_gap;
        }
    }

    int fallback_y = total_height;
    const bool has_grid_rows = !placement_state.row_heights.empty();
    const bool has_fallback = std::any_of(placements.begin(), placements.end(),
                                          [](const GridPlacement& placement) {
                                              return placement.fallback_block;
                                          });
    if (has_grid_rows && has_fallback) {
        fallback_y += row_gap;
    }

    for (const GridPlacement& placement : placements) {
        if (placement.fallback_block) {
            const int target_x = content_x + placement.child->style.margin.left;
            const int target_y = content_y + fallback_y + placement.child->style.margin.top;
            shift_box(*placement.child, target_x - placement.child->rect.x, target_y - placement.child->rect.y);
            fallback_y += std::max(1, placement.child->rect.height +
                                          placement.child->style.margin.top +
                                          placement.child->style.margin.bottom) + row_gap;
            continue;
        }
        int allocated_height = 0;
        for (int r = 0; r < placement.row_span; ++r) {
            allocated_height += placement_state.row_heights[static_cast<std::size_t>(placement.row + r)];
        }
        allocated_height += row_gap * (placement.row_span - 1);
        const int target_x = grid_column_x(columns, content_x, placement.column) + placement.child->style.margin.left;
        const int target_y = content_y + row_offsets[static_cast<std::size_t>(placement.row)] +
            placement.child->style.margin.top;
        shift_box(*placement.child, target_x - placement.child->rect.x, target_y - placement.child->rect.y);
        placement.child->rect.width = grid_item_width(columns, placement.column, placement.column_span);
        if (placement.child->style.height < 0) {
            placement.child->rect.height = std::max(placement.child->rect.height, allocated_height);
        }
    }

    return has_fallback ? std::max(total_height, fallback_y - row_gap) : total_height;
}
#endif

std::size_t count_layout_boxes(const LayoutBox& root) {
    std::size_t count = 0;
    std::vector<const LayoutBox*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
