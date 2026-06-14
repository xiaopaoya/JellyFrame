#include "core/style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wearweb {
namespace {

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

constexpr int kRootFontSizePx = 16;

std::string lowercase(std::string value);

bool parse_length_px(const std::string& raw_value, int& output, int em_base = kRootFontSizePx) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }

    float pixels = parsed;
    if (end == nullptr || *end == '\0') {
        pixels = parsed;
    } else if (std::strncmp(end, "px", 2) == 0) {
        pixels = parsed;
        end += 2;
    } else if (std::strncmp(end, "rem", 3) == 0) {
        pixels = parsed * static_cast<float>(kRootFontSizePx);
        end += 3;
    } else if (std::strncmp(end, "em", 2) == 0) {
        pixels = parsed * static_cast<float>(em_base);
        end += 2;
    } else if (std::strncmp(end, "vh", 2) == 0) {
        pixels = parsed * 240.0F / 100.0F;
        end += 2;
    } else {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    output = static_cast<int>(pixels >= 0.0F ? pixels + 0.5F : pixels - 0.5F);
    return true;
}

bool parse_float(const std::string& raw_value, float& output) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE) {
        return false;
    }
    while (end != nullptr && *end != '\0') {
        if (std::isspace(static_cast<unsigned char>(*end)) == 0) {
            return false;
        }
        ++end;
    }
    output = parsed;
    return true;
}

bool parse_integer(const std::string& raw_value, int& output) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE) {
        return false;
    }
    while (end != nullptr && *end != '\0') {
        if (std::isspace(static_cast<unsigned char>(*end)) == 0) {
            return false;
        }
        ++end;
    }
    output = static_cast<int>(parsed);
    return true;
}

bool parse_positive_ratio_number(const std::string& raw_value, int& output) {
    float parsed = 0.0F;
    if (!parse_float(raw_value, parsed) || parsed <= 0.0F) {
        return false;
    }
    output = std::max(1, std::min(1000000, static_cast<int>(parsed * 1000.0F + 0.5F)));
    return true;
}

bool parse_aspect_ratio(const std::string& raw_value, int& width, int& height) {
    std::string value = lowercase(trim(raw_value));
    if (value.rfind("auto", 0) == 0) {
        value = trim(std::string_view(value).substr(4));
    }
    const std::size_t slash = value.find('/');
    if (slash == std::string::npos) {
        int single = 0;
        if (!parse_positive_ratio_number(value, single)) {
            return false;
        }
        width = single;
        height = 1000;
        return true;
    }

    int parsed_width = 0;
    int parsed_height = 0;
    if (!parse_positive_ratio_number(value.substr(0, slash), parsed_width) ||
        !parse_positive_ratio_number(value.substr(slash + 1), parsed_height)) {
        return false;
    }
    width = parsed_width;
    height = parsed_height;
    return true;
}

bool parse_span_value(const std::string& raw_value, int& span) {
    std::istringstream stream(lowercase(trim(raw_value)));
    std::string first;
    if (!(stream >> first)) {
        return false;
    }
    if (first == "span") {
        int parsed = 0;
        if (!(stream >> parsed) || parsed <= 0) {
            return false;
        }
        span = std::min(parsed, 16);
        return true;
    }
    int parsed = 0;
    if (parse_integer(first, parsed) && parsed > 0) {
        span = 1;
        return true;
    }
    return false;
}

bool parse_grid_template_columns_min(const std::string& raw_value, int& min_track, int em_base) {
    const std::string value = lowercase(trim(raw_value));
    const std::size_t minmax = value.find("minmax(");
    if (minmax != std::string::npos) {
        const std::size_t begin = minmax + 7;
        const std::size_t comma = value.find(',', begin);
        if (comma == std::string::npos) {
            return false;
        }
        int px = 0;
        if (!parse_length_px(value.substr(begin, comma - begin), px, em_base)) {
            return false;
        }
        min_track = std::max(1, px);
        return true;
    }
    if (value == "1fr") {
        min_track = 1;
        return true;
    }
    int px = 0;
    if (parse_length_px(value, px, em_base)) {
        min_track = std::max(1, px);
        return true;
    }
    return false;
}

bool parse_grid_auto_rows_min(const std::string& raw_value, int& min_row, int em_base) {
    const std::string value = lowercase(trim(raw_value));
    if (value.rfind("minmax(", 0) == 0) {
        const std::size_t begin = 7;
        const std::size_t comma = value.find(',', begin);
        if (comma == std::string::npos) {
            return false;
        }
        int px = 0;
        if (!parse_length_px(value.substr(begin, comma - begin), px, em_base)) {
            return false;
        }
        min_row = std::max(0, px);
        return true;
    }
    int px = 0;
    if (parse_length_px(value, px, em_base)) {
        min_row = std::max(0, px);
        return true;
    }
    return false;
}

bool parse_box_edge_px(const std::string& value, EdgeSizes& output, int em_base = kRootFontSizePx) {
    std::istringstream stream(value);
    std::array<int, 4> values{0, 0, 0, 0};
    int count = 0;
    std::string token;
    while (stream >> token && count < 4) {
        if (!parse_length_px(token, values[static_cast<std::size_t>(count)], em_base)) {
            return false;
        }
        ++count;
    }
    if (count == 0 || (stream >> token)) {
        return false;
    }
    if (count == 1) {
        output = EdgeSizes{values[0], values[0], values[0], values[0]};
    } else if (count == 2) {
        output = EdgeSizes{values[0], values[1], values[0], values[1]};
    } else if (count == 3) {
        output = EdgeSizes{values[0], values[1], values[2], values[1]};
    } else {
        output = EdgeSizes{values[0], values[1], values[2], values[3]};
    }
    return true;
}

bool parse_margin_edge_px(const std::string& value, EdgeSizes& output, bool& left_auto, bool& right_auto, int em_base) {
    std::istringstream stream(value);
    std::array<int, 4> values{0, 0, 0, 0};
    std::array<bool, 4> auto_values{false, false, false, false};
    int count = 0;
    std::string token;
    while (stream >> token && count < 4) {
        if (token == "auto") {
            auto_values[static_cast<std::size_t>(count)] = true;
        } else if (!parse_length_px(token, values[static_cast<std::size_t>(count)], em_base)) {
            return false;
        }
        ++count;
    }
    if (count == 0 || (stream >> token)) {
        return false;
    }

    if (count == 1) {
        output = EdgeSizes{values[0], values[0], values[0], values[0]};
        left_auto = auto_values[0];
        right_auto = auto_values[0];
    } else if (count == 2) {
        output = EdgeSizes{values[0], values[1], values[0], values[1]};
        left_auto = auto_values[1];
        right_auto = auto_values[1];
    } else if (count == 3) {
        output = EdgeSizes{values[0], values[1], values[2], values[1]};
        left_auto = auto_values[1];
        right_auto = auto_values[1];
    } else {
        output = EdgeSizes{values[0], values[1], values[2], values[3]};
        left_auto = auto_values[3];
        right_auto = auto_values[1];
    }
    if (left_auto) {
        output.left = 0;
    }
    if (right_auto) {
        output.right = 0;
    }
    return true;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parse_hex_component(char high, char low, std::uint8_t& output) {
    const auto digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    const int a = digit(high);
    const int b = digit(low);
    if (a < 0 || b < 0) {
        return false;
    }
    output = static_cast<std::uint8_t>((a << 4) | b);
    return true;
}

bool parse_color(const std::string& raw_value, Color& output) {
    const std::string value = lowercase(trim(raw_value));
    const auto parse_rgb_function = [&](std::string_view body, bool has_alpha) {
        std::string normalized;
        normalized.reserve(body.size());
        for (char ch : body) {
            normalized.push_back(ch == ',' || ch == '/' ? ' ' : ch);
        }
        std::istringstream stream(normalized);
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        float a = 1.0F;
        if (!(stream >> r >> g >> b)) {
            return false;
        }
        if (has_alpha && !(stream >> a)) {
            return false;
        }
        output = Color{
            static_cast<std::uint8_t>(std::max(0.0F, std::min(255.0F, r))),
            static_cast<std::uint8_t>(std::max(0.0F, std::min(255.0F, g))),
            static_cast<std::uint8_t>(std::max(0.0F, std::min(255.0F, b))),
            static_cast<std::uint8_t>(std::max(0.0F, std::min(255.0F, a <= 1.0F ? a * 255.0F : a))),
        };
        return true;
    };

    if (value == "transparent") {
        output = Color{0, 0, 0, 0};
        return true;
    }
    if (value == "black") {
        output = Color{0, 0, 0, 255};
        return true;
    }
    if (value == "white") {
        output = Color{255, 255, 255, 255};
        return true;
    }
    if (value == "red") {
        output = Color{220, 38, 38, 255};
        return true;
    }
    if (value == "green") {
        output = Color{22, 163, 74, 255};
        return true;
    }
    if (value == "blue") {
        output = Color{37, 99, 235, 255};
        return true;
    }
    if (value.size() == 4 && value[0] == '#') {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        if (!parse_hex_component(value[1], value[1], r) ||
            !parse_hex_component(value[2], value[2], g) ||
            !parse_hex_component(value[3], value[3], b)) {
            return false;
        }
        output = Color{r, g, b, 255};
        return true;
    }
    if ((value.size() == 7 || value.size() == 9) && value[0] == '#') {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
        if (!parse_hex_component(value[1], value[2], r) ||
            !parse_hex_component(value[3], value[4], g) ||
            !parse_hex_component(value[5], value[6], b)) {
            return false;
        }
        if (value.size() == 9 && !parse_hex_component(value[7], value[8], a)) {
            return false;
        }
        output = Color{r, g, b, a};
        return true;
    }
    if (value.rfind("rgba(", 0) == 0 && value.back() == ')') {
        return parse_rgb_function(std::string_view(value).substr(5, value.size() - 6), true);
    }
    if (value.rfind("rgb(", 0) == 0 && value.back() == ')') {
        return parse_rgb_function(std::string_view(value).substr(4, value.size() - 5), false);
    }
    if (value.rfind("linear-gradient(", 0) == 0) {
        const std::size_t hash = value.find('#');
        if (hash != std::string::npos) {
            std::size_t end = hash + 1;
            while (end < value.size() && std::isxdigit(static_cast<unsigned char>(value[end])) != 0) {
                ++end;
            }
            return parse_color(value.substr(hash, end - hash), output);
        }
    }
    return false;
}

bool is_identifier_char(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) || ch == '-' || ch == '_';
}

std::string unquote(std::string value) {
    value = trim(value);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool matches_attribute_selector(const Node& node, std::string_view content) {
    const std::size_t equals = content.find('=');
    if (equals == std::string_view::npos) {
        return !node.attribute(trim(content)).empty() || node.attributes.find(trim(content)) != node.attributes.end();
    }
    const std::string name = trim(content.substr(0, equals));
    const std::string expected = unquote(std::string(content.substr(equals + 1)));
    return node.attribute(name) == expected;
}

bool is_document_root_element(const Node& node) {
    return node.type == NodeType::Element &&
        node.tag_name == "html" &&
        node.parent != nullptr &&
        node.parent->tag_name == "document";
}

bool matches_compound_selector(const Node& node, std::string_view selector) {
    if (node.type != NodeType::Element || selector.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (selector[index] == '*') {
        ++index;
    } else if (selector[index] != '.' && selector[index] != '#' && selector[index] != '[' && selector[index] != ':') {
        const std::size_t begin = index;
        while (index < selector.size() && is_identifier_char(selector[index])) {
            ++index;
        }
        if (node.tag_name != selector.substr(begin, index - begin)) {
            return false;
        }
    }

    while (index < selector.size()) {
        const char marker = selector[index];
        if (marker == '[') {
            const std::size_t close = selector.find(']', index + 1);
            if (close == std::string_view::npos ||
                !matches_attribute_selector(node, selector.substr(index + 1, close - index - 1))) {
                return false;
            }
            index = close + 1;
            continue;
        }
        if (marker == ':') {
            const std::size_t begin = index + 1;
            index = begin;
            while (index < selector.size() && is_identifier_char(selector[index])) {
                ++index;
            }
            const std::string pseudo(selector.substr(begin, index - begin));
            if (pseudo == "root") {
                if (!is_document_root_element(node)) {
                    return false;
                }
                continue;
            }
            return false;
        }
        if (marker != '.' && marker != '#') {
            return false;
        }
        ++index;
        const std::size_t begin = index;
        while (index < selector.size() && is_identifier_char(selector[index])) {
            ++index;
        }
        if (begin == index) {
            return false;
        }
        const std::string value(selector.substr(begin, index - begin));
        if (marker == '.' && !node.has_class(value)) {
            return false;
        }
        if (marker == '#' && node.attribute("id") != value) {
            return false;
        }
    }
    return true;
}

std::string extract_id_from_compound(std::string_view compound) {
    const std::size_t pos = compound.find('#');
    if (pos == std::string_view::npos) {
        return {};
    }
    std::size_t index = pos + 1;
    const std::size_t begin = index;
    while (index < compound.size() && is_identifier_char(compound[index])) {
        ++index;
    }
    return std::string(compound.substr(begin, index - begin));
}

std::string extract_class_from_compound(std::string_view compound) {
    const std::size_t pos = compound.find('.');
    if (pos == std::string_view::npos) {
        return {};
    }
    std::size_t index = pos + 1;
    const std::size_t begin = index;
    while (index < compound.size() && is_identifier_char(compound[index])) {
        ++index;
    }
    return std::string(compound.substr(begin, index - begin));
}

std::string extract_tag_from_compound(std::string_view compound) {
    if (compound.empty() || compound[0] == '*' || compound[0] == '.' || compound[0] == '#' ||
        compound[0] == '[' || compound[0] == ':') {
        return {};
    }
    std::size_t index = 0;
    while (index < compound.size() && is_identifier_char(compound[index])) {
        ++index;
    }
    return std::string(compound.substr(0, index));
}

bool matches_selector_from_right(const Node* node, const std::vector<CssSelectorPart>& parts, std::size_t index) {
    if (node == nullptr || index >= parts.size() || !matches_compound_selector(*node, parts[index].compound)) {
        return false;
    }
    if (index + 1 >= parts.size()) {
        return true;
    }

    if (parts[index].combinator_to_left == CssSelectorCombinator::Child) {
        return matches_selector_from_right(node->parent, parts, index + 1);
    }

    for (const Node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (matches_selector_from_right(ancestor, parts, index + 1)) {
            return true;
        }
    }
    return false;
}

bool matches_rule(const Node& node, const CssRule& rule) {
    return !rule.selector_parts.empty() && matches_selector_from_right(&node, rule.selector_parts, 0);
}

struct CascadeSlot {
    bool set = false;
    bool important = false;
    CssSpecificity specificity;
    std::size_t source_order = 0;
};

enum class CascadeProperty : std::size_t {
    Display,
    Color,
    Background,
    Margin,
    Padding,
    BorderWidth,
    BorderColor,
    Border,
    BorderRadius,
    Width,
    Height,
    MinWidth,
    MinHeight,
    MaxWidth,
    AspectRatio,
    FontSize,
    LineHeight,
    TextIndent,
    BoxShadow,
    Overflow,
    Opacity,
    Transform,
    Position,
    ZIndex,
    TextAlign,
    JustifyContent,
    AlignItems,
    BoxSizing,
    Gap,
    ColumnGap,
    RowGap,
    GridTemplateColumns,
    GridAutoRows,
    GridColumn,
    GridRow,
    Count,
};

struct CascadeSlots {
    std::array<CascadeSlot, static_cast<std::size_t>(CascadeProperty::Count)> slots;
};

CascadeSlot& cascade_slot(CascadeSlots& slots, CascadeProperty property) {
    return slots.slots[static_cast<std::size_t>(property)];
}

CascadeSlot* cascade_slot_for_property(CascadeSlots& slots, const std::string& property) {
    if (property == "display") {
        return &cascade_slot(slots, CascadeProperty::Display);
    }
    if (property == "color") {
        return &cascade_slot(slots, CascadeProperty::Color);
    }
    if (property == "background" || property == "background-color") {
        return &cascade_slot(slots, CascadeProperty::Background);
    }
    if (property == "margin") {
        return &cascade_slot(slots, CascadeProperty::Margin);
    }
    if (property == "padding") {
        return &cascade_slot(slots, CascadeProperty::Padding);
    }
    if (property == "border-width") {
        return &cascade_slot(slots, CascadeProperty::BorderWidth);
    }
    if (property == "border-color") {
        return &cascade_slot(slots, CascadeProperty::BorderColor);
    }
    if (property == "border") {
        return &cascade_slot(slots, CascadeProperty::Border);
    }
    if (property == "border-radius") {
        return &cascade_slot(slots, CascadeProperty::BorderRadius);
    }
    if (property == "width") {
        return &cascade_slot(slots, CascadeProperty::Width);
    }
    if (property == "height") {
        return &cascade_slot(slots, CascadeProperty::Height);
    }
    if (property == "min-width") {
        return &cascade_slot(slots, CascadeProperty::MinWidth);
    }
    if (property == "min-height") {
        return &cascade_slot(slots, CascadeProperty::MinHeight);
    }
    if (property == "max-width") {
        return &cascade_slot(slots, CascadeProperty::MaxWidth);
    }
    if (property == "aspect-ratio") {
        return &cascade_slot(slots, CascadeProperty::AspectRatio);
    }
    if (property == "font-size") {
        return &cascade_slot(slots, CascadeProperty::FontSize);
    }
    if (property == "line-height") {
        return &cascade_slot(slots, CascadeProperty::LineHeight);
    }
    if (property == "text-indent") {
        return &cascade_slot(slots, CascadeProperty::TextIndent);
    }
    if (property == "box-shadow") {
        return &cascade_slot(slots, CascadeProperty::BoxShadow);
    }
    if (property == "overflow") {
        return &cascade_slot(slots, CascadeProperty::Overflow);
    }
    if (property == "opacity") {
        return &cascade_slot(slots, CascadeProperty::Opacity);
    }
    if (property == "transform") {
        return &cascade_slot(slots, CascadeProperty::Transform);
    }
    if (property == "position") {
        return &cascade_slot(slots, CascadeProperty::Position);
    }
    if (property == "z-index") {
        return &cascade_slot(slots, CascadeProperty::ZIndex);
    }
    if (property == "text-align") {
        return &cascade_slot(slots, CascadeProperty::TextAlign);
    }
    if (property == "justify-content") {
        return &cascade_slot(slots, CascadeProperty::JustifyContent);
    }
    if (property == "align-items") {
        return &cascade_slot(slots, CascadeProperty::AlignItems);
    }
    if (property == "box-sizing") {
        return &cascade_slot(slots, CascadeProperty::BoxSizing);
    }
    if (property == "gap") {
        return &cascade_slot(slots, CascadeProperty::Gap);
    }
    if (property == "column-gap") {
        return &cascade_slot(slots, CascadeProperty::ColumnGap);
    }
    if (property == "row-gap") {
        return &cascade_slot(slots, CascadeProperty::RowGap);
    }
    if (property == "grid-template-columns") {
        return &cascade_slot(slots, CascadeProperty::GridTemplateColumns);
    }
    if (property == "grid-auto-rows") {
        return &cascade_slot(slots, CascadeProperty::GridAutoRows);
    }
    if (property == "grid-column") {
        return &cascade_slot(slots, CascadeProperty::GridColumn);
    }
    if (property == "grid-row") {
        return &cascade_slot(slots, CascadeProperty::GridRow);
    }
    return nullptr;
}

bool specificity_less(const CssSpecificity& left, const CssSpecificity& right) {
    if (left.ids != right.ids) {
        return left.ids < right.ids;
    }
    if (left.classes != right.classes) {
        return left.classes < right.classes;
    }
    return left.elements < right.elements;
}

bool declaration_wins(const CascadeSlot& current,
                      const CssDeclaration& declaration,
                      const CssSpecificity& specificity,
                      std::size_t source_order) {
    if (!current.set) {
        return true;
    }
    if (current.important != declaration.important) {
        return declaration.important;
    }
    if (specificity_less(current.specificity, specificity)) {
        return true;
    }
    if (specificity_less(specificity, current.specificity)) {
        return false;
    }
    return source_order >= current.source_order;
}

bool apply_declaration(Style& style, const std::string& property, const std::string& value) {
    if (property == "display") {
        if (value == "block") {
            style.display = Display::Block;
        } else if (value == "none") {
            style.display = Display::None;
        } else if (value == "inline") {
            style.display = Display::Inline;
        } else if (value == "inline-block") {
            style.display = Display::InlineBlock;
        } else if (value == "flex" || value == "inline-flex") {
            style.display = Display::Flex;
        } else if (value == "grid" || value == "inline-grid") {
            style.display = Display::Grid;
        } else {
            return false;
        }
        return true;
    } else if (property == "color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.color = parsed;
        style.color_specified = true;
        return true;
    } else if (property == "background" || property == "background-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.background_color = parsed;
        return true;
    } else if (property == "margin") {
        return parse_margin_edge_px(value, style.margin, style.margin_left_auto, style.margin_right_auto, style.font_size);
    } else if (property == "padding") {
        return parse_box_edge_px(value, style.padding, style.font_size);
    } else if (property == "border-width") {
        return parse_box_edge_px(value, style.border_width, style.font_size);
    } else if (property == "border-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.border_color = parsed;
        return true;
    } else if (property == "border") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "none" || lowered == "0" || lowered == "0px") {
            style.border_width = EdgeSizes{};
            return true;
        }
        int width = 0;
        Color color;
        bool has_width = false;
        bool has_color = false;
        std::size_t index = 0;
        while (index < value.size()) {
            while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) {
                ++index;
            }
            const std::size_t begin = index;
            while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) == 0) {
                ++index;
            }
            const std::string token = value.substr(begin, index - begin);
            if (!token.empty() && !has_width && parse_length_px(token, width, style.font_size)) {
                has_width = true;
            } else if (!token.empty() && !has_color && parse_color(token, color)) {
                has_color = true;
            }
        }
        if (!has_width && !has_color) {
            return false;
        }
        if (has_width) {
            style.border_width = EdgeSizes{width, width, width, width};
        }
        if (has_color) {
            style.border_color = color;
        }
        return true;
    } else if (property == "border-radius") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.border_radius = px;
        return true;
    } else if (property == "width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.width = px;
        return true;
    } else if (property == "height") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.height = px;
        return true;
    } else if (property == "min-width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.min_width = px;
        return true;
    } else if (property == "min-height") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.min_height = px;
        return true;
    } else if (property == "max-width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.max_width = px;
        return true;
    } else if (property == "aspect-ratio") {
        int ratio_width = 0;
        int ratio_height = 0;
        if (!parse_aspect_ratio(value, ratio_width, ratio_height)) {
            return false;
        }
        style.aspect_ratio_width = ratio_width;
        style.aspect_ratio_height = ratio_height;
        return true;
    } else if (property == "font-size") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.font_size = px;
        style.font_size_specified = true;
        return true;
    } else if (property == "line-height") {
        float multiplier = 0.0F;
        int px = 0;
        if (parse_float(value, multiplier)) {
            style.line_height = std::max(1, static_cast<int>(static_cast<float>(style.font_size) * multiplier + 0.5F));
        } else if (parse_length_px(value, px, style.font_size)) {
            style.line_height = std::max(1, px);
        } else {
            return false;
        }
        style.line_height_specified = true;
        return true;
    } else if (property == "text-indent") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.text_indent = px;
        style.text_indent_specified = true;
        return true;
    } else if (property == "box-shadow") {
        style.box_shadow = trim(value);
        return !style.box_shadow.empty();
    } else if (property == "overflow") {
        const std::string lowered = lowercase(trim(value));
        if (lowered != "visible" && lowered != "hidden" && lowered != "scroll" &&
            lowered != "auto" && lowered != "clip") {
            return false;
        }
        style.overflow = lowered;
        return true;
    } else if (property == "opacity") {
        float parsed = 1.0F;
        if (!parse_float(value, parsed)) {
            return false;
        }
        style.opacity = std::max(0.0F, std::min(1.0F, parsed));
        return true;
    } else if (property == "transform") {
        const std::string transformed = trim(value);
        if (transformed.empty()) {
            return false;
        }
        style.transform = lowercase(transformed) == "none" ? std::string{} : transformed;
        return true;
    } else if (property == "position") {
        const std::string lowered = lowercase(trim(value));
        if (lowered != "static" && lowered != "relative" && lowered != "absolute" &&
            lowered != "fixed" && lowered != "sticky") {
            return false;
        }
        style.position = lowered == "static" ? std::string{} : lowered;
        return true;
    } else if (property == "z-index") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "auto") {
            style.z_index_auto = true;
            style.z_index = 0;
            return true;
        }
        int parsed = 0;
        if (!parse_integer(lowered, parsed)) {
            return false;
        }
        style.z_index = parsed;
        style.z_index_auto = false;
        return true;
    } else if (property == "text-align") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "center") {
            style.text_align = TextAlign::Center;
        } else if (lowered == "right" || lowered == "end") {
            style.text_align = TextAlign::End;
        } else if (lowered == "left" || lowered == "start") {
            style.text_align = TextAlign::Start;
        } else {
            return false;
        }
        style.text_align_specified = true;
        return true;
    } else if (property == "justify-content") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "center") {
            style.justify_content = JustifyContent::Center;
        } else if (lowered == "space-around") {
            style.justify_content = JustifyContent::SpaceAround;
        } else if (lowered == "space-between") {
            style.justify_content = JustifyContent::SpaceBetween;
        } else if (lowered == "flex-start" || lowered == "start" || lowered == "normal") {
            style.justify_content = JustifyContent::Start;
        } else {
            return false;
        }
        return true;
    } else if (property == "align-items") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "center") {
            style.align_items = AlignItems::Center;
        } else if (lowered == "flex-end" || lowered == "end") {
            style.align_items = AlignItems::End;
        } else if (lowered == "flex-start" || lowered == "start") {
            style.align_items = AlignItems::Start;
        } else if (lowered == "stretch" || lowered == "normal") {
        style.align_items = AlignItems::Stretch;
        } else {
            return false;
        }
        return true;
    } else if (property == "box-sizing") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "border-box") {
            style.box_sizing_border_box = true;
        } else if (lowered == "content-box") {
            style.box_sizing_border_box = false;
        } else {
            return false;
        }
        return true;
    } else if (property == "gap") {
        EdgeSizes parsed;
        if (!parse_box_edge_px(value, parsed, style.font_size)) {
            return false;
        }
        style.row_gap = std::max(0, parsed.top);
        style.column_gap = std::max(0, parsed.right);
        return true;
    } else if (property == "column-gap") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.column_gap = std::max(0, px);
        return true;
    } else if (property == "row-gap") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.row_gap = std::max(0, px);
        return true;
    } else if (property == "grid-template-columns") {
        int min_track = 0;
        if (!parse_grid_template_columns_min(value, min_track, style.font_size)) {
            return false;
        }
        style.grid_min_track_width = min_track;
        return true;
    } else if (property == "grid-auto-rows") {
        int min_row = 0;
        if (!parse_grid_auto_rows_min(value, min_row, style.font_size)) {
            return false;
        }
        style.grid_auto_row_min = min_row;
        return true;
    } else if (property == "grid-column") {
        int span = 1;
        if (!parse_span_value(value, span)) {
            return false;
        }
        style.grid_column_span = span;
        return true;
    } else if (property == "grid-row") {
        int span = 1;
        if (!parse_span_value(value, span)) {
            return false;
        }
        style.grid_row_span = span;
        return true;
    }
    return false;
}

void mark_slot(CascadeSlot& slot,
               const CssDeclaration& declaration,
               const CssSpecificity& specificity,
               std::size_t source_order) {
    slot.set = true;
    slot.important = declaration.important;
    slot.specificity = specificity;
    slot.source_order = source_order;
}

void apply_cascaded_declaration(Style& style,
                                CascadeSlot& slot,
                                const CssDeclaration& declaration,
                                const CssSpecificity& specificity,
                                std::size_t source_order) {
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return;
    }
    if (apply_declaration(style, declaration.property, declaration.value)) {
        mark_slot(slot, declaration, specificity, source_order);
    }
}

void apply_declarations(Style& style,
                        CascadeSlots& slots,
                        const std::vector<CssDeclaration>& declarations,
                        const CssSpecificity& specificity,
                        std::size_t source_order) {
    for (const CssDeclaration& declaration : declarations) {
        CascadeSlot* slot = cascade_slot_for_property(slots, declaration.property);
        if (slot != nullptr) {
            apply_cascaded_declaration(style, *slot, declaration, specificity, source_order);
        }
    }
}

std::vector<CssDeclaration> parse_inline_style(const std::string& source) {
    std::vector<CssDeclaration> declarations;
    std::size_t index = 0;
    while (index < source.size()) {
        const std::size_t colon = source.find(':', index);
        if (colon == std::string::npos) {
            break;
        }
        const std::size_t semicolon = source.find(';', colon + 1);
        const std::size_t end = semicolon == std::string::npos ? source.size() : semicolon;
        CssDeclaration declaration;
        declaration.property = lowercase(trim(std::string_view(source).substr(index, colon - index)));
        declaration.value = trim(std::string_view(source).substr(colon + 1, end - colon - 1));
        if (!declaration.property.empty() && !declaration.value.empty()) {
            declarations.push_back(std::move(declaration));
        }
        index = end + 1;
    }
    return declarations;
}

Style default_style_for(const Node& node) {
    Style style;
    if (node.type == NodeType::Text) {
        style.display = Display::Inline;
        return style;
    }

    static constexpr std::array<std::string_view, 34> block_tags = {
        "document", "html", "body", "div", "p", "section", "article", "header", "footer", "main",
        "nav", "aside", "form", "fieldset", "dialog", "details", "summary", "blockquote", "address",
        "hgroup", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "table", "tr", "td",
        "th", "app-root"
    };
    static constexpr std::array<std::string_view, 8> hidden_tags = {
        "head", "script", "style", "meta", "link", "title", "template", "noscript"
    };

    if (std::find(hidden_tags.begin(), hidden_tags.end(), std::string_view(node.tag_name)) != hidden_tags.end()) {
        style.display = Display::None;
        return style;
    }
    if (std::find(block_tags.begin(), block_tags.end(), std::string_view(node.tag_name)) != block_tags.end()) {
        style.display = Display::Block;
    }
    if (node.tag_name == "h1") {
        style.font_size = 24;
        style.font_size_specified = true;
        style.margin.bottom = 8;
    } else if (node.tag_name == "h2") {
        style.font_size = 20;
        style.font_size_specified = true;
        style.margin.bottom = 6;
    } else if (node.tag_name == "p") {
        style.margin.bottom = 6;
    } else if (node.tag_name == "a") {
        style.color = Color{37, 99, 235, 255};
        style.color_specified = true;
    } else if (node.tag_name == "mark") {
        style.background_color = Color{254, 240, 138, 255};
        style.color = Color{0, 0, 0, 255};
        style.color_specified = true;
    } else if (node.tag_name == "blockquote") {
        style.margin = EdgeSizes{8, 0, 8, 0};
        style.padding.left = 12;
        style.border_width.left = 4;
        style.border_color = Color{203, 213, 225, 255};
    } else if (node.tag_name == "summary") {
        style.margin.bottom = 6;
    } else if (node.tag_name == "progress" || node.tag_name == "meter") {
        style.display = Display::InlineBlock;
        style.width = 160;
        style.height = 12;
        style.border_width = EdgeSizes{1, 1, 1, 1};
        style.border_color = Color{148, 163, 184, 255};
        style.border_radius = 3;
        style.background_color = Color{226, 232, 240, 255};
    } else if (node.tag_name == "button") {
        style.display = Display::InlineBlock;
        style.padding = EdgeSizes{4, 8, 4, 8};
        style.border_width = EdgeSizes{1, 1, 1, 1};
        style.border_color = Color{107, 114, 128, 255};
        style.background_color = Color{243, 244, 246, 255};
    } else if (node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea") {
        style.display = Display::InlineBlock;
        style.padding = EdgeSizes{4, 6, 4, 6};
        style.border_width = EdgeSizes{1, 1, 1, 1};
        style.border_color = Color{107, 114, 128, 255};
        style.background_color = Color{255, 255, 255, 255};
        style.min_width = 140;
        if (node.tag_name == "input" && (node.attribute("type") == "checkbox" || node.attribute("type") == "radio")) {
            style.width = 18;
            style.height = 18;
            style.min_width = 18;
            style.padding = EdgeSizes{};
        } else if (node.tag_name == "input" && node.attribute("type") == "color") {
            style.width = 44;
            style.height = 24;
            style.min_width = 44;
        } else if (node.tag_name == "input" && node.attribute("type") == "range") {
            style.width = 140;
            style.min_width = 120;
        }
    } else if (node.tag_name == "img" || node.tag_name == "picture") {
        style.display = Display::InlineBlock;
    } else if (node.tag_name == "dialog") {
        if (node.attributes.find("open") == node.attributes.end()) {
            style.display = Display::None;
        }
        style.padding = EdgeSizes{8, 8, 8, 8};
        style.border_width = EdgeSizes{1, 1, 1, 1};
        style.border_color = Color{107, 114, 128, 255};
        style.background_color = Color{255, 255, 255, 255};
    }
    return style;
}

} // namespace

std::vector<CssSelectorPart> parse_css_selector_parts(std::string_view selector) {
    std::vector<CssSelectorPart> parts;
    std::size_t end = selector.size();
    bool next_is_child = false;
    while (end > 0) {
        while (end > 0 && std::isspace(static_cast<unsigned char>(selector[end - 1])) != 0) {
            --end;
        }
        if (end == 0) {
            break;
        }
        if (selector[end - 1] == '>') {
            next_is_child = true;
            --end;
            continue;
        }

        std::size_t begin = end;
        int bracket_depth = 0;
        while (begin > 0) {
            const char ch = selector[begin - 1];
            if (ch == ']') {
                ++bracket_depth;
            } else if (ch == '[' && bracket_depth > 0) {
                --bracket_depth;
            } else if (bracket_depth == 0 && ch == '>') {
                break;
            } else if (bracket_depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
                break;
            }
            --begin;
        }

        CssSelectorPart part;
        part.compound = trim(selector.substr(begin, end - begin));
        part.combinator_to_left = next_is_child ? CssSelectorCombinator::Child : CssSelectorCombinator::Descendant;
        if (!part.compound.empty()) {
            parts.push_back(std::move(part));
        }
        next_is_child = begin > 0 && selector[begin - 1] == '>';
        end = begin;
    }
    return parts;
}

CssRuleIndexKey build_css_rule_index_key(const std::vector<CssSelectorPart>& selector_parts) {
    CssRuleIndexKey key;
    if (selector_parts.empty()) {
        key.universal = true;
        return key;
    }

    const std::string& rightmost = selector_parts.front().compound;
    key.id = extract_id_from_compound(rightmost);
    if (!key.id.empty()) {
        return key;
    }
    key.class_name = extract_class_from_compound(rightmost);
    if (!key.class_name.empty()) {
        return key;
    }
    key.tag_name = extract_tag_from_compound(rightmost);
    if (!key.tag_name.empty()) {
        return key;
    }
    key.universal = true;
    return key;
}

void CssStyleSheet::push_back(CssRule rule) {
    rules_.push_back(std::move(rule));
}

std::size_t CssStyleSheet::size() const {
    return rules_.size();
}

bool CssStyleSheet::empty() const {
    return rules_.empty();
}

CssRule& CssStyleSheet::operator[](std::size_t index) {
    return rules_[index];
}

const CssRule& CssStyleSheet::operator[](std::size_t index) const {
    return rules_[index];
}

CssStyleSheet::iterator CssStyleSheet::begin() {
    return rules_.begin();
}

CssStyleSheet::iterator CssStyleSheet::end() {
    return rules_.end();
}

CssStyleSheet::const_iterator CssStyleSheet::begin() const {
    return rules_.begin();
}

CssStyleSheet::const_iterator CssStyleSheet::end() const {
    return rules_.end();
}

const CssStyleSheet::RuleList& CssStyleSheet::rules() const {
    return rules_;
}

StyleResolver::StyleResolver(Stylesheet stylesheet)
    : stylesheet_(std::move(stylesheet)) {
    build_rule_index();
}

void StyleResolver::build_rule_index() {
    for (const CssRule& rule : stylesheet_) {
        if (!rule.index_key.id.empty()) {
            id_rules_[rule.index_key.id].push_back(&rule);
        } else if (!rule.index_key.class_name.empty()) {
            class_rules_[rule.index_key.class_name].push_back(&rule);
        } else if (!rule.index_key.tag_name.empty()) {
            tag_rules_[rule.index_key.tag_name].push_back(&rule);
        } else {
            universal_rules_.push_back(&rule);
        }
    }
}

Style StyleResolver::resolve(const Node& node) const {
    Style style = default_style_for(node);
    CascadeSlots slots;
    std::vector<const CssRule*> candidates;
    candidates.reserve(16);
    const auto already_added = [&](const CssRule* candidate) {
        return std::find(candidates.begin(), candidates.end(), candidate) != candidates.end();
    };
    const auto append_bucket = [&](const std::vector<const CssRule*>* bucket) {
        if (bucket == nullptr) {
            return;
        }
        for (const CssRule* rule : *bucket) {
            if (!already_added(rule)) {
                candidates.push_back(rule);
            }
        }
    };

    append_bucket(&universal_rules_);
    if (node.type == NodeType::Element) {
        const auto id_it = id_rules_.find(node.attribute("id"));
        append_bucket(id_it == id_rules_.end() ? nullptr : &id_it->second);

        std::size_t index = 0;
        const std::string& classes = node.attribute("class");
        while (index < classes.size()) {
            while (index < classes.size() && std::isspace(static_cast<unsigned char>(classes[index])) != 0) {
                ++index;
            }
            const std::size_t begin = index;
            while (index < classes.size() && std::isspace(static_cast<unsigned char>(classes[index])) == 0) {
                ++index;
            }
            if (begin != index) {
                const auto class_it = class_rules_.find(classes.substr(begin, index - begin));
                append_bucket(class_it == class_rules_.end() ? nullptr : &class_it->second);
            }
        }

        const auto tag_it = tag_rules_.find(node.tag_name);
        append_bucket(tag_it == tag_rules_.end() ? nullptr : &tag_it->second);
    }

    std::sort(candidates.begin(), candidates.end(), [](const CssRule* left, const CssRule* right) {
        return left->source_order < right->source_order;
    });

    for (const CssRule* rule : candidates) {
        if (matches_rule(node, *rule)) {
            apply_declarations(style, slots, rule->declarations, rule->specificity, rule->source_order);
        }
    }
    if (node.type == NodeType::Element) {
        CssSpecificity inline_specificity;
        inline_specificity.ids = 1;
        inline_specificity.classes = 0;
        inline_specificity.elements = 0;
        apply_declarations(style, slots, parse_inline_style(node.attribute("style")), inline_specificity,
                           static_cast<std::size_t>(-1));
    }
    return style;
}

} // namespace wearweb
