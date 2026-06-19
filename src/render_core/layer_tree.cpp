#include "render_core/layer_tree.h"

#include "render_core/form_control.h"
#include "render_core/text_normalization.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace jellyframe {
namespace {

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
    const int x2 = std::max(left.x + left.width, right.x + right.width);
    const int y2 = std::max(left.y + left.height, right.y + right.height);
    return Rect{x1, y1, x2 - x1, y2 - y1};
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

bool is_positioned(const Style& style) {
    return !style.position.empty();
}

bool has_transform(const Style& style) {
    return !style.transform.empty();
}

bool has_shadow(const Style& style) {
    return !style.box_shadow.empty() && style.box_shadow != "none";
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

void push_border_rects(DisplayList& display_list, Rect rect, const EdgeSizes& border, Color color) {
    if (!has_border(border) || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    push_fill_rect(display_list, Rect{rect.x, rect.y, rect.width, border.top}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y + rect.height - border.bottom, rect.width, border.bottom}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y, border.left, rect.height}, color);
    push_fill_rect(display_list, Rect{rect.x + rect.width - border.right, rect.y, border.right, rect.height}, color);
}

void push_text(DisplayList& display_list,
               Rect rect,
               Color color,
               const std::string& text,
               int font_size,
               int font_weight,
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
    command.text_align = align;
    command.text_single_line = single_line;
    display_list.push_back(std::move(command));
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

std::string generated_before_text(const LayoutBox& box) {
    if (box.style.before_content_kind == GeneratedContentKind::Text) {
        return box.style.before_content_text;
    }
    if (box.style.before_content_kind == GeneratedContentKind::Counter && box.node != nullptr) {
        return std::to_string(list_item_ordinal(*box.node)) + box.style.before_counter_suffix;
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
              TextCommandAlign::Start,
              true);
}

bool parse_float_attribute(const Node& node, const char* name, float& output) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE) {
        return false;
    }
    output = parsed;
    return true;
}

bool parse_shadow_length_token(const char*& cursor, int& output) {
    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
        ++cursor;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(cursor, &end);
    if (end == cursor || errno == ERANGE) {
        return false;
    }
    cursor = end;
    if (std::strncmp(cursor, "px", 2) == 0) {
        cursor += 2;
    }
    output = static_cast<int>(parsed >= 0.0F ? parsed + 0.5F : parsed - 0.5F);
    return true;
}

void skip_shadow_non_length_token(const char*& cursor) {
    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
        ++cursor;
    }
    if (*cursor == '\0' || *cursor == ',') {
        return;
    }
    if (std::isalpha(static_cast<unsigned char>(*cursor)) != 0 || *cursor == '#') {
        while (*cursor != '\0' && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '(' && *cursor != ',') {
            ++cursor;
        }
        if (*cursor == '(') {
            int depth = 1;
            ++cursor;
            while (*cursor != '\0' && depth > 0) {
                if (*cursor == '(') {
                    ++depth;
                } else if (*cursor == ')') {
                    --depth;
                }
                ++cursor;
            }
        }
        return;
    }
    ++cursor;
}

bool parse_shadow_lengths(const std::string& shadow, int& offset_x, int& offset_y, int& blur) {
    const char* cursor = shadow.c_str();
    int lengths[3] = {0, 0, 0};
    int count = 0;
    while (*cursor != '\0' && *cursor != ',' && count < 3) {
        const char* before = cursor;
        int value = 0;
        if (parse_shadow_length_token(cursor, value)) {
            lengths[count++] = value;
        } else {
            cursor = before;
            skip_shadow_non_length_token(cursor);
        }
    }
    if (count < 2) {
        return false;
    }
    offset_x = lengths[0];
    offset_y = lengths[1];
    blur = count >= 3 ? lengths[2] : 0;
    return true;
}

Color approximate_shadow_color(const std::string& shadow) {
    const std::size_t rgba = shadow.find("rgba(");
    if (rgba == std::string::npos) {
        return Color{0, 0, 0, 32};
    }
    const std::size_t close = shadow.find(')', rgba + 5);
    if (close == std::string::npos) {
        return Color{0, 0, 0, 32};
    }
    const std::string body = shadow.substr(rgba + 5, close - rgba - 5);
    const std::size_t comma = body.rfind(',');
    if (comma == std::string::npos) {
        return Color{0, 0, 0, 32};
    }
    char* end = nullptr;
    const float alpha = std::strtof(body.c_str() + comma + 1, &end);
    const int out_alpha = static_cast<int>(std::max(0.0F, std::min(1.0F, alpha)) * 255.0F + 0.5F);
    return Color{0, 0, 0, static_cast<std::uint8_t>(std::max(8, std::min(64, out_alpha)))};
}

void paint_approximate_box_shadow(const LayoutBox& box, DisplayList& display_list) {
    if (!has_shadow(box.style)) {
        return;
    }
    int offset_x = 0;
    int offset_y = 0;
    int blur = 0;
    if (!parse_shadow_lengths(box.style.box_shadow, offset_x, offset_y, blur)) {
        return;
    }
    const int spread = std::max(1, blur / 3);
    const Rect shadow_rect{
        box.rect.x + offset_x - spread,
        box.rect.y + offset_y - spread,
        box.rect.width + spread * 2,
        box.rect.height + spread * 2,
    };
    push_fill_rect(display_list, shadow_rect, approximate_shadow_color(box.style.box_shadow), box.style.border_radius);
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
    push_fill_rect(display_list, inner, fill, std::max(0, box.style.border_radius - 1));
}

int range_state_value(const FormControlState& state) {
    char* end = nullptr;
    const long parsed = std::strtol(state.value.c_str(), &end, 10);
    if (end == state.value.c_str()) {
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
        const int denom = std::max(1, state.max - state.min);
        const int value = std::max(state.min, std::min(range_state_value(state), state.max));
        const int fill_width = (inner.width * (value - state.min) + denom / 2) / denom;
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
                  TextCommandAlign::Start,
                  true);
        if (state.kind == FormControlKind::Select) {
            push_text(display_list,
                      Rect{inner.x + std::max(0, inner.width - 12), inner.y, 12, inner.height},
                      Color{15, 23, 42, 255},
                      "v",
                      box.style.font_size,
                      box.style.font_weight,
                      TextCommandAlign::Center,
                      true);
        }
    }
}

void paint_box_self(const LayoutBox& box, DisplayList& display_list) {
    const Rect paint_rect = paint_rect_for(box);
    paint_approximate_box_shadow(box, display_list);
    if (is_visible_background(box.style.background_color)) {
        push_fill_rect(display_list, paint_rect, box.style.background_color, box.style.border_radius);
    }

    if (has_border(box.style.border_width)) {
        push_border_rects(display_list, paint_rect, box.style.border_width, box.style.border_color);
    }

    paint_meter_bar(box, display_list);
    paint_form_control(box, display_list);
    paint_list_marker(box, display_list);

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        const std::string text = normalized_render_text(*box.node);
        const int line_height = box.style.line_height > 0
            ? box.style.line_height
            : box.style.font_size + std::max(6, box.style.font_size / 3);
        const bool single_line = !has_text_wrap_opportunity(text) || box.rect.height <= line_height;
        push_text(display_list,
                  box.rect,
                  box.style.color,
                  text,
                  box.style.font_size,
                  box.style.font_weight,
                  text_command_align(box.style.text_align),
                  single_line);
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
    if (box.style.border_radius > 0 && has_overflow_clip(box.style)) {
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
    const int x2 = std::min(left.x + left.width, right.x + right.width);
    const int y2 = std::min(left.y + left.height, right.y + right.height);
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
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
                              std::size_t max_display_commands) {
    if (output.size() >= max_display_commands) {
        return;
    }
    DisplayCommand flattened = command;
    if (has_clip) {
        flattened.rect = intersect_rect(flattened.rect, clip);
        if (empty_rect(flattened.rect)) {
            return;
        }
    }
    flattened.color = with_opacity(flattened.color, opacity);
    flattened.color2 = with_opacity(flattened.color2, opacity);
    if (flattened.color.a == 0) {
        return;
    }
    output.push_back(std::move(flattened));
}

void flatten_layer(const LayerNode& layer,
                   DisplayList& output,
                   Rect clip,
                   bool has_clip,
                   float opacity,
                   std::size_t max_display_commands,
                   DiagnosticSink* diagnostics,
                   bool& display_budget_reported) {
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
    if (layer.has_clip) {
        clip = has_clip ? intersect_rect(clip, layer.clip_rect) : layer.clip_rect;
        has_clip = true;
        if (empty_rect(clip)) {
            return;
        }
    }

    const float layer_opacity = opacity * layer.opacity;
    for (const DisplayCommand& command : layer.display_list) {
        append_flattened_command(output, command, clip, has_clip, layer_opacity, max_display_commands);
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
    }
    for (const auto& child : layer.children) {
        flatten_layer(*child, output, clip, has_clip, layer_opacity, max_display_commands,
                      diagnostics, display_budget_reported);
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
    }
}

void sort_layer_children(LayerNode& layer) {
    std::stable_sort(layer.children.begin(), layer.children.end(),
        [](const LayerNodePtr& left, const LayerNodePtr& right) {
            if (left->z_index != right->z_index) {
                return left->z_index < right->z_index;
            }
            return left->source_order < right->source_order;
        });
    for (const auto& child : layer.children) {
        sort_layer_children(*child);
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
    std::size_t next_source_order = 0;
    std::size_t layer_count = 1;
    auto root_layer = make_layer_node(arena);
    root_layer->type = LayerType::Root;
    root_layer->reasons = layer_reasons_for(root, true);
    root_layer->box = &root;
    root_layer->bounds = root.rect;
    root_layer->clip_rect = root.rect;
    root_layer->has_clip = has_overflow_clip(root.style);
    root_layer->opacity = root.style.opacity;
    root_layer->z_index = root.style.z_index;
    root_layer->source_order = next_source_order++;

    paint_box_self(root, root_layer->display_list);
    trim_display_list(root_layer->display_list);
    bool layer_budget_reported = false;
    build_children(root, *root_layer, next_source_order, layer_count, layer_budget_reported, arena);
    sort_layer_children(*root_layer);
    return root_layer;
}

DisplayList LayerTreeBuilder::flatten(const LayerNode& root) const {
    DisplayList output;
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    output.reserve(std::min(count_layer_display_commands(root), max_display_commands));
    bool display_budget_reported = false;
    flatten_layer(root, output, Rect{}, false, 1.0F, max_display_commands,
                  options_.diagnostics, display_budget_reported);
    return output;
}

void LayerTreeBuilder::trim_display_list(DisplayList& display_list) const {
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    if (display_list.size() > max_display_commands) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "display-command-limit",
                          "Layer display command budget was reached; commands in this layer were clipped",
                          "Increase max_display_commands for complex pages.");
        display_list.resize(max_display_commands);
    }
}

void LayerTreeBuilder::build_children(const LayoutBox& box,
                                      LayerNode& layer,
                                      std::size_t& next_source_order,
                                      std::size_t& layer_count,
                                      bool& layer_budget_reported,
                                      MonotonicArena* arena) const {
    for (const auto& child : box.children) {
        build_box(*child, layer, next_source_order, layer_count, layer_budget_reported, arena);
    }
}

void LayerTreeBuilder::build_box(const LayoutBox& box,
                                 LayerNode& parent_layer,
                                 std::size_t& next_source_order,
                                 std::size_t& layer_count,
                                 bool& layer_budget_reported,
                                 MonotonicArena* arena) const {
    const LayerReasons reasons = layer_reasons_for(box, false);
    const std::size_t max_layers = std::max<std::size_t>(1, options_.max_layers);
    if (needs_own_layer(reasons) && layer_count < max_layers) {
        auto child_layer = make_layer_node(arena);
        ++layer_count;
        child_layer->type = layer_type_for(reasons);
        child_layer->reasons = reasons;
        child_layer->box = &box;
        child_layer->bounds = box.rect;
        child_layer->clip_rect = box.rect;
        child_layer->has_clip = (reasons & LayerReasonOverflowClip) != 0U;
        child_layer->opacity = box.style.opacity;
        child_layer->z_index = box.style.z_index_auto ? 0 : box.style.z_index;
        child_layer->source_order = next_source_order++;
        paint_box_self(box, child_layer->display_list);
        trim_display_list(child_layer->display_list);
        build_children(box, *child_layer, next_source_order, layer_count, layer_budget_reported, arena);
        parent_layer.children.push_back(std::move(child_layer));
        return;
    }
    if (needs_own_layer(reasons) && !layer_budget_reported) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-limit",
                          "Layer budget was reached; later stacking/clip/composited boxes were folded into parent layers",
                          "This preserves paint output where possible but may reduce clipping or stacking fidelity.");
        layer_budget_reported = true;
    }

    paint_box_self(box, parent_layer.display_list);
    trim_display_list(parent_layer.display_list);
    build_children(box, parent_layer, next_source_order, layer_count, layer_budget_reported, arena);
}

LayerNodePtr LayerTreeBuilder::make_layer_node(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    }
    return LayerNodePtr(&arena->create<LayerNode>(), LayerNodeDeleter{true});
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
