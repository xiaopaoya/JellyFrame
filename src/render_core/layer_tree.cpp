#include "render_core/layer_tree.h"

#include "render_core/animation_timeline.h"
#include "render_core/form_control.h"
#include "render_core/feature_config.h"
#include "render_core/flex_grid_paint.h"
#include "render_core/text_normalization.h"
#include "render_core/text_scan.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace jellyframe {
namespace {

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
constexpr int kConicGradientAreaWarningPixels = 65536;
constexpr int kRadialGradientAreaWarningPixels = 32768;
constexpr int kBoxShadowAreaWarningPixels = 98304;
#endif

bool has_border(const EdgeSizes& border) {
    return border.top > 0 || border.right > 0 || border.bottom > 0 || border.left > 0;
}

bool is_visible_background(Color color) {
    return color.a != 0;
}

Rect union_rect(Rect left, Rect right) {
    if (left.width <= 0 || left.height <= 0) {
        return right;
    }
    if (right.width <= 0 || right.height <= 0) {
        return left;
    }
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::max(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

Rect paint_rect_for(const LayoutBox& box) {
    if (box.style.display != Display::Inline || box.children.empty()) {
        return box.rect;
    }
    Rect rect{};
    for (const auto& child : box.children) {
        rect = union_rect(rect, child->rect);
    }
    return rect.width > 0 && rect.height > 0 ? rect : box.rect;
}

bool has_overflow_clip(const Style& style) {
    return style.overflow == "hidden" || style.overflow == "scroll" ||
        style.overflow == "auto" || style.overflow == "clip";
}

bool has_scrollable_overflow(const Style& style) {
    return style.overflow == "scroll" || style.overflow == "auto";
}

bool is_positioned(const Style& style) {
    return !style.position.empty();
}

bool has_transform(const Style& style) {
    return !style.transform.empty();
}

Transform2D parsed_transform_or_identity(const Style& style, DiagnosticSink* diagnostics) {
    Transform2D transform;
    if (style.transform.empty()) {
        return transform;
    }
    if (!parse_css_transform_2d(style.transform, transform)) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-transform-unsupported",
                          "Transform could not be applied by the supported 2D subset",
                          style.transform);
        return Transform2D{};
    }
    return transform;
}

bool has_shadow(const Style& style) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return style.box_shadow.enabled;
#else
    (void)style;
    return false;
#endif
}

bool has_text_shadow(const Style& style) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return style.text_shadow.enabled;
#else
    (void)style;
    return false;
#endif
}

int resolved_border_radius(const LayoutBox& box) {
    const int max_radius = std::max(0, std::min(box.rect.width, box.rect.height) / 2);
    if (box.style.border_radius_percent >= 0) {
        return std::min(max_radius,
                        (std::max(0, std::min(box.rect.width, box.rect.height)) *
                         box.style.border_radius_percent + 50) / 100);
    }
    CornerRadii radii = decode_corner_radii(box.style.border_radius);
    radii.top_left = std::min(max_radius, radii.top_left);
    radii.top_right = std::min(max_radius, radii.top_right);
    radii.bottom_right = std::min(max_radius, radii.bottom_right);
    radii.bottom_left = std::min(max_radius, radii.bottom_left);
    return encode_corner_radii(radii);
}

void push_fill_rect(DisplayList& display_list, Rect rect, Color color, int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
void push_linear_gradient(DisplayList& display_list,
                          Rect rect,
                          Color first,
                          Color second,
                          GradientAxis axis,
                          int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || (first.a == 0 && second.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::LinearGradient;
    command.rect = rect;
    command.color = first;
    command.color2 = second;
    command.gradient_axis = axis;
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}

void push_conic_gradient(DisplayList& display_list,
                         Rect rect,
                         Color first,
                         Color second,
                         int stop_percent,
                         int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || (first.a == 0 && second.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::ConicGradient;
    command.rect = rect;
    command.color = first;
    command.color2 = second;
    command.gradient_stop_percent = std::max(0, std::min(100, stop_percent));
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}

void push_box_shadow(DisplayList& display_list,
                     Rect rect,
                     Color color,
                     int border_radius,
                     int extent,
                     int blur) {
    if (rect.width <= 0 || rect.height <= 0 || color.a == 0 || extent <= 0) {
        return;
    }
    const std::int64_t extent64 = static_cast<std::int64_t>(extent);
    const std::int64_t twice_extent64 = extent64 * 2;
    const int twice_extent = static_cast<int>(std::clamp(twice_extent64,
                                                         static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                                         static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    DisplayCommand command;
    command.type = DisplayCommandType::BoxShadow;
    command.rect = Rect{
        safe_edge(rect.x, -extent),
        safe_edge(rect.y, -extent),
        safe_edge(rect.width, twice_extent),
        safe_edge(rect.height, twice_extent),
    };
    command.color = color;
    command.color2 = color;
    command.border_radius = expand_corner_radii(border_radius, extent);
    command.stroke_width = extent;
    command.gradient_stop_percent = blur;
    display_list.push_back(std::move(command));
}

void push_radial_gradient(DisplayList& display_list,
                          Rect rect,
                          Color center,
                          Color edge,
                          GradientAxis axis,
                          int packed_position,
                          int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || (center.a == 0 && edge.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::RadialGradient;
    command.rect = rect;
    command.color = center;
    command.color2 = edge;
    command.gradient_axis = axis;
    command.gradient_stop_percent = packed_position;
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}
#endif

void paint_background_paint(const BackgroundPaint& paint,
                            Rect rect,
                            int border_radius,
                            DisplayList& display_list,
                            const LayerTreeBuilderOptions& options) {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    (void)options;
#endif
    if (paint.kind == BackgroundPaintKind::LinearGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        push_linear_gradient(display_list, rect, paint.color, paint.color2, paint.axis, border_radius);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius);
#endif
    } else if (paint.kind == BackgroundPaintKind::ConicGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        const long long area = static_cast<long long>(std::max(0, rect.width)) * static_cast<long long>(std::max(0, rect.height));
        if (area > kConicGradientAreaWarningPixels) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-conic-gradient-area-budget", "conic-gradient() area is above the embedded progress-ring budget",
                              "area=" + std::to_string(area) + "px limit=" + std::to_string(kConicGradientAreaWarningPixels) + "px");
        }
        push_conic_gradient(display_list, rect, paint.color, paint.color2, paint.stop_percent, border_radius);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius);
#endif
    } else if (paint.kind == BackgroundPaintKind::RadialGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        const long long area = static_cast<long long>(std::max(0, rect.width)) * static_cast<long long>(std::max(0, rect.height));
        if (area > kRadialGradientAreaWarningPixels) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-radial-gradient-area-budget", "radial-gradient() area is above the embedded highlight budget",
                              "area=" + std::to_string(area) + "px limit=" + std::to_string(kRadialGradientAreaWarningPixels) + "px");
        }
        push_radial_gradient(display_list, rect, paint.color, paint.color2, paint.axis, paint.stop_percent, border_radius);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius);
#endif
    } else if (is_visible_background(paint.color)) {
        push_fill_rect(display_list, rect, paint.color, border_radius);
    }
}

bool equal_border_widths(const EdgeSizes& border) {
    return border.top == border.right && border.top == border.bottom && border.top == border.left;
}

void push_stroke_rect(DisplayList& display_list, Rect rect, Color color, int stroke_width, int border_radius) {
    if (rect.width <= 0 || rect.height <= 0 || stroke_width <= 0 || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::StrokeRect;
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.stroke_width = stroke_width;
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}

void push_border_rects(DisplayList& display_list, Rect rect, const EdgeSizes& border, Color color, int border_radius) {
    if (!has_border(border) || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    if (has_corner_radius(border_radius) && equal_border_widths(border)) {
        push_stroke_rect(display_list, rect, color, border.top, border_radius);
        return;
    }
    push_fill_rect(display_list, Rect{rect.x, rect.y, rect.width, border.top}, color);
    push_fill_rect(display_list, Rect{rect.x, safe_edge(rect.y, safe_edge(rect.height, -border.bottom)), rect.width, border.bottom}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y, border.left, rect.height}, color);
    push_fill_rect(display_list, Rect{safe_edge(rect.x, safe_edge(rect.width, -border.right)), rect.y, border.right, rect.height}, color);
}

void push_text(DisplayList& display_list,
               Rect rect,
               Color color,
               const std::string& text,
               int font_size,
               int font_weight,
               std::uint32_t font_family_hash,
               TextCommandAlign align,
               bool single_line) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty() || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.text = text;
    command.font_size = font_size;
    command.font_weight = font_weight;
    command.font_family_hash = font_family_hash;
    command.text_align = align;
    command.text_single_line = single_line;
    display_list.push_back(std::move(command));
}

std::string ellipsize_single_line_text(const std::string& text,
                                       const Style& style,
                                       int available_width,
                                       const TextMeasureProvider& text_measure) {
    if (!style.text_overflow_ellipsis || !style.white_space_nowrap || available_width <= 0 || text.empty()) {
        return text;
    }
    const auto measure_width = [&](std::string_view value) {
        return measure_text_with_letter_spacing(text_measure,
                                                value,
                                                style.font_size,
                                                style.font_weight,
                                                style.font_family_hash,
                                                style.letter_spacing).width;
    };
    if (measure_width(text) <= available_width) {
        return text;
    }

    static constexpr std::string_view marker = "...";
    if (measure_width(marker) >= available_width) {
        return std::string(marker);
    }

    std::string prefix;
    prefix.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t begin = index;
        consume_utf8_codepoint(text, index);
        std::string candidate = prefix;
        candidate.append(text, begin, index - begin);
        candidate.append(marker);
        if (measure_width(candidate) > available_width) {
            break;
        }
        prefix.assign(candidate, 0, candidate.size() - marker.size());
    }
    prefix.append(marker);
    return prefix;
}

void push_text_with_layout(DisplayList& display_list,
                           Rect rect,
                           Color color,
                           const std::string& text,
                           const Style& style,
                           TextCommandAlign align,
                           const TextMeasureProvider& text_measure) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty() || color.a == 0) {
        return;
    }
    const std::string rendered_text = ellipsize_single_line_text(text, style, rect.width, text_measure);
    const int line_height = style.line_height > 0
        ? style.line_height
        : style.font_size + std::max(6, style.font_size / 3);
    const bool wrap_anywhere = style.overflow_wrap_anywhere && !style.white_space_nowrap;
    const bool wrap_at_opportunities = !wrap_anywhere && !style.white_space_nowrap &&
        has_text_wrap_opportunity(rendered_text);
    const bool split_scalars = style.letter_spacing != 0;
    if (!wrap_anywhere && !wrap_at_opportunities && !split_scalars) {
        push_text(display_list, rect, color, rendered_text, style.font_size, style.font_weight,
                  style.font_family_hash, align, true);
        return;
    }

    const std::vector<std::string> lines = wrap_anywhere
        ? wrap_text_anywhere(text_measure,
                             rendered_text,
                             style.font_size,
                             style.font_weight,
                             style.font_family_hash,
                             style.letter_spacing,
                             rect.width)
        : wrap_at_opportunities
        ? wrap_text_at_opportunities(text_measure,
                                     rendered_text,
                                     style.font_size,
                                     style.font_weight,
                                     style.font_family_hash,
                                     style.letter_spacing,
                                     rect.width)
        : std::vector<std::string>{rendered_text};
    const int bounded_spacing = bounded_letter_spacing(style.font_size, style.letter_spacing);
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string& line = lines[line_index];
        const int y = rect.y + static_cast<int>(line_index) * line_height;
        if (y >= safe_edge(rect.y, rect.height)) {
            break;
        }
        Rect line_rect{rect.x, y, rect.width, std::min(line_height, safe_edge(rect.y, rect.height) - y)};
        if (!split_scalars) {
            push_text(display_list, line_rect, color, line, style.font_size, style.font_weight,
                      style.font_family_hash, align, true);
            continue;
        }
        const int line_width = measure_text_with_letter_spacing(text_measure,
                                                                 line,
                                                                 style.font_size,
                                                                 style.font_weight,
                                                                 style.font_family_hash,
                                                                 style.letter_spacing).width;
        int cursor_x = line_rect.x;
        if (align == TextCommandAlign::Center) {
            cursor_x += std::max(0, (line_rect.width - line_width) / 2);
        } else if (align == TextCommandAlign::End) {
            cursor_x += std::max(0, line_rect.width - line_width);
        }
        std::size_t scalar_index = 0;
        while (scalar_index < line.size()) {
            const std::size_t begin = scalar_index;
            consume_utf8_codepoint(line, scalar_index);
            const std::string scalar = line.substr(begin, scalar_index - begin);
            const int scalar_width = std::max(1, measure_text(text_measure,
                                                              scalar,
                                                              style.font_size,
                                                              style.font_weight,
                                                              style.font_family_hash).width);
            push_text(display_list,
                      Rect{cursor_x, line_rect.y, scalar_width, line_rect.height},
                      color,
                      scalar,
                      style.font_size,
                      style.font_weight,
                      style.font_family_hash,
                      TextCommandAlign::Start,
                      true);
            cursor_x += scalar_width;
            if (scalar_index < line.size()) {
                cursor_x += bounded_spacing;
            }
        }
    }
}

void push_image(DisplayList& display_list,
                Rect rect,
                std::uint32_t image_handle,
                ObjectFit object_fit,
                ObjectPosition object_position,
                ImageRendering image_rendering,
                int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || image_handle == 0) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = rect;
    command.image_handle = image_handle;
    command.object_fit = object_fit;
    command.object_position = object_position;
    command.image_rendering = image_rendering;
    command.border_radius = border_radius;
    command.color = Color{255, 255, 255, 255};
    command.color2 = command.color;
    display_list.push_back(std::move(command));
}

void push_text_decorations(DisplayList& display_list, const LayoutBox& box, Rect rect) {
    if (!box.style.text_decoration_underline && !box.style.text_decoration_line_through) {
        return;
    }
    const int thickness = std::max(1, box.style.font_size / 12);
    const int inset = std::max(0, box.style.font_size / 12);
    const Rect line_rect_base{
        rect.x + inset,
        rect.y,
        std::max(0, rect.width - inset * 2),
        thickness,
    };
    if (line_rect_base.width <= 0) {
        return;
    }
    if (box.style.text_decoration_line_through) {
        Rect strike = line_rect_base;
        strike.y = rect.y + std::max(0, (rect.height - thickness) / 2);
        push_fill_rect(display_list, strike, box.style.color);
    }
    if (box.style.text_decoration_underline) {
        Rect underline = line_rect_base;
        underline.y = rect.y + std::max(0, rect.height - std::max(thickness + 1, box.style.font_size / 5));
        push_fill_rect(display_list, underline, box.style.color);
    }
}

TextCommandAlign text_command_align(TextAlign align) {
    switch (align) {
    case TextAlign::Center:
        return TextCommandAlign::Center;
    case TextAlign::End:
        return TextCommandAlign::End;
    case TextAlign::Start:
    default:
        return TextCommandAlign::Start;
    }
}

int estimate_marker_width(const std::string& text, int font_size) {
    int units = 0;
    for (char ch : text) {
        units += ch == '.' || ch == ' ' ? 4 : 8;
    }
    return std::max(font_size, (font_size * units + 7) / 14);
}

int list_item_ordinal(const Node& node) {
    if (node.parent == nullptr) {
        return 1;
    }
    int ordinal = 0;
    for (const auto& sibling : node.parent->children) {
        if (sibling->type == NodeType::Element && sibling->tag_name == "li") {
            ++ordinal;
        }
        if (sibling.get() == &node) {
            return std::max(1, ordinal);
        }
    }
    return std::max(1, ordinal);
}

Rect content_rect_for(const LayoutBox& box);

std::string generated_before_text(const LayoutBox& box) {
    if (box.style.before_content_kind == GeneratedContentKind::Text) {
        return box.style.before_content_text;
    }
    if (box.style.before_content_kind == GeneratedContentKind::Counter && box.node != nullptr) {
        return std::to_string(list_item_ordinal(*box.node)) + box.style.before_counter_suffix;
    }
    return {};
}

std::string generated_after_text(const LayoutBox& box) {
    if (box.style.after_content_kind == GeneratedContentKind::Text) {
        return box.style.after_content_text;
    }
    if (box.style.after_content_kind == GeneratedContentKind::Counter && box.node != nullptr) {
        return std::to_string(list_item_ordinal(*box.node)) + box.style.after_counter_suffix;
    }
    return {};
}

std::string native_marker_text(const LayoutBox& box) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name != "li") {
        return {};
    }
    if (box.style.list_style_type == ListStyleType::Decimal) {
        return std::to_string(list_item_ordinal(*box.node)) + ".";
    }
    if (box.style.list_style_type == ListStyleType::Disc) {
        return "*";
    }
    return {};
}

void paint_list_marker(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name != "li") {
        return;
    }
    std::string marker = generated_before_text(box);
    bool generated = !marker.empty();
    if (!generated) {
        marker = native_marker_text(box);
    }
    if (marker.empty()) {
        return;
    }

    const int font_weight = generated && box.style.before_font_weight_specified
        ? box.style.before_font_weight
        : box.style.font_weight;
    const Color color = generated && box.style.before_color_specified ? box.style.before_color : box.style.color;
    const int width = estimate_marker_width(marker, box.style.font_size);
    const int marker_x = generated
        ? box.rect.x + (box.style.before_left_specified ? box.style.before_left : 0)
        : box.rect.x - width - 4;
    push_text(display_list,
              Rect{marker_x, box.rect.y, width, std::max(box.rect.height, box.style.font_size + 4)},
              color,
              marker,
              box.style.font_size,
              font_weight,
              box.style.font_family_hash,
              TextCommandAlign::Start,
              true);
}

void paint_generated_inline_content(const LayoutBox& box,
                                    DisplayList& display_list,
                                    CssPseudoElement pseudo_element) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name == "li") {
        return;
    }
    const Rect content = content_rect_for(box);
    const auto paint_one = [&](const std::string& text,
                               bool after,
                               TextCommandAlign align) {
        if (text.empty()) {
            return;
        }
        const int font_weight = after
            ? (box.style.after_font_weight_specified ? box.style.after_font_weight : box.style.font_weight)
            : (box.style.before_font_weight_specified ? box.style.before_font_weight : box.style.font_weight);
        const Color color = after
            ? (box.style.after_color_specified ? box.style.after_color : box.style.color)
            : (box.style.before_color_specified ? box.style.before_color : box.style.color);
        Rect rect = content;
        if (!after && box.style.before_left_specified) {
            rect.x += box.style.before_left;
            rect.width = std::max(0, rect.width - box.style.before_left);
        } else if (after && box.style.after_left_specified) {
            rect.x += box.style.after_left;
            rect.width = std::max(0, rect.width - box.style.after_left);
        }
        push_text(display_list,
                  rect,
                  color,
                  text,
                  box.style.font_size,
                  font_weight,
                  box.style.font_family_hash,
                  align,
                  true);
    };

    if (pseudo_element == CssPseudoElement::Before) {
        paint_one(generated_before_text(box), false, TextCommandAlign::Start);
    } else if (pseudo_element == CssPseudoElement::After) {
        paint_one(generated_after_text(box), true, TextCommandAlign::End);
    }
}

bool parse_float_attribute(const Node& node, const char* name, float& output) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    output = parsed;
    return true;
}

void paint_box_shadow(const LayoutBox& box,
                      DisplayList& display_list,
                      const LayerTreeBuilderOptions& options) {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    (void)box;
    (void)display_list;
    (void)options;
    return;
#else
    if (!has_shadow(box.style)) {
        return;
    }
    const BoxShadowStyle& shadow = box.style.box_shadow;
    const Color shadow_color = shadow.uses_current_color ? box.style.color : shadow.color;
    int blur = shadow.blur;
    constexpr int kMaxSoftShadowBlur = 24;
    if (blur > kMaxSoftShadowBlur) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-box-shadow-blur-clamped",
                          "box-shadow blur radius is above the embedded soft-shadow budget",
                          "blur=" + std::to_string(blur) + "px limit=" + std::to_string(kMaxSoftShadowBlur) + "px");
        blur = kMaxSoftShadowBlur;
    }
    const int extent = std::max(1, safe_add(std::max(1, blur), shadow.spread));
    const int spread = shadow.spread;
    const Rect shadow_rect{
        safe_edge(box.rect.x, safe_add(shadow.offset_x, -extent)),
        safe_edge(box.rect.y, safe_add(shadow.offset_y, -extent)),
        safe_add(box.rect.width, safe_add(extent, extent)),
        safe_add(box.rect.height, safe_add(extent, extent)),
    };
    const long long shadow_area = static_cast<long long>(std::max(0, shadow_rect.width)) *
        static_cast<long long>(std::max(0, shadow_rect.height));
    if (shadow_area > kBoxShadowAreaWarningPixels) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-box-shadow-area-budget",
                          "box-shadow paint area is above the embedded shadow budget",
                          "area=" + std::to_string(shadow_area) +
                              "px limit=" + std::to_string(kBoxShadowAreaWarningPixels) + "px");
    }
    push_box_shadow(display_list,
                    Rect{safe_edge(box.rect.x, safe_add(shadow.offset_x, safe_negate(spread))),
                         safe_edge(box.rect.y, safe_add(shadow.offset_y, safe_negate(spread))),
                         safe_add(box.rect.width, safe_add(spread, spread)),
                         safe_add(box.rect.height, safe_add(spread, spread))},
                    shadow_color,
                    expand_corner_radii(resolved_border_radius(box), spread),
                    extent,
                    blur);
#endif
}

void paint_outline(const LayoutBox& box, DisplayList& display_list) {
    if (box.style.outline_width <= 0 || box.style.outline_color.a == 0) {
        return;
    }
    const int width = box.style.outline_width;
    const int extent = std::max(0, safe_add(width, box.style.outline_offset));
    const Rect outline_rect{
        safe_edge(box.rect.x, -extent),
        safe_edge(box.rect.y, -extent),
        safe_edge(box.rect.width, extent * 2),
        safe_edge(box.rect.height, extent * 2),
    };
    const int border_radius = resolved_border_radius(box);
    push_stroke_rect(display_list,
                     outline_rect,
                     box.style.outline_color,
                     width,
                     has_corner_radius(border_radius) ? expand_corner_radii(border_radius, extent) : 0);
}

void paint_meter_bar(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element) {
        return;
    }
    const bool is_progress = box.node->tag_name == "progress";
    const bool is_meter = box.node->tag_name == "meter";
    if (!is_progress && !is_meter) {
        return;
    }

    float min_value = 0.0F;
    float max_value = 1.0F;
    float value = 0.0F;
    if (is_progress) {
        max_value = 1.0F;
        parse_float_attribute(*box.node, "max", max_value);
        if (!parse_float_attribute(*box.node, "value", value)) {
            value = 0.0F;
        }
    } else {
        parse_float_attribute(*box.node, "min", min_value);
        parse_float_attribute(*box.node, "max", max_value);
        if (!parse_float_attribute(*box.node, "value", value)) {
            value = min_value;
        }
    }
    if (max_value <= min_value) {
        return;
    }

    const float ratio = std::max(0.0F, std::min(1.0F, (value - min_value) / (max_value - min_value)));
    Rect inner{
        box.rect.x + box.style.border_width.left + 1,
        box.rect.y + box.style.border_width.top + 1,
        std::max(0, box.rect.width - box.style.border_width.left - box.style.border_width.right - 2),
        std::max(0, box.rect.height - box.style.border_width.top - box.style.border_width.bottom - 2),
    };
    inner.width = static_cast<int>(static_cast<float>(inner.width) * ratio + 0.5F);
    const Color fill = is_progress ? Color{37, 99, 235, 255} : Color{22, 163, 74, 255};
    push_fill_rect(display_list, inner, fill, expand_corner_radii(resolved_border_radius(box), -1));
}

int range_state_value(const FormControlState& state) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(state.value.c_str(), &end, 10);
    if (end == state.value.c_str() || errno == ERANGE || end == nullptr || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return state.min;
    }
    return static_cast<int>(parsed);
}

void paint_form_control(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element || !is_form_control(*box.node)) {
        return;
    }
    const FormControlState& state = ensure_form_control_state(*box.node);
    const Rect inner{
        box.rect.x + box.style.border_width.left + box.style.padding.left,
        box.rect.y + box.style.border_width.top + box.style.padding.top,
        std::max(0, box.rect.width - box.style.border_width.left - box.style.border_width.right -
                    box.style.padding.left - box.style.padding.right),
        std::max(0, box.rect.height - box.style.border_width.top - box.style.border_width.bottom -
                    box.style.padding.top - box.style.padding.bottom),
    };

    if (state.kind == FormControlKind::Checkbox || state.kind == FormControlKind::Radio) {
        if (state.checked) {
            const Rect mark{
                box.rect.x + std::max(3, box.rect.width / 4),
                box.rect.y + std::max(3, box.rect.height / 4),
                std::max(4, box.rect.width / 2),
                std::max(4, box.rect.height / 2),
            };
            push_fill_rect(display_list, mark, Color{37, 99, 235, 255}, state.kind == FormControlKind::Radio ? 99 : 1);
        }
        return;
    }

    if (state.kind == FormControlKind::Range) {
        const int track_height = 4;
        const Rect track{
            inner.x,
            inner.y + std::max(0, (inner.height - track_height) / 2),
            inner.width,
            track_height,
        };
        push_fill_rect(display_list, track, Color{203, 213, 225, 255}, 2);
        const std::int64_t min_value = state.min;
        const std::int64_t max_value = state.max;
        const std::int64_t denom = std::max<std::int64_t>(1, max_value - min_value);
        const std::int64_t value = std::max<std::int64_t>(min_value,
                                                          std::min<std::int64_t>(range_state_value(state),
                                                                                 max_value));
        const std::int64_t fill_width64 =
            (static_cast<std::int64_t>(inner.width) * (value - min_value) + denom / 2) / denom;
        const int fill_width = static_cast<int>(std::max<std::int64_t>(
            0, std::min<std::int64_t>(fill_width64, inner.width)));
        push_fill_rect(display_list, Rect{track.x, track.y, fill_width, track.height}, Color{37, 99, 235, 255}, 2);
        const int thumb_size = std::max(10, std::min(18, box.rect.height - 2));
        const int thumb_x = inner.x + fill_width - thumb_size / 2;
        push_fill_rect(display_list,
                       Rect{thumb_x, box.rect.y + std::max(0, (box.rect.height - thumb_size) / 2),
                            thumb_size, thumb_size},
                       Color{37, 99, 235, 255},
                       thumb_size / 2);
        return;
    }

    if (state.kind == FormControlKind::Text || state.kind == FormControlKind::TextArea ||
        state.kind == FormControlKind::Date || state.kind == FormControlKind::Time ||
        state.kind == FormControlKind::Color || state.kind == FormControlKind::File ||
        state.kind == FormControlKind::Select) {
        std::string text = form_control_display_text(*box.node);
        Color text_color = box.style.color;
        if (text.empty()) {
            text = box.node->attribute("placeholder");
            text_color = Color{100, 116, 139, 255};
        }
        if (state.kind == FormControlKind::File && text.empty()) {
            text = "Choose file";
        }
        const int arrow_width = state.kind == FormControlKind::Select ? 14 : 0;
        push_text(display_list,
                  Rect{inner.x + 2, inner.y, std::max(0, inner.width - arrow_width - 4), inner.height},
                  text_color,
                  text,
                  box.style.font_size,
                  box.style.font_weight,
                  box.style.font_family_hash,
                  text_command_align(box.style.text_align),
                  true);
        if (state.kind == FormControlKind::Select) {
            push_text(display_list,
                      Rect{inner.x + std::max(0, inner.width - 12), inner.y, 12, inner.height},
                      Color{15, 23, 42, 255},
                      "v",
                      box.style.font_size,
                      box.style.font_weight,
                      box.style.font_family_hash,
                      TextCommandAlign::Center,
                      true);
        }
    }
}

bool resolve_image_handle(const LayoutBox& box,
                          ImageResolveKind kind,
                          std::uint16_t background_resource_id,
                          const LayerTreeBuilderOptions& options,
                          std::uint32_t& image_handle) {
    image_handle = 0;
    if (box.node == nullptr || box.node->type != NodeType::Element || options.image_resolver.resolve == nullptr) {
        return false;
    }
    if (kind == ImageResolveKind::Content &&
        (box.node->tag_name != "img" && box.node->tag_name != "canvas")) {
        return false;
    }
    if (kind == ImageResolveKind::Content && box.node->tag_name == "img" && box.node->attribute("src").empty()) {
        return false;
    }
    if (kind == ImageResolveKind::Background && background_resource_id == 0) {
        return false;
    }
    return options.image_resolver.resolve(*box.node,
                                          kind,
                                          background_resource_id,
                                          image_handle,
                                          options.image_resolver.context) && image_handle != 0;
}

Rect content_rect_for(const LayoutBox& box) {
    return Rect{
        box.rect.x + box.style.border_width.left + box.style.padding.left,
        box.rect.y + box.style.border_width.top + box.style.padding.top,
        std::max(0, box.rect.width - box.style.border_width.left - box.style.border_width.right -
                    box.style.padding.left - box.style.padding.right),
        std::max(0, box.rect.height - box.style.border_width.top - box.style.border_width.bottom -
                    box.style.padding.top - box.style.padding.bottom),
    };
}

int scrollable_content_height(const LayoutBox& box) {
    const Rect content = content_rect_for(box);
    int bottom = content.y;
    for (const auto& child : box.children) {
        bottom = std::max(bottom, child->rect.y + child->rect.height + child->style.margin.bottom);
    }
    return std::max(0, bottom - content.y);
}

int max_scroll_y_for(const LayoutBox& box) {
    if (!has_scrollable_overflow(box.style)) {
        return 0;
    }
    const Rect content = content_rect_for(box);
    return std::max(0, scrollable_content_height(box) - std::max(0, content.height));
}

int resolved_scroll_y_for(const LayoutBox& box, const LayerTreeBuilderOptions& options) {
    if (box.node == nullptr || options.scroll_resolver.resolve_y == nullptr) {
        return 0;
    }
    const int max_scroll = max_scroll_y_for(box);
    if (max_scroll <= 0) {
        return 0;
    }
    const int requested = options.scroll_resolver.resolve_y(*box.node, max_scroll, options.scroll_resolver.context);
    return std::max(0, std::min(requested, max_scroll));
}

bool paint_scroll_indicator(const LayoutBox& box, int scroll_y, int max_scroll_y, DisplayList& display_list) {
    if (max_scroll_y <= 0 || scroll_y < 0) {
        return false;
    }
    const Rect content = content_rect_for(box);
    if (content.width < 16 || content.height < 18) {
        return false;
    }
    constexpr int kTrackWidth = 3;
    constexpr int kTrackInset = 2;
    constexpr int kMinThumbHeight = 12;
    const int track_height = std::max(0, content.height - kTrackInset * 2);
    if (track_height < kMinThumbHeight) {
        return false;
    }
    const int scrollable_height = content.height + max_scroll_y;
    int thumb_height = std::max(kMinThumbHeight, (track_height * content.height) / std::max(1, scrollable_height));
    thumb_height = std::min(track_height, thumb_height);
    const int travel = std::max(0, track_height - thumb_height);
    const int clamped_scroll = std::max(0, std::min(scroll_y, max_scroll_y));
    const int thumb_y = content.y + kTrackInset +
        (max_scroll_y > 0 ? (travel * clamped_scroll) / max_scroll_y : 0);
    const int track_x = content.x + content.width - kTrackWidth - kTrackInset;
    const Rect track{track_x, content.y + kTrackInset, kTrackWidth, track_height};
    const Rect thumb{track_x, thumb_y, kTrackWidth, thumb_height};
    push_fill_rect(display_list, track, Color{255, 255, 255, 48}, 2);
    push_fill_rect(display_list, thumb, Color{255, 255, 255, 176}, 2);
    return true;
}

void translate_display_commands(DisplayList& display_list, std::size_t begin, int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    for (std::size_t index = begin; index < display_list.size(); ++index) {
        display_list[index].rect.x += dx;
        display_list[index].rect.y += dy;
    }
}

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
int select_popup_row_height(const LayoutBox& box) {
    return std::max(20, box.style.line_height > 0
        ? box.style.line_height
        : box.style.font_size + std::max(6, box.style.font_size / 3));
}
#endif

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
bool paint_select_popup(const LayoutBox& box,
                        const Rect& viewport,
                        int scroll_y,
                        const LayerTreeBuilderOptions& options,
                        DisplayList& display_list,
                        Rect& popup_bounds) {
    if (box.node == nullptr || !select_popup_is_open(*box.node)) {
        return false;
    }
    const int option_count = form_control_option_count(*box.node);
    const SelectPopupGeometry geometry = select_popup_geometry(
        box.rect, viewport, option_count, select_popup_row_height(box));
    if (geometry.visible_option_count <= 0 || geometry.rect.width <= 0 || geometry.rect.height <= 0) {
        return false;
    }
    if (geometry.visible_option_count < option_count) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "select-popup-option-limit",
                          "The select popup cannot show every option inside the current viewport",
                          "Use a shorter option list or a page-level picker until popup scrolling is enabled.");
    }

    popup_bounds = geometry.rect;
    popup_bounds.y -= scroll_y;
    push_fill_rect(display_list, popup_bounds, Color{248, 250, 252, 255}, 2);
    const EdgeSizes border{1, 1, 1, 1};
    push_border_rects(display_list, popup_bounds, border, box.style.border_color, 2);
    const FormControlState& state = ensure_form_control_state(*box.node);
    for (int visible_index = 0; visible_index < geometry.visible_option_count; ++visible_index) {
        const int option_index = geometry.first_option_index + visible_index;
        const Rect row{popup_bounds.x,
                       popup_bounds.y + visible_index * geometry.row_height,
                       popup_bounds.width,
                       geometry.row_height};
        if (option_index == state.selected_index) {
            push_fill_rect(display_list, row, Color{219, 234, 254, 255}, 0);
        }
        const Color text_color = form_control_option_disabled(*box.node, option_index)
            ? Color{148, 163, 184, 255}
            : box.style.color;
        push_text_with_layout(display_list,
                              Rect{row.x + 5, row.y, std::max(0, row.width - 10), row.height},
                              text_color,
                              form_control_option_text(*box.node, option_index),
                              box.style,
                              TextCommandAlign::Start,
                              options.text_measure);
    }
    return true;
}
#endif

void paint_box_self(const LayoutBox& box, DisplayList& display_list, const LayerTreeBuilderOptions& options) {
    const Rect paint_rect = paint_rect_for(box);
    const int border_radius = resolved_border_radius(box);
    paint_box_shadow(box, display_list, options);
    const BackgroundPaint base{box.style.background_paint, box.style.background_gradient_axis,
                               box.style.background_gradient_stop_percent, box.style.background_color,
                               box.style.background_color2};
    paint_background_paint(base, paint_rect, border_radius, display_list, options);
    if (has_background_overlay(box.style.background_overlay_packed) &&
        !has_background_image_resource(box.style.background_overlay_packed)) {
        paint_background_paint(unpack_background_overlay(box.style.background_overlay_packed),
                               paint_rect, border_radius, display_list, options);
    }
    const std::uint16_t background_resource_id =
        background_image_resource_id(box.style.background_overlay_packed);
    std::uint32_t background_image_handle = 0;
    if (resolve_image_handle(box,
                             ImageResolveKind::Background,
                             background_resource_id,
                             options,
                             background_image_handle)) {
        push_image(display_list,
                   paint_rect,
                   background_image_handle,
                   background_image_object_fit(box.style.background_overlay_packed),
                   background_image_object_position(box.style.background_overlay_packed),
                   background_image_rendering(box.style.background_overlay_packed),
                   border_radius);
    }

    if (has_border(box.style.border_width)) {
        push_border_rects(display_list,
                          paint_rect,
                          box.style.border_width,
                          box.style.border_color,
                          border_radius);
    }
    paint_outline(box, display_list);

    paint_meter_bar(box, display_list);
    paint_form_control(box, display_list);
    paint_list_marker(box, display_list);
    paint_generated_inline_content(box, display_list, CssPseudoElement::Before);

    std::uint32_t image_handle = 0;
    if (resolve_image_handle(box, ImageResolveKind::Content, 0, options, image_handle)) {
        push_image(display_list,
                   content_rect_for(box),
                   image_handle,
                   box.style.object_fit,
                   box.style.object_position,
                   box.style.image_rendering,
                   resolved_border_radius(box));
    }

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        const std::string text = transformed_render_text(*box.node, box.style.text_transform);
        if (has_text_shadow(box.style)) {
            const TextShadowStyle& shadow = box.style.text_shadow;
            Rect shadow_rect = box.rect;
            shadow_rect.x += shadow.offset_x;
            shadow_rect.y += shadow.offset_y;
            push_text_with_layout(display_list,
                                  shadow_rect,
                                  shadow.uses_current_color ? box.style.color : shadow.color,
                                  text,
                                  box.style,
                                  text_command_align(box.style.text_align),
                                  options.text_measure);
        }
        push_text_with_layout(display_list,
                              box.rect,
                              box.style.color,
                              text,
                              box.style,
                              text_command_align(box.style.text_align),
                              options.text_measure);
        push_text_decorations(display_list, box, box.rect);
    }
}

LayerReasons layer_reasons_for(const LayoutBox& box, bool root) {
    LayerReasons reasons = LayerReasonNone;
    if (root) {
        reasons |= LayerReasonRoot;
    }
    if (has_overflow_clip(box.style)) {
        reasons |= LayerReasonOverflowClip;
    }
    if (box.style.opacity < 0.999F) {
        reasons |= LayerReasonOpacity;
    }
    if (has_transform(box.style)) {
        reasons |= LayerReasonTransform;
    }
    if (is_positioned(box.style)) {
        reasons |= LayerReasonPositioned;
    }
    if (!box.style.z_index_auto) {
        reasons |= LayerReasonZIndex;
    }
    if (has_shadow(box.style)) {
        reasons |= LayerReasonShadow;
    }
    if ((has_corner_radius(box.style.border_radius) || box.style.border_radius_percent >= 0) && has_overflow_clip(box.style)) {
        reasons |= LayerReasonRoundedClip;
    }
    return reasons;
}

LayerType layer_type_for(LayerReasons reasons) {
    if ((reasons & LayerReasonRoot) != 0U) {
        return LayerType::Root;
    }
    if ((reasons & (LayerReasonOpacity | LayerReasonTransform)) != 0U) {
        return LayerType::Composited;
    }
    if ((reasons & (LayerReasonPositioned | LayerReasonZIndex)) != 0U) {
        return LayerType::Stacking;
    }
    if ((reasons & (LayerReasonOverflowClip | LayerReasonRoundedClip)) != 0U) {
        return LayerType::Clip;
    }
    return LayerType::Paint;
}

bool needs_own_layer(LayerReasons reasons) {
    return (reasons & ~static_cast<LayerReasons>(LayerReasonRoot)) != 0U;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = static_cast<std::uint8_t>(std::max(0, std::min(255, alpha)));
    return color;
}

void append_flattened_command(DisplayList& output,
                              const DisplayCommand& command,
                              Rect clip,
                              bool has_clip,
                              float opacity,
                              int translate_x,
                              int translate_y,
                              std::size_t max_display_commands,
                              std::vector<std::uint32_t>* display_clip_indices = nullptr,
                              std::uint32_t clip_index = kNoFlattenedClip) {
    if (output.size() >= max_display_commands) {
        return;
    }
    DisplayCommand flattened = command;
    flattened.rect.x += translate_x;
    flattened.rect.y += translate_y;
    if (has_clip) {
        flattened.rect = intersect_rect(flattened.rect, clip);
        if (empty_rect(flattened.rect)) {
            return;
        }
    }
    flattened.color = with_opacity(flattened.color, opacity);
    flattened.color2 = with_opacity(flattened.color2, opacity);
    if (flattened.color.a == 0 &&
        ((flattened.type != DisplayCommandType::LinearGradient &&
          flattened.type != DisplayCommandType::ConicGradient &&
          flattened.type != DisplayCommandType::RadialGradient) || flattened.color2.a == 0)) {
        return;
    }
    output.push_back(std::move(flattened));
    if (display_clip_indices != nullptr) {
        display_clip_indices->push_back(clip_index);
    }
}

void flatten_layer(const LayerNode& layer,
                   DisplayList& output,
                   Rect clip,
                   bool has_clip,
                   float opacity,
                   int translate_x,
                   int translate_y,
                   std::size_t max_display_commands,
                   DiagnosticSink* diagnostics,
                   bool& display_budget_reported,
                   FlattenedLayerTree* metadata = nullptr) {
    struct PendingLayer {
        const LayerNode* layer = nullptr;
        Rect clip;
        bool has_clip = false;
        float opacity = 1.0F;
        int translate_x = 0;
        int translate_y = 0;
        std::uint32_t clip_index = kNoFlattenedClip;
    };

    std::vector<PendingLayer> pending;
    pending.push_back(PendingLayer{&layer, clip, has_clip, opacity, translate_x, translate_y, kNoFlattenedClip});
    while (!pending.empty()) {
        const PendingLayer current = pending.back();
        pending.pop_back();
        if (output.size() >= max_display_commands) {
            if (!display_budget_reported) {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::LayerTree,
                                  DiagnosticSeverity::Warning,
                                  "display-command-limit",
                                  "Flattened display command budget was reached; remaining paint commands were skipped",
                                  "Increase max_display_commands for complex pages.");
                display_budget_reported = true;
            }
            return;
        }
        const LayerNode& current_layer = *current.layer;
        const float layer_opacity = current.opacity * current_layer.opacity;
        const int layer_translate_x = current.translate_x + static_cast<int>(current_layer.transform.translate_x >= 0.0F
            ? current_layer.transform.translate_x + 0.5F
            : current_layer.transform.translate_x - 0.5F);
        const int layer_translate_y = current.translate_y + static_cast<int>(current_layer.transform.translate_y >= 0.0F
            ? current_layer.transform.translate_y + 0.5F
            : current_layer.transform.translate_y - 0.5F);
        Rect current_clip = current.clip;
        bool current_has_clip = current.has_clip;
        std::uint32_t current_clip_index = current.clip_index;
        if (current_layer.has_clip) {
            current_clip = current_has_clip ? intersect_rect(current_clip, current_layer.clip_rect) : current_layer.clip_rect;
            current_has_clip = true;
            if (empty_rect(current_clip)) {
                continue;
            }
            if (metadata != nullptr) {
                current_clip_index = static_cast<std::uint32_t>(metadata->clips.size());
                metadata->clips.push_back(FlattenedClip{
                    Rect{safe_add(current_layer.clip_rect.x, layer_translate_x),
                         safe_add(current_layer.clip_rect.y, layer_translate_y),
                         current_layer.clip_rect.width,
                         current_layer.clip_rect.height},
                    current_layer.clip_border_radius,
                    current.clip_index});
            }
        }
        for (const DisplayCommand& command : current_layer.display_list) {
            append_flattened_command(output,
                                     command,
                                     current_clip,
                                     current_has_clip,
                                     layer_opacity,
                                     layer_translate_x,
                                     layer_translate_y,
                                     max_display_commands,
                                     metadata != nullptr ? &metadata->display_clip_indices : nullptr,
                                     current_clip_index);
            if (output.size() >= max_display_commands) {
                break;
            }
        }
        for (auto it = current_layer.children.rbegin(); it != current_layer.children.rend(); ++it) {
            pending.push_back(PendingLayer{it->get(),
                                           current_clip,
                                           current_has_clip,
                                           layer_opacity,
                                           layer_translate_x,
                                           layer_translate_y,
                                           current_clip_index});
        }
    }
}

void sort_layer_children(LayerNode& layer) {
    std::vector<LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        LayerNode* current = pending.back();
        pending.pop_back();
        std::stable_sort(current->children.begin(), current->children.end(),
            [](const LayerNodePtr& left, const LayerNodePtr& right) {
                if (left->z_index != right->z_index) {
                    return left->z_index < right->z_index;
                }
                return left->source_order < right->source_order;
            });
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
}

} // namespace

void LayerNodeDeleter::operator()(LayerNode* layer) const {
    if (!arena_owned) {
        delete layer;
    }
}

LayerTreeBuilder::LayerTreeBuilder(LayerTreeBuilderOptions options)
    : options_(options) {}

LayerNodePtr LayerTreeBuilder::build(const LayoutBox& root) const {
    return build_with_arena(root, nullptr);
}

LayerNodePtr LayerTreeBuilder::build(const LayoutBox& root, MonotonicArena& arena) const {
    return build_with_arena(root, &arena);
}

LayerNodePtr LayerTreeBuilder::build_with_arena(const LayoutBox& root, MonotonicArena* arena) const {
    std::size_t remaining_commands = std::max<std::size_t>(1, options_.max_display_commands);
    bool display_budget_reported = false;
    auto root_layer = make_layer_node(arena);
    root_layer->type = LayerType::Root;
    root_layer->reasons = layer_reasons_for(root, true);
    root_layer->box = &root;
    root_layer->bounds = root.rect;
    root_layer->clip_rect = root.rect;
    root_layer->has_clip = has_overflow_clip(root.style);
    root_layer->clip_border_radius = root_layer->has_clip ? resolved_border_radius(root) : 0;
    root_layer->opacity = root.style.opacity;
    root_layer->transform = parsed_transform_or_identity(root.style, options_.diagnostics);
    root_layer->transform_origin_x_percent = root.style.transform_origin_x_percent;
    root_layer->transform_origin_y_percent = root.style.transform_origin_y_percent;
    root_layer->has_transform = has_transform(root.style);
    root_layer->scroll_y = resolved_scroll_y_for(root, options_);
    root_layer->max_scroll_y = max_scroll_y_for(root);
    root_layer->z_index = root.style.z_index;
    root_layer->source_order = 0;

    if (!root.style.visibility_hidden) {
        paint_box_self(root, root_layer->display_list, options_);
    }
    trim_display_list(root_layer->display_list, 0, remaining_commands, display_budget_reported);
    build_children(root, *root_layer, arena, root.rect, remaining_commands, display_budget_reported);
    if (!root.style.visibility_hidden) {
        const std::size_t command_begin = root_layer->display_list.size();
        paint_generated_inline_content(root, root_layer->display_list, CssPseudoElement::After);
        trim_display_list(root_layer->display_list, command_begin, remaining_commands, display_budget_reported);
    }
    sort_layer_children(*root_layer);
    return root_layer;
}

DisplayList LayerTreeBuilder::flatten(const LayerNode& root) const {
    DisplayList output;
    flatten_into(root, output);
    return output;
}

void LayerTreeBuilder::flatten_into(const LayerNode& root, DisplayList& output) const {
    output.clear();
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    const std::size_t required_capacity = std::min(count_layer_display_commands(root), max_display_commands);
    if (output.capacity() < required_capacity) {
        output.reserve(required_capacity);
    }
    bool display_budget_reported = false;
    flatten_layer(root, output, Rect{}, false, 1.0F, 0, 0, max_display_commands,
                  options_.diagnostics, display_budget_reported);
}

FlattenedLayerTree LayerTreeBuilder::flatten_with_clip_metadata(const LayerNode& root) const {
    FlattenedLayerTree output;
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    const std::size_t required_capacity = std::min(count_layer_display_commands(root), max_display_commands);
    output.display_list.reserve(required_capacity);
    output.display_clip_indices.reserve(required_capacity);
    output.clips.reserve(count_layers(root));
    bool display_budget_reported = false;
    flatten_layer(root,
                  output.display_list,
                  Rect{},
                  false,
                  1.0F,
                  0,
                  0,
                  max_display_commands,
                  options_.diagnostics,
                  display_budget_reported,
                  &output);
    return output;
}

void LayerTreeBuilder::trim_display_list(DisplayList& display_list,
                                         std::size_t command_begin,
                                         std::size_t& remaining_commands,
                                         bool& budget_reported) const {
    command_begin = std::min(command_begin, display_list.size());
    const std::size_t added_commands = display_list.size() - command_begin;
    const std::size_t kept_commands = std::min(added_commands, remaining_commands);
    if (kept_commands < added_commands) {
        display_list.resize(command_begin + kept_commands);
        if (!budget_reported) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "display-command-limit",
                          "Retained-tree display command budget was reached; later commands were clipped",
                          "Increase max_display_commands for complex pages.");
            budget_reported = true;
        }
    }
    remaining_commands -= kept_commands;
}

void LayerTreeBuilder::build_children(const LayoutBox& box,
                                      LayerNode& layer,
                                      MonotonicArena* arena,
                                      const Rect& viewport,
                                      std::size_t& remaining_commands,
                                      bool& display_budget_reported) const {
#if !JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    (void)viewport;
#endif
    struct PendingBox {
        const LayoutBox* box = nullptr;
        LayerNode* layer = nullptr;
        int scroll_y = 0;
        bool exit = false;
    };

    std::size_t next_source_order = layer.source_order + 1;
    std::size_t layer_count = 1;
    bool layer_budget_reported = false;
    const std::size_t max_layers = std::max<std::size_t>(1, options_.max_layers);

    std::vector<PendingBox> pending;
    pending.reserve(box.children.size());
    const auto enqueue_children = [&pending](const LayoutBox& parent,
                                             LayerNode* parent_layer,
                                             int parent_scroll_y) {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        const std::vector<const LayoutBox*> ordered = ordered_flex_paint_children(parent);
#else
        const std::vector<const LayoutBox*> ordered;
#endif
        if (ordered.empty()) {
            for (auto it = parent.children.rbegin(); it != parent.children.rend(); ++it) {
                pending.push_back(PendingBox{it->get(), parent_layer, parent_scroll_y, false});
            }
            return;
        }
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            pending.push_back(PendingBox{*it, parent_layer, parent_scroll_y, false});
        }
    };
    enqueue_children(box, &layer, layer.scroll_y);

    while (!pending.empty()) {
        const PendingBox current = pending.back();
        pending.pop_back();
        const LayoutBox* current_box = current.box;
        LayerNode& current_layer = *current.layer;
        if (current.exit) {
            if (!current_box->style.visibility_hidden) {
                const std::size_t command_begin = current_layer.display_list.size();
                paint_generated_inline_content(*current_box, current_layer.display_list, CssPseudoElement::After);
                translate_display_commands(current_layer.display_list, command_begin, 0, -current.scroll_y);
                trim_display_list(current_layer.display_list,
                                  command_begin,
                                  remaining_commands,
                                  display_budget_reported);
            }
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
            if (layer_count < max_layers && current_box->node != nullptr &&
                form_control_kind(*current_box->node) == FormControlKind::Select &&
                select_popup_is_open(*current_box->node)) {
                DisplayList popup_commands;
                popup_commands.reserve(2 + static_cast<std::size_t>(
                    std::max(0, form_control_option_count(*current_box->node)) * 2));
                Rect popup_bounds;
                if (paint_select_popup(*current_box,
                                       viewport,
                                       current.scroll_y,
                                       options_,
                                       popup_commands,
                                       popup_bounds)) {
                    auto popup_layer = make_layer_node(arena);
                    popup_layer->type = LayerType::Stacking;
                    popup_layer->reasons = LayerReasonZIndex | LayerReasonTransientOverlay;
                    popup_layer->box = current_box;
                    popup_layer->bounds = popup_bounds;
                    popup_layer->opacity = 1.0F;
                    popup_layer->z_index = current_layer.z_index;
                    popup_layer->source_order = next_source_order++;
                    popup_layer->display_list = std::move(popup_commands);
                    trim_display_list(popup_layer->display_list,
                                      0,
                                      remaining_commands,
                                      display_budget_reported);
                    current_layer.children.push_back(std::move(popup_layer));
                    ++layer_count;
                }
            }
#endif
            if (options_.paint_scroll_indicators &&
                current_layer.box == current_box &&
                current_layer.max_scroll_y > 0 &&
                layer_count < max_layers) {
                DisplayList indicator_commands;
                indicator_commands.reserve(2);
                if (paint_scroll_indicator(*current_box,
                                           current_layer.scroll_y,
                                           current_layer.max_scroll_y,
                                           indicator_commands)) {
                    auto indicator_layer = make_layer_node(arena);
                    indicator_layer->type = LayerType::Paint;
                    indicator_layer->box = nullptr;
                    indicator_layer->bounds = current_box->rect;
                    indicator_layer->opacity = 1.0F;
                    indicator_layer->source_order = next_source_order++;
                    indicator_layer->display_list = std::move(indicator_commands);
                    trim_display_list(indicator_layer->display_list,
                                      0,
                                      remaining_commands,
                                      display_budget_reported);
                    current_layer.children.push_back(std::move(indicator_layer));
                    ++layer_count;
                }
            }
            continue;
        }

        const LayerReasons reasons = layer_reasons_for(*current_box, false);
        LayerNode* target_layer = &current_layer;
        const int own_scroll_y = resolved_scroll_y_for(*current_box, options_);
        if (needs_own_layer(reasons) && layer_count < max_layers) {
            auto child_layer = make_layer_node(arena);
            target_layer = child_layer.get();
            ++layer_count;
            child_layer->type = layer_type_for(reasons);
            child_layer->reasons = reasons;
            child_layer->box = current_box;
            child_layer->bounds = current_box->rect;
            child_layer->clip_rect = current_box->rect;
            child_layer->has_clip = (reasons & LayerReasonOverflowClip) != 0U;
            child_layer->clip_border_radius = (reasons & LayerReasonRoundedClip) != 0U
                ? resolved_border_radius(*current_box)
                : 0;
            child_layer->opacity = current_box->style.opacity;
            child_layer->transform = parsed_transform_or_identity(current_box->style, options_.diagnostics);
            child_layer->transform.translate_y -= static_cast<float>(current.scroll_y);
            child_layer->transform_origin_x_percent = current_box->style.transform_origin_x_percent;
            child_layer->transform_origin_y_percent = current_box->style.transform_origin_y_percent;
            child_layer->has_transform = has_transform(current_box->style);
            child_layer->scroll_y = own_scroll_y;
            child_layer->max_scroll_y = max_scroll_y_for(*current_box);
            child_layer->z_index = current_box->style.z_index_auto ? 0 : current_box->style.z_index;
            child_layer->source_order = next_source_order++;
            current_layer.children.push_back(std::move(child_layer));
        } else if (needs_own_layer(reasons) && !layer_budget_reported) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::LayerTree,
                              DiagnosticSeverity::Warning,
                              "layer-limit",
                              "Layer budget was reached; later stacking/clip/composited boxes were folded into parent layers",
                              "This preserves paint output where possible but may reduce clipping or stacking fidelity.");
            layer_budget_reported = true;
        }

        if (!current_box->style.visibility_hidden) {
            const std::size_t command_begin = target_layer->display_list.size();
            paint_box_self(*current_box, target_layer->display_list, options_);
            if (target_layer == &current_layer) {
                translate_display_commands(target_layer->display_list, command_begin, 0, -current.scroll_y);
            }
            trim_display_list(target_layer->display_list,
                              command_begin,
                              remaining_commands,
                              display_budget_reported);
        }
        const int child_scroll_y = target_layer == &current_layer ? current.scroll_y + own_scroll_y : own_scroll_y;
        pending.push_back(PendingBox{current_box, target_layer, child_scroll_y, true});
        enqueue_children(*current_box, target_layer, child_scroll_y);
    }
}

LayerNodePtr LayerTreeBuilder::make_layer_node(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    }
    return LayerNodePtr(&arena->create<LayerNode>(), LayerNodeDeleter{true});
}

bool apply_opacity_overrides_to_layer_tree(LayerNode& root,
                                           const std::vector<StyleOverride>& overrides,
                                           LayerTreeOverrideScratch& scratch) {
    if (overrides.empty()) {
        return false;
    }

    auto find_layer = [&](const Node* node) -> LayerNode* {
        scratch.clear();
        scratch.pending.push_back(&root);
        while (!scratch.pending.empty()) {
            LayerNode* current = scratch.pending.back();
            scratch.pending.pop_back();
            if (current->box != nullptr && current->box->node == node) {
                return current;
            }
            for (const LayerNodePtr& child : current->children) {
                scratch.pending.push_back(child.get());
            }
        }
        return nullptr;
    };

    // Validate every override first so a rejected batch cannot leave a partial frame.
    for (const StyleOverride& override : overrides) {
        if (override.node == nullptr || !override.has_opacity || override.has_color ||
            override.has_background_color || override.has_transform || find_layer(override.node) == nullptr) {
            scratch.clear();
            return false;
        }
    }
    for (const StyleOverride& override : overrides) {
        LayerNode* layer = find_layer(override.node);
        layer->opacity = override.opacity;
    }
    scratch.clear();
    return true;
}

std::size_t count_layers(const LayerNode& layer) {
    std::size_t count = 0;
    std::vector<const LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        const LayerNode* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

std::size_t count_layer_display_commands(const LayerNode& layer) {
    std::size_t count = 0;
    std::vector<const LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        const LayerNode* current = pending.back();
        pending.pop_back();
        count += current->display_list.size();
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
