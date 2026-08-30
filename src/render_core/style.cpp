#include "render_core/style.h"

#include "render_core/form_control.h"
#include "render_core/feature_config.h"
#include "render_core/text_backend.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

#if !JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
bool is_flex_grid_property(const std::string& property) {
    return property == "align-content" || property == "align-items" || property == "align-self" ||
        property == "column-gap" || property == "flex" || property == "flex-basis" ||
        property == "flex-direction" || property == "flex-grow" || property == "flex-shrink" ||
        property == "flex-wrap" || property == "gap" || property == "grid-auto-rows" ||
        property == "grid-column" || property == "grid-row" ||
        property == "grid-template-columns" || property == "grid-template-rows" ||
        property == "justify-content" || property == "order" || property == "place-content" ||
        property == "place-items" || property == "place-self" || property == "row-gap";
}
#endif

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
constexpr int kDefaultViewportWidthPx = 360;
constexpr int kDefaultViewportHeightPx = 240;
constexpr float kMaxTransformTranslationPx = 100000.0F;

std::string lowercase(std::string value);
bool parse_length_px(const std::string& raw_value, int& output, int em_base = kRootFontSizePx);

bool round_finite_float_to_int(float value, int& output) {
    if (!std::isfinite(value)) {
        return false;
    }
    const double rounded = value >= 0.0F
        ? static_cast<double>(value) + 0.5
        : static_cast<double>(value) - 0.5;
    if (rounded < static_cast<double>(std::numeric_limits<int>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    output = static_cast<int>(rounded);
    return true;
}

bool add_lengths(int left, std::int64_t right, int& output) {
    const std::int64_t result = static_cast<std::int64_t>(left) + right;
    if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
        return false;
    }
    output = static_cast<int>(result);
    return true;
}

bool is_css_ident_char(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_' || ch == '-';
}

bool selector_contains_pseudo(std::string_view selector, std::string_view pseudo) {
    std::size_t index = 0;
    while ((index = selector.find(':', index)) != std::string_view::npos) {
        if (index + 1 < selector.size() && selector[index + 1] == ':') {
            index += 2;
            continue;
        }
        const std::size_t name_begin = index + 1;
        if (selector.substr(name_begin, pseudo.size()) == pseudo) {
            const std::size_t after = name_begin + pseudo.size();
            if (after >= selector.size() || !is_css_ident_char(selector[after])) {
                return true;
            }
        }
        ++index;
    }
    return false;
}

void add_interaction_hints_for_selector(std::string_view selector, InteractionInvalidationHints& hints) {
    hints.hover = hints.hover || selector_contains_pseudo(selector, "hover");
    hints.active = hints.active || selector_contains_pseudo(selector, "active");
    hints.focus = hints.focus ||
        selector_contains_pseudo(selector, "focus") ||
        selector_contains_pseudo(selector, "focus-within");
}

std::vector<std::string> split_function_arguments(std::string_view body) {
    std::vector<std::string> args;
    std::size_t begin = 0;
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < body.size(); ++index) {
        const char ch = body[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < body.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')' && depth > 0) {
            --depth;
        } else if (ch == ',' && depth == 0) {
            args.push_back(trim(body.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    args.push_back(trim(body.substr(begin)));
    return args;
}

bool parse_length_function(const std::string& value, int& output, int em_base) {
    const auto function_body = [&](std::string_view name, std::string_view& body) {
        if (value.rfind(name, 0) != 0 || value.size() <= name.size() + 2 ||
            value[name.size()] != '(' || value.back() != ')') {
            return false;
        }
        body = std::string_view(value).substr(name.size() + 1, value.size() - name.size() - 2);
        return true;
    };

    std::string_view body;
    if (function_body("clamp", body)) {
        const std::vector<std::string> args = split_function_arguments(body);
        if (args.size() != 3) {
            return false;
        }
        int min_value = 0;
        int preferred_value = 0;
        int max_value = 0;
        if (!parse_length_px(args[0], min_value, em_base) ||
            !parse_length_px(args[2], max_value, em_base)) {
            return false;
        }
        if (!parse_length_px(args[1], preferred_value, em_base)) {
            preferred_value = min_value;
        }
        output = std::max(min_value, std::min(preferred_value, max_value));
        return true;
    }
    if (function_body("min", body) || function_body("max", body)) {
        const bool is_min = value.rfind("min", 0) == 0;
        const std::vector<std::string> args = split_function_arguments(body);
        bool have_value = false;
        int result = 0;
        for (const std::string& arg : args) {
            int parsed = 0;
            if (!parse_length_px(arg, parsed, em_base)) {
                continue;
            }
            result = have_value ? (is_min ? std::min(result, parsed) : std::max(result, parsed)) : parsed;
            have_value = true;
        }
        if (!have_value) {
            return false;
        }
        output = result;
        return true;
    }
    if (function_body("calc", body)) {
        const std::string expr = trim(body);
        std::size_t op = std::string::npos;
        char op_char = '\0';
        for (std::size_t index = 1; index + 1 < expr.size(); ++index) {
            if ((expr[index] == '+' || expr[index] == '-') &&
                std::isspace(static_cast<unsigned char>(expr[index - 1])) != 0 &&
                std::isspace(static_cast<unsigned char>(expr[index + 1])) != 0) {
                op = index;
                op_char = expr[index];
                break;
            }
        }
        if (op == std::string::npos) {
            return parse_length_px(expr, output, em_base);
        }
        int left = 0;
        int right = 0;
        if (!parse_length_px(expr.substr(0, op), left, em_base) ||
            !parse_length_px(expr.substr(op + 1), right, em_base)) {
            return false;
        }
        return op_char == '-'
            ? add_lengths(left, -static_cast<std::int64_t>(right), output)
            : add_lengths(left, right, output);
    }
    return false;
}

std::vector<std::string> split_whitespace_components(std::string_view value) {
    std::vector<std::string> tokens;
    std::size_t begin = 0;
    int depth = 0;
    char quote = '\0';
    const auto flush = [&](std::size_t end) {
        if (end > begin) {
            std::string token = trim(value.substr(begin, end - begin));
            if (!token.empty()) {
                tokens.push_back(std::move(token));
            }
        }
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < value.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')' && depth > 0) {
            --depth;
        } else if (depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
            flush(index);
            begin = index + 1;
        }
    }
    flush(value.size());
    return tokens;
}

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
bool has_top_level_comma(std::string_view value) {
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < value.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
        } else if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')' && depth > 0) {
            --depth;
        } else if (ch == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}
#endif

bool parse_length_px(const std::string& raw_value, int& output, int em_base) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }
    const std::string lowered = lowercase(value);
    if (parse_length_function(lowered, output, em_base)) {
        return true;
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

    float pixels = 0.0F;
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
        pixels = parsed * static_cast<float>(kDefaultViewportHeightPx) / 100.0F;
        end += 2;
    } else if (std::strncmp(end, "vw", 2) == 0) {
        pixels = parsed * static_cast<float>(kDefaultViewportWidthPx) / 100.0F;
        end += 2;
    } else if (*end == '%') {
        pixels = parsed * static_cast<float>(kDefaultViewportWidthPx) / 100.0F;
        ++end;
    } else {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    return round_finite_float_to_int(pixels, output);
}

bool parse_float(const std::string& raw_value, float& output) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed)) {
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

bool parse_percentage_int(const std::string& raw_value, int& output) {
    const std::string value = trim(raw_value);
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
    if (end == nullptr || *end != '%') {
        return false;
    }
    ++end;
    while (std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    int rounded = 0;
    if (!round_finite_float_to_int(parsed, rounded)) {
        return false;
    }
    output = std::max(-1000, std::min(1000, rounded));
    return true;
}

bool parse_time_ms(const std::string& raw_value, std::uint32_t& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed) || parsed < 0.0F) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    float milliseconds = 0.0F;
    if (end != nullptr && std::strncmp(end, "ms", 2) == 0) {
        milliseconds = parsed;
        end += 2;
    } else if (end != nullptr && *end == 's') {
        milliseconds = parsed * 1000.0F;
        ++end;
    } else {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    if (!std::isfinite(milliseconds)) {
        return false;
    }
    output = static_cast<std::uint32_t>(std::min(60000.0F, milliseconds + 0.5F));
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
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

bool parse_positive_ratio_number(const std::string& raw_value, int& output) {
    float parsed = 0.0F;
    if (!parse_float(raw_value, parsed) || parsed <= 0.0F) {
        return false;
    }
    if (parsed >= 1000.0F) {
        output = 1000000;
        return true;
    }
    return round_finite_float_to_int(parsed * 1000.0F, output) && output > 0;
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

bool parse_grid_placement(const std::string& raw_value, int& start, int& span) {
    const std::string value = lowercase(trim(raw_value));
    start = -1;
    span = 1;
    const std::size_t slash = value.find('/');
    const std::string first = trim(value.substr(0, slash));
    const std::string second = slash == std::string::npos ? std::string{} : trim(value.substr(slash + 1));

    const auto parse_line = [](const std::string& token, int& line) {
        return parse_integer(token, line) && line > 0 && line <= std::numeric_limits<std::int16_t>::max();
    };
    const auto parse_span = [](const std::string& token, int& parsed_span) {
        return parse_span_value(token, parsed_span) && parsed_span > 0;
    };
    const auto is_span = [](const std::string& token) {
        return token.rfind("span", 0) == 0 &&
            (token.size() == 4 || std::isspace(static_cast<unsigned char>(token[4])) != 0);
    };

    if (slash == std::string::npos) {
        int parsed_span = 1;
        if (is_span(first) && parse_span(first, parsed_span)) {
            span = parsed_span;
            return true;
        }
        int line = 0;
        if (!parse_line(first, line)) {
            return false;
        }
        start = line - 1;
        return true;
    }

    int first_line = 0;
    if (!parse_line(first, first_line)) {
        return false;
    }
    start = first_line - 1;
    int parsed_span = 1;
    if (is_span(second) && parse_span(second, parsed_span)) {
        span = parsed_span;
        return true;
    }
    int second_line = 0;
    if (!parse_line(second, second_line) || second_line <= first_line) {
        return false;
    }
    span = second_line - first_line;
    return true;
}

bool parse_font_weight(const std::string& raw_value, int& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "normal") {
        output = 400;
        return true;
    }
    if (value == "bold" || value == "bolder") {
        output = 700;
        return true;
    }
    if (value == "lighter") {
        output = 300;
        return true;
    }
    int parsed = 0;
    if (!parse_integer(value, parsed) || parsed < 1) {
        return false;
    }
    output = std::max(100, std::min(900, ((parsed + 50) / 100) * 100));
    return true;
}

bool parse_flex_factor(const std::string& raw_value, int& output) {
    float parsed = 0.0F;
    if (!parse_float(raw_value, parsed) || parsed < 0.0F) {
        return false;
    }
    if (parsed >= 1000.0F) {
        output = 1000000;
        return true;
    }
    return round_finite_float_to_int(parsed * 1000.0F, output) && output >= 0;
}

bool parse_position_inset(const std::string& raw_value, int font_size, int& output, bool& specified) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "auto") {
        output = 0;
        specified = false;
        return true;
    }
    int px = 0;
    if (!parse_length_px(value, px, font_size)) {
        return false;
    }
    output = px;
    specified = true;
    return true;
}

bool parse_flex_basis_value(const std::string& raw_value, int font_size, int& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "auto") {
        output = -1;
        return true;
    }
    return parse_length_px(value, output, font_size);
}

bool parse_flex_shorthand(const std::string& raw_value,
                          int font_size,
                          int& grow,
                          int& shrink,
                          int& basis) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "none") {
        grow = 0;
        shrink = 0;
        basis = -1;
        return true;
    }
    if (value == "auto") {
        grow = 1000;
        shrink = 1000;
        basis = -1;
        return true;
    }

    std::istringstream stream(value);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        if (tokens.size() >= 3) {
            return false;
        }
        tokens.push_back(token);
    }
    if (tokens.empty()) {
        return false;
    }

    int parsed_grow = 0;
    if (!parse_flex_factor(tokens[0], parsed_grow)) {
        return false;
    }
    int parsed_shrink = 1000;
    int parsed_basis = 0;
    if (tokens.size() == 1) {
        grow = parsed_grow;
        shrink = parsed_shrink;
        basis = parsed_basis;
        return true;
    }

    int second_factor = 0;
    if (parse_flex_factor(tokens[1], second_factor)) {
        parsed_shrink = second_factor;
        if (tokens.size() == 3 && !parse_flex_basis_value(tokens[2], font_size, parsed_basis)) {
            return false;
        }
    } else if (tokens.size() == 2) {
        if (!parse_flex_basis_value(tokens[1], font_size, parsed_basis)) {
            return false;
        }
    } else {
        return false;
    }

    grow = parsed_grow;
    shrink = parsed_shrink;
    basis = parsed_basis;
    return true;
}

bool parse_list_style_type(const std::string& raw_value, ListStyleType& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "none") {
        output = ListStyleType::None;
        return true;
    }
    if (value == "disc" || value == "circle" || value == "square") {
        output = ListStyleType::Disc;
        return true;
    }
    if (value == "decimal" || value == "decimal-leading-zero") {
        output = ListStyleType::Decimal;
        return true;
    }
    return false;
}

bool parse_animatable_property(const std::string& raw_value, AnimatableProperty& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "all") {
        output = AnimatableProperty::All;
        return true;
    }
    if (value == "opacity") {
        output = AnimatableProperty::Opacity;
        return true;
    }
    if (value == "transform") {
        output = AnimatableProperty::Transform;
        return true;
    }
    if (value == "background-color" || value == "background") {
        output = AnimatableProperty::BackgroundColor;
        return true;
    }
    if (value == "color") {
        output = AnimatableProperty::Color;
        return true;
    }
    return false;
}

bool parse_timing_function(const std::string& raw_value,
                           AnimationTimingFunction& output,
                           std::uint64_t& cubic_bezier) {
    const std::string value = lowercase(trim(raw_value));
    cubic_bezier = 0;
    if (value == "linear") {
        output = AnimationTimingFunction::Linear;
        return true;
    }
    if (value == "ease") {
        output = AnimationTimingFunction::Ease;
        return true;
    }
    if (value == "ease-in") {
        output = AnimationTimingFunction::EaseIn;
        return true;
    }
    if (value == "ease-out") {
        output = AnimationTimingFunction::EaseOut;
        return true;
    }
    if (value == "ease-in-out") {
        output = AnimationTimingFunction::EaseInOut;
        return true;
    }
    constexpr std::string_view prefix = "cubic-bezier(";
    if (value.rfind(prefix, 0) != 0 || value.back() != ')') {
        return false;
    }
    const std::vector<std::string> args = split_function_arguments(
        std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() != 4) {
        return false;
    }
    float points[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (!parse_float(args[index], points[index])) {
            return false;
        }
    }
    if (points[0] < 0.0F || points[0] > 1.0F || points[2] < 0.0F || points[2] > 1.0F ||
        points[1] < -2.0F || points[1] > 2.0F || points[3] < -2.0F || points[3] > 2.0F) {
        return false;
    }
    const auto pack = [](float point) {
        const float scaled = point * 8192.0F;
        const int rounded = static_cast<int>(scaled >= 0.0F ? scaled + 0.5F : scaled - 0.5F);
        return static_cast<std::uint16_t>(static_cast<std::int16_t>(rounded));
    };
    cubic_bezier = static_cast<std::uint64_t>(pack(points[0])) |
        (static_cast<std::uint64_t>(pack(points[1])) << 16U) |
        (static_cast<std::uint64_t>(pack(points[2])) << 32U) |
        (static_cast<std::uint64_t>(pack(points[3])) << 48U);
    output = AnimationTimingFunction::CubicBezier;
    return true;
}

std::vector<std::string> split_comma_components(std::string_view value) {
    std::vector<std::string> components;
    std::size_t begin = 0;
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < value.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')' && depth > 0) {
            --depth;
        } else if (ch == ',' && depth == 0) {
            components.push_back(trim(value.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    components.push_back(trim(value.substr(begin)));
    return components;
}

bool parse_font_family_hash(const std::string& raw_value, std::uint32_t& output) {
    const std::vector<std::string> components = split_comma_components(raw_value);
    for (const std::string& component : components) {
        const std::string family = trim(component);
        if (family.empty()) {
            continue;
        }
        const std::string lowered = lowercase(family);
        if (lowered == "initial" || lowered == "revert" || lowered == "unset") {
            output = 0;
            return true;
        }
        if (lowered == "inherit") {
            return false;
        }
        output = normalized_font_family_hash(family);
        return true;
    }
    return false;
}

void set_style_transitions(Style& style, const std::vector<StyleTransition>& transitions) {
    const std::size_t count = std::min<std::size_t>(kMaxStyleTransitions, transitions.size());
    style.transitions.assign(transitions.begin(), transitions.begin() + static_cast<std::ptrdiff_t>(count));
}

void set_style_animations(Style& style, const std::vector<StyleAnimation>& animations) {
    const std::size_t count = std::min<std::size_t>(kMaxStyleAnimations, animations.size());
    style.animations.assign(animations.begin(), animations.begin() + static_cast<std::ptrdiff_t>(count));
}

bool parse_transition_shorthand(const std::string& raw_value, Style& style) {
    const std::string lowered = lowercase(trim(raw_value));
    if (lowered == "none") {
        style.transitions.clear();
        return true;
    }
    std::vector<StyleTransition> parsed;
    for (const std::string& component : split_comma_components(raw_value)) {
        if (component.empty()) {
            continue;
        }
        StyleTransition transition;
        bool have_property = false;
        bool have_duration = false;
        bool have_delay = false;
        bool have_timing = false;
        for (const std::string& token : split_whitespace_components(component)) {
            AnimationTimingFunction timing = AnimationTimingFunction::Ease;
            std::uint64_t cubic_bezier = 0;
            if (!have_timing && parse_timing_function(token, timing, cubic_bezier)) {
                transition.timing = timing;
                transition.cubic_bezier = cubic_bezier;
                have_timing = true;
                continue;
            }
            std::uint32_t time = 0;
            if (parse_time_ms(token, time)) {
                if (!have_duration) {
                    transition.duration_ms = time;
                    have_duration = true;
                    continue;
                }
                if (!have_delay) {
                    transition.delay_ms = time;
                    have_delay = true;
                    continue;
                }
                return false;
            }
            AnimatableProperty property = AnimatableProperty::All;
            if (!have_property && parse_animatable_property(token, property)) {
                transition.property = property;
                have_property = true;
                continue;
            }
            return false;
        }
        if (have_duration && transition.duration_ms > 0) {
            parsed.push_back(transition);
        }
    }
    set_style_transitions(style, parsed);
    return true;
}

bool parse_iteration_count(const std::string& raw_value, std::uint16_t& count, bool& infinite) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "infinite") {
        count = 1;
        infinite = true;
        return true;
    }
    int parsed = 0;
    if (!parse_integer(value, parsed) || parsed < 1) {
        return false;
    }
    count = static_cast<std::uint16_t>(std::min(65535, parsed));
    infinite = false;
    return true;
}

bool parse_animation_direction(const std::string& raw_value, AnimationDirection& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "normal") {
        output = AnimationDirection::Normal;
        return true;
    }
    if (value == "alternate") {
        output = AnimationDirection::Alternate;
        return true;
    }
    return false;
}

bool parse_animation_fill_mode(const std::string& raw_value, AnimationFillMode& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "none") {
        output = AnimationFillMode::None;
        return true;
    }
    if (value == "forwards") {
        output = AnimationFillMode::Forwards;
        return true;
    }
    if (value == "backwards") {
        output = AnimationFillMode::Backwards;
        return true;
    }
    if (value == "both") {
        output = AnimationFillMode::Both;
        return true;
    }
    return false;
}

bool is_animation_name_token(const std::string& raw_value) {
    const std::string value = trim(raw_value);
    if (value.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(value.front());
    if (!(std::isalpha(first) != 0 || value.front() == '_' || value.front() == '-')) {
        return false;
    }
    for (char ch : value) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (!(std::isalnum(byte) != 0 || ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

bool parse_animation_shorthand(const std::string& raw_value, Style& style) {
    const std::string lowered = lowercase(trim(raw_value));
    if (lowered == "none") {
        style.animations.clear();
        return true;
    }
    std::vector<StyleAnimation> parsed;
    for (const std::string& component : split_comma_components(raw_value)) {
        if (component.empty()) {
            continue;
        }
        StyleAnimation animation;
        bool have_name = false;
        bool have_duration = false;
        bool have_delay = false;
        bool have_timing = false;
        bool have_iteration_count = false;
        bool have_direction = false;
        bool have_fill_mode = false;
        for (const std::string& token : split_whitespace_components(component)) {
            AnimationTimingFunction timing = AnimationTimingFunction::Ease;
            std::uint64_t cubic_bezier = 0;
            if (!have_timing && parse_timing_function(token, timing, cubic_bezier)) {
                animation.timing = timing;
                animation.cubic_bezier = cubic_bezier;
                have_timing = true;
                continue;
            }
            std::uint32_t time = 0;
            if (parse_time_ms(token, time)) {
                if (!have_duration) {
                    animation.duration_ms = time;
                    have_duration = true;
                    continue;
                }
                if (!have_delay) {
                    animation.delay_ms = time;
                    have_delay = true;
                    continue;
                }
                return false;
            }
            std::uint16_t iterations = 1;
            bool infinite = false;
            if (!have_iteration_count && parse_iteration_count(token, iterations, infinite)) {
                animation.iteration_count = iterations;
                animation.infinite = infinite;
                have_iteration_count = true;
                continue;
            }
            AnimationDirection direction = AnimationDirection::Normal;
            if (!have_direction && parse_animation_direction(token, direction)) {
                animation.direction = direction;
                have_direction = true;
                continue;
            }
            AnimationFillMode fill_mode = AnimationFillMode::None;
            if (!have_fill_mode && parse_animation_fill_mode(token, fill_mode)) {
                animation.fill_mode = fill_mode;
                have_fill_mode = true;
                continue;
            }
            const std::string name = trim(token);
            if (!have_name && name != "none" && is_animation_name_token(name)) {
                animation.name = name;
                have_name = true;
                continue;
            }
            return false;
        }
        if (have_name && have_duration && animation.duration_ms > 0) {
            parsed.push_back(std::move(animation));
        }
    }
    set_style_animations(style, parsed);
    return true;
}

void ensure_animation_entries(Style& style) {
    if (style.animations.empty()) {
        style.animations.emplace_back();
    }
}

bool parse_animation_longhand(const std::string& property, const std::string& raw_value, Style& style) {
    if (property == "animation-name") {
        const std::string lowered = lowercase(trim(raw_value));
        if (lowered == "none") {
            style.animations.clear();
            return true;
        }
        std::vector<std::string> names;
        for (const std::string& component : split_comma_components(raw_value)) {
            const std::string name = trim(component);
            if (!is_animation_name_token(name)) {
                return false;
            }
            names.push_back(name);
        }
        if (names.empty()) {
            return false;
        }
        const std::size_t count = std::min<std::size_t>(
            kMaxStyleAnimations, std::max(style.animations.size(), names.size()));
        style.animations.resize(count);
        for (std::size_t index = 0; index < style.animations.size(); ++index) {
            style.animations[index].name = names[std::min(index, names.size() - 1)];
        }
        return true;
    }

    ensure_animation_entries(style);
    const std::vector<std::string> values = split_comma_components(raw_value);
    if (values.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < style.animations.size(); ++index) {
        const std::string& value = values[std::min(index, values.size() - 1)];
        if (property == "animation-duration") {
            std::uint32_t ms = 0;
            if (!parse_time_ms(value, ms)) {
                return false;
            }
            style.animations[index].duration_ms = ms;
        } else if (property == "animation-delay") {
            std::uint32_t ms = 0;
            if (!parse_time_ms(value, ms)) {
                return false;
            }
            style.animations[index].delay_ms = ms;
        } else if (property == "animation-timing-function") {
            AnimationTimingFunction timing = AnimationTimingFunction::Ease;
            std::uint64_t cubic_bezier = 0;
            if (!parse_timing_function(value, timing, cubic_bezier)) {
                return false;
            }
            style.animations[index].timing = timing;
            style.animations[index].cubic_bezier = cubic_bezier;
        } else if (property == "animation-iteration-count") {
            std::uint16_t iterations = 1;
            bool infinite = false;
            if (!parse_iteration_count(value, iterations, infinite)) {
                return false;
            }
            style.animations[index].iteration_count = iterations;
            style.animations[index].infinite = infinite;
        } else if (property == "animation-direction") {
            AnimationDirection direction = AnimationDirection::Normal;
            if (!parse_animation_direction(value, direction)) {
                return false;
            }
            style.animations[index].direction = direction;
        } else if (property == "animation-fill-mode") {
            AnimationFillMode fill_mode = AnimationFillMode::None;
            if (!parse_animation_fill_mode(value, fill_mode)) {
                return false;
            }
            style.animations[index].fill_mode = fill_mode;
        }
    }
    return true;
}

bool parse_transition_longhand(const std::string& property, const std::string& raw_value, Style& style) {
    if (property == "transition-property") {
        std::vector<StyleTransition> parsed;
        for (const std::string& component : split_comma_components(raw_value)) {
            AnimatableProperty animatable = AnimatableProperty::All;
            if (!parse_animatable_property(component, animatable)) {
                return false;
            }
            StyleTransition transition;
            transition.property = animatable;
            parsed.push_back(transition);
        }
        set_style_transitions(style, parsed);
        return true;
    }

    if (style.transitions.empty()) {
        StyleTransition transition;
        transition.property = AnimatableProperty::All;
        style.transitions.push_back(transition);
    }
    const std::vector<std::string> values = split_comma_components(raw_value);
    if (values.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < style.transitions.size(); ++index) {
        const std::string& value = values[std::min(index, values.size() - 1)];
        if (property == "transition-duration") {
            std::uint32_t ms = 0;
            if (!parse_time_ms(value, ms)) {
                return false;
            }
            style.transitions[index].duration_ms = ms;
        } else if (property == "transition-delay") {
            std::uint32_t ms = 0;
            if (!parse_time_ms(value, ms)) {
                return false;
            }
            style.transitions[index].delay_ms = ms;
        } else if (property == "transition-timing-function") {
            AnimationTimingFunction timing = AnimationTimingFunction::Ease;
            std::uint64_t cubic_bezier = 0;
            if (!parse_timing_function(value, timing, cubic_bezier)) {
                return false;
            }
            style.transitions[index].timing = timing;
            style.transitions[index].cubic_bezier = cubic_bezier;
        }
    }
    style.transitions.erase(
        std::remove_if(style.transitions.begin(), style.transitions.end(), [](const StyleTransition& transition) {
            return transition.duration_ms == 0;
        }),
        style.transitions.end());
    return true;
}

bool parse_simple_grid_template_columns(const std::string& raw_value,
                                        std::array<int, 4>& widths,
                                        int& count,
                                        int em_base) {
    const std::string value = lowercase(trim(raw_value));
    if (value.rfind("repeat(", 0) == 0 && value.back() == ')') {
        const std::size_t comma = value.find(',');
        if (comma == std::string::npos) {
            return false;
        }
        int repeat_count = 0;
        if (!parse_integer(trim(value.substr(7, comma - 7)), repeat_count) ||
            repeat_count < 2 || repeat_count > static_cast<int>(widths.size())) {
            return false;
        }
        const std::string track = trim(value.substr(comma + 1, value.size() - comma - 2));
        int width = 0;
        int stored_width = 0;
        if (parse_length_px(track, width, em_base)) {
            stored_width = std::max(1, width);
        } else if (track.rfind("minmax(", 0) == 0 && track.back() == ')') {
            const std::size_t track_comma = track.find(',');
            if (track_comma == std::string::npos) {
                return false;
            }
            const std::string min_track = trim(track.substr(7, track_comma - 7));
            const std::string max_track = trim(track.substr(track_comma + 1, track.size() - track_comma - 2));
            int min_width = 0;
            if (!parse_length_px(min_track, min_width, em_base)) {
                return false;
            }
            if (max_track == "auto" || max_track == "1fr" ||
                (!max_track.empty() && max_track.back() == 'r' && max_track.size() >= 2 &&
                 max_track[max_track.size() - 2] == 'f')) {
                stored_width = min_width > 0 ? std::max(1, min_width) : 0;
            } else {
                return false;
            }
        } else if (track == "auto" || track == "1fr" || track == "min-content" || track == "max-content" ||
                   (!track.empty() && track.back() == 'r' && track.size() >= 2 && track[track.size() - 2] == 'f')) {
            stored_width = 0;
        } else {
            return false;
        }
        widths = std::array<int, 4>{{0, 0, 0, 0}};
        for (int index = 0; index < repeat_count; ++index) {
            widths[static_cast<std::size_t>(index)] = stored_width;
        }
        count = repeat_count;
        return true;
    }

    std::istringstream stream(value);
    std::string token;
    std::array<int, 4> parsed{{0, 0, 0, 0}};
    int parsed_count = 0;
    while (stream >> token) {
        if (parsed_count >= static_cast<int>(parsed.size())) {
            return false;
        }
        int width = 0;
        if (parse_length_px(token, width, em_base)) {
            parsed[static_cast<std::size_t>(parsed_count)] = std::max(1, width);
        } else if (token == "auto" || token == "1fr" || token == "min-content" || token == "max-content") {
            parsed[static_cast<std::size_t>(parsed_count)] = 0;
        } else if (!token.empty() && token.back() == 'f' && token.size() >= 2 && token[token.size() - 2] == 'r') {
            parsed[static_cast<std::size_t>(parsed_count)] = 0;
        } else {
            return false;
        }
        ++parsed_count;
    }
    if (parsed_count < 2) {
        return false;
    }
    widths = parsed;
    count = parsed_count;
    return true;
}

bool parse_simple_grid_template_rows(const std::string& raw_value,
                                     std::array<std::int16_t, 4>& heights,
                                     int& count,
                                     int em_base) {
    const std::string value = lowercase(trim(raw_value));
    std::istringstream stream(value);
    std::string token;
    std::array<std::int16_t, 4> parsed{{0, 0, 0, 0}};
    int parsed_count = 0;
    while (stream >> token) {
        if (parsed_count >= static_cast<int>(parsed.size())) {
            return false;
        }
        int height = 0;
        if (parse_length_px(token, height, em_base)) {
            if (height > std::numeric_limits<std::int16_t>::max()) {
                return false;
            }
            parsed[static_cast<std::size_t>(parsed_count)] = static_cast<std::int16_t>(std::max(1, height));
        } else if (token == "1fr") {
            parsed[static_cast<std::size_t>(parsed_count)] = 0;
        } else {
            return false;
        }
        ++parsed_count;
    }
    if (parsed_count < 2) {
        return false;
    }
    heights = parsed;
    count = parsed_count;
    return true;
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
    const std::vector<std::string> tokens = split_whitespace_components(value);
    std::array<int, 4> values{0, 0, 0, 0};
    const int count = static_cast<int>(tokens.size());
    if (count == 0 || count > 4) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        if (!parse_length_px(tokens[static_cast<std::size_t>(index)],
                             values[static_cast<std::size_t>(index)],
                             em_base)) {
            return false;
        }
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
    const std::vector<std::string> tokens = split_whitespace_components(value);
    std::array<int, 4> values{0, 0, 0, 0};
    std::array<bool, 4> auto_values{false, false, false, false};
    const int count = static_cast<int>(tokens.size());
    if (count == 0 || count > 4) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        const std::string& token = tokens[static_cast<std::size_t>(index)];
        if (token == "auto") {
            auto_values[static_cast<std::size_t>(index)] = true;
        } else if (!parse_length_px(token, values[static_cast<std::size_t>(index)], em_base)) {
            return false;
        }
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

bool parse_margin_side_px(const std::string& raw_value, int& output, bool& is_auto, int em_base) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "auto") {
        output = 0;
        is_auto = true;
        return true;
    }
    if (!parse_length_px(value, output, em_base)) {
        return false;
    }
    is_auto = false;
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
    const auto parse_hsl_function = [&](std::string_view body) {
        std::string normalized;
        normalized.reserve(body.size());
        for (const char ch : body) {
            normalized.push_back(ch == ',' || ch == '/' ? ' ' : ch);
        }
        std::istringstream stream(normalized);
        std::string hue_text;
        std::string saturation_text;
        std::string lightness_text;
        std::string alpha_text;
        if (!(stream >> hue_text >> saturation_text >> lightness_text)) {
            return false;
        }
        if (stream >> alpha_text) {
            std::string extra;
            if (stream >> extra) {
                return false;
            }
        }
        const auto parse_percentage = [](const std::string& text, float& result) {
            if (text.size() < 2 || text.back() != '%') {
                return false;
            }
            float parsed = 0.0F;
            if (!parse_float(text.substr(0, text.size() - 1), parsed) || !std::isfinite(parsed)) {
                return false;
            }
            result = std::max(0.0F, std::min(1.0F, parsed / 100.0F));
            return true;
        };
        float saturation = 0.0F;
        float lightness = 0.0F;
        if (!parse_percentage(saturation_text, saturation) || !parse_percentage(lightness_text, lightness)) {
            return false;
        }
        char* hue_end = nullptr;
        errno = 0;
        const float raw_hue = std::strtof(hue_text.c_str(), &hue_end);
        if (hue_end == hue_text.c_str() || errno == ERANGE || hue_end == nullptr || !std::isfinite(raw_hue)) {
            return false;
        }
        float hue = raw_hue;
        const std::string_view hue_unit(hue_end);
        if (hue_unit.empty() || hue_unit == "deg") {
            // CSS allows unitless zero; accepting unitless degrees keeps the
            // bounded parser useful for common authored token values.
        } else if (hue_unit == "turn") {
            hue *= 360.0F;
        } else if (hue_unit == "grad") {
            hue *= 0.9F;
        } else if (hue_unit == "rad") {
            hue *= 180.0F / 3.14159265358979323846F;
        } else {
            return false;
        }
        float alpha = 1.0F;
        if (!alpha_text.empty()) {
            if (alpha_text.back() == '%') {
                if (!parse_percentage(alpha_text, alpha)) {
                    return false;
                }
            } else if (!parse_float(alpha_text, alpha) || !std::isfinite(alpha) || alpha < 0.0F || alpha > 1.0F) {
                return false;
            }
        }
        hue = std::fmod(hue, 360.0F);
        if (hue < 0.0F) {
            hue += 360.0F;
        }
        const float chroma = (1.0F - std::fabs(2.0F * lightness - 1.0F)) * saturation;
        const float hue_sector = hue / 60.0F;
        const float second = chroma * (1.0F - std::fabs(std::fmod(hue_sector, 2.0F) - 1.0F));
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        if (hue_sector < 1.0F) {
            red = chroma;
            green = second;
        } else if (hue_sector < 2.0F) {
            red = second;
            green = chroma;
        } else if (hue_sector < 3.0F) {
            green = chroma;
            blue = second;
        } else if (hue_sector < 4.0F) {
            green = second;
            blue = chroma;
        } else if (hue_sector < 5.0F) {
            red = second;
            blue = chroma;
        } else {
            red = chroma;
            blue = second;
        }
        const float match = lightness - chroma * 0.5F;
        output = Color{
            static_cast<std::uint8_t>(std::lround(std::max(0.0F, std::min(1.0F, red + match)) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(std::max(0.0F, std::min(1.0F, green + match)) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(std::max(0.0F, std::min(1.0F, blue + match)) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(alpha * 255.0F)),
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
    if (value.rfind("hsl(", 0) == 0 && value.back() == ')') {
        return parse_hsl_function(std::string_view(value).substr(4, value.size() - 5));
    }
    if (value.rfind("hsla(", 0) == 0 && value.back() == ')') {
        return parse_hsl_function(std::string_view(value).substr(5, value.size() - 6));
    }
    constexpr std::string_view color_mix_prefix = "color-mix(";
    if (value.rfind(color_mix_prefix, 0) == 0 && value.back() == ')') {
        const std::vector<std::string> args = split_function_arguments(
            std::string_view(value).substr(color_mix_prefix.size(), value.size() - color_mix_prefix.size() - 1));
        if (args.size() != 3 || trim(args[0]) != "in srgb") {
            return false;
        }
        const auto parse_component = [&](const std::string& component, Color& color, int& percent, bool& specified) {
            const std::vector<std::string> tokens = split_whitespace_components(component);
            if (tokens.empty() || tokens.size() > 2 || !parse_color(tokens[0], color)) {
                return false;
            }
            specified = tokens.size() == 2;
            if (!specified) {
                percent = 0;
                return true;
            }
            return parse_percentage_int(tokens[1], percent) && percent >= 0 && percent <= 100;
        };
        Color first;
        Color second;
        int first_percent = 0;
        int second_percent = 0;
        bool first_specified = false;
        bool second_specified = false;
        if (!parse_component(args[1], first, first_percent, first_specified) ||
            !parse_component(args[2], second, second_percent, second_specified)) {
            return false;
        }
        if (!first_specified && !second_specified) {
            first_percent = 50;
            second_percent = 50;
        } else if (!first_specified) {
            first_percent = 100 - second_percent;
        } else if (!second_specified) {
            second_percent = 100 - first_percent;
        }
        const int total = first_percent + second_percent;
        if (total <= 0 || total > 100) {
            return false;
        }
        const auto mix = [&](std::uint8_t left, std::uint8_t right) {
            return static_cast<std::uint8_t>((static_cast<int>(left) * first_percent +
                                              static_cast<int>(right) * second_percent + total / 2) / total);
        };
        const int alpha = (static_cast<int>(first.a) * first_percent +
                           static_cast<int>(second.a) * second_percent + total / 2) / total;
        output = Color{mix(first.r, second.r), mix(first.g, second.g), mix(first.b, second.b),
                       static_cast<std::uint8_t>(alpha)};
        return true;
    }
    return false;
}

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
bool parse_box_shadow_style(const std::string& raw_value, int em_base, BoxShadowStyle& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "none") {
        output = BoxShadowStyle{};
        return true;
    }
    if (value.empty() || has_top_level_comma(value)) {
        return false;
    }
    const std::vector<std::string> tokens = split_whitespace_components(value);
    int lengths[4] = {0, 0, 0, 0};
    int length_count = 0;
    bool has_color = false;
    Color color{0, 0, 0, 64};
    for (const std::string& token : tokens) {
        if (token == "inset") {
            return false;
        }
        int length = 0;
        if (length_count < 4 && parse_length_px(token, length, em_base)) {
            lengths[length_count++] = length;
            continue;
        }
        Color parsed;
        if (!has_color && parse_color(token, parsed)) {
            color = parsed;
            has_color = true;
            continue;
        }
        return false;
    }
    if (length_count < 2 || lengths[2] < 0 || lengths[3] < 0 ||
        lengths[0] < -32768 || lengths[0] > 32767 || lengths[1] < -32768 || lengths[1] > 32767 ||
        lengths[2] > 32767 || lengths[3] > 32767) {
        return false;
    }
    output.enabled = true;
    output.uses_current_color = !has_color;
    output.offset_x = static_cast<std::int16_t>(lengths[0]);
    output.offset_y = static_cast<std::int16_t>(lengths[1]);
    output.blur = static_cast<std::int16_t>(lengths[2]);
    output.spread = static_cast<std::int16_t>(lengths[3]);
    output.color = color;
    return true;
}

bool parse_text_shadow_style(const std::string& raw_value, int em_base, TextShadowStyle& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value == "none") {
        output = TextShadowStyle{};
        return true;
    }
    if (value.empty() || has_top_level_comma(value)) {
        return false;
    }
    const std::vector<std::string> tokens = split_whitespace_components(value);
    int lengths[3] = {0, 0, 0};
    int length_count = 0;
    bool has_color = false;
    Color color{0, 0, 0, 255};
    for (const std::string& token : tokens) {
        int length = 0;
        if (length_count < 3 && parse_length_px(token, length, em_base)) {
            lengths[length_count++] = length;
            continue;
        }
        Color parsed;
        if (!has_color && parse_color(token, parsed)) {
            color = parsed;
            has_color = true;
            continue;
        }
        return false;
    }
    if (length_count < 2 || lengths[2] < 0 || lengths[0] < -32768 || lengths[0] > 32767 ||
        lengths[1] < -32768 || lengths[1] > 32767 || lengths[2] > 32767) {
        return false;
    }
    output.enabled = true;
    output.uses_current_color = !has_color;
    output.offset_x = static_cast<std::int16_t>(lengths[0]);
    output.offset_y = static_cast<std::int16_t>(lengths[1]);
    output.blur = static_cast<std::int16_t>(lengths[2]);
    output.color = color;
    return true;
}
#endif

struct BorderShorthandParseResult {
    int width = 0;
    Color color;
    bool has_width = false;
    bool has_color = false;
};

BorderShorthandParseResult parse_border_shorthand(const std::string& value, int em_base) {
    BorderShorthandParseResult result;
    const std::string lowered = lowercase(trim(value));
    if (lowered == "none" || lowered == "0" || lowered == "0px") {
        result.has_width = true;
        result.width = 0;
        return result;
    }

    for (const std::string& token : split_whitespace_components(value)) {
        if (!token.empty() && !result.has_width && parse_length_px(token, result.width, em_base)) {
            result.has_width = true;
        } else if (!token.empty() && !result.has_color && parse_color(token, result.color)) {
            result.has_color = true;
        }
    }
    return result;
}

bool parse_linear_gradient_background(const std::string& raw_value, Color& first, Color& second, GradientAxis& axis) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "linear-gradient(";
    if (value.rfind(prefix, 0) != 0 || value.back() != ')') {
        return false;
    }

    std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    axis = GradientAxis::Vertical;
    if (args.size() == 3) {
        const std::string direction = trim(args[0]);
        const auto erase_direction = [&](GradientAxis parsed_axis, bool reverse) {
            axis = parsed_axis;
            args.erase(args.begin());
            if (reverse) {
                std::swap(args[0], args[1]);
            }
        };
        if (direction == "to bottom") {
            args.erase(args.begin());
        } else if (direction == "to top") {
            erase_direction(GradientAxis::Vertical, true);
        } else if (direction == "to right") {
            erase_direction(GradientAxis::Horizontal, false);
        } else if (direction == "to left") {
            erase_direction(GradientAxis::Horizontal, true);
        } else if (direction == "to bottom right") {
            erase_direction(GradientAxis::DiagonalDownRight, false);
        } else if (direction == "to top left") {
            erase_direction(GradientAxis::DiagonalDownRight, true);
        } else if (direction == "to bottom left") {
            erase_direction(GradientAxis::DiagonalDownLeft, false);
        } else if (direction == "to top right") {
            erase_direction(GradientAxis::DiagonalDownLeft, true);
        } else if (direction == "0deg") {
            erase_direction(GradientAxis::Vertical, true);
        } else if (direction == "45deg") {
            erase_direction(GradientAxis::DiagonalDownLeft, true);
        } else if (direction == "90deg") {
            erase_direction(GradientAxis::Horizontal, false);
        } else if (direction == "135deg") {
            erase_direction(GradientAxis::DiagonalDownRight, false);
        } else if (direction == "180deg") {
            args.erase(args.begin());
        } else if (direction == "225deg") {
            erase_direction(GradientAxis::DiagonalDownLeft, false);
        } else if (direction == "270deg") {
            erase_direction(GradientAxis::Horizontal, true);
        } else if (direction == "315deg") {
            erase_direction(GradientAxis::DiagonalDownRight, true);
        } else {
            return false;
        }
    }
    if (args.size() != 2) {
        return false;
    }
    return parse_color(args[0], first) && parse_color(args[1], second);
}

bool parse_conic_stop(const std::string& component, Color& color, int& start_percent, int& end_percent) {
    const std::vector<std::string> tokens = split_whitespace_components(component);
    if (tokens.size() != 3) {
        return false;
    }
    if (!parse_color(tokens[0], color) ||
        !parse_percentage_int(tokens[1], start_percent) ||
        !parse_percentage_int(tokens[2], end_percent)) {
        return false;
    }
    if (start_percent < 0 || start_percent > 100 || end_percent < 0 || end_percent > 100) {
        return false;
    }
    return start_percent <= end_percent;
}

std::string conic_gradient_failure_detail(const std::string& raw_value) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "conic-gradient(";
    if (value.rfind(prefix, 0) != 0) {
        return {};
    }
    constexpr std::string_view expected =
        "Expected supported subset: conic-gradient(<color> 0% N%, <color> N% 100%).";
    if (value.size() <= prefix.size() + 1 || value.back() != ')') {
        return std::string(expected) + " Function must be closed with ')'.";
    }

    const std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() != 2) {
        return std::string(expected) + " Only two contiguous color segments are supported.";
    }

    std::array<int, 4> percents{{0, 0, 0, 0}};
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::vector<std::string> tokens = split_whitespace_components(args[index]);
        if (tokens.size() != 3) {
            return std::string(expected) + " Each segment must be '<color> start% end%'.";
        }
        Color color;
        if (!parse_color(tokens[0], color)) {
            return std::string(expected) + " Segment color is outside the supported color subset.";
        }
        int start = 0;
        int end = 0;
        if (!parse_percentage_int(tokens[1], start) || !parse_percentage_int(tokens[2], end)) {
            return std::string(expected) + " Segment stops must be percentages.";
        }
        if (start < 0 || start > 100 || end < 0 || end > 100) {
            return std::string(expected) + " Segment percentages must be between 0% and 100%.";
        }
        if (start > end) {
            return std::string(expected) + " Segment start must not be after segment end.";
        }
        percents[index * 2] = start;
        percents[index * 2 + 1] = end;
    }
    if (percents[0] != 0 || percents[1] != percents[2] || percents[3] != 100) {
        return std::string(expected) + " Segments must be contiguous from 0% to 100%.";
    }
    return {};
}

bool parse_conic_gradient_background(const std::string& raw_value,
                                     Color& first,
                                     Color& second,
                                     int& stop_percent) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "conic-gradient(";
    if (value.rfind(prefix, 0) != 0 || value.back() != ')') {
        return false;
    }

    const std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() != 2) {
        return false;
    }

    int first_start = 0;
    int first_end = 0;
    int second_start = 0;
    int second_end = 0;
    if (!parse_conic_stop(args[0], first, first_start, first_end) ||
        !parse_conic_stop(args[1], second, second_start, second_end)) {
        return false;
    }
    if (first_start != 0 || first_end != second_start || second_end != 100) {
        return false;
    }
    stop_percent = first_end;
    return true;
}

bool parse_radial_stop(const std::string& component, Color& color, int expected_percent) {
    const std::vector<std::string> tokens = split_whitespace_components(component);
    if (tokens.empty() || tokens.size() > 2) {
        return false;
    }
    if (!parse_color(tokens[0], color)) {
        return false;
    }
    if (tokens.size() == 1) {
        return true;
    }
    int percent = 0;
    return parse_percentage_int(tokens[1], percent) && percent == expected_percent;
}

std::string radial_gradient_failure_detail(const std::string& raw_value) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "radial-gradient(";
    if (value.rfind(prefix, 0) != 0) {
        return {};
    }
    constexpr std::string_view expected =
        "Expected supported subset: radial-gradient([circle] [at <x%> <y%>,] <color> [0%], <color> [100%]).";
    if (value.size() <= prefix.size() + 1 || value.back() != ')') {
        return std::string(expected) + " Function must be closed with ')'.";
    }

    std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() == 3) {
        const std::vector<std::string> position = split_whitespace_components(args[0]);
        std::size_t position_index = 0;
        if (!position.empty() && position[0] == "circle") {
            ++position_index;
        }
        bool valid_position = position_index == position.size();
        if (!valid_position && position_index < position.size() && position[position_index] == "at") {
            ++position_index;
            valid_position = position_index + 1 == position.size() && position[position_index] == "center";
            if (!valid_position && position_index + 2 == position.size()) {
                int x_percent = 0;
                int y_percent = 0;
                valid_position = parse_percentage_int(position[position_index], x_percent) &&
                    parse_percentage_int(position[position_index + 1], y_percent) &&
                    x_percent >= 0 && x_percent <= 100 && y_percent >= 0 && y_percent <= 100;
            }
        }
        if (!valid_position) {
            return std::string(expected) + " Only a circle with a center or percentage position is supported.";
        }
        args.erase(args.begin());
    }
    if (args.size() != 2) {
        return std::string(expected) + " Only two color stops are supported.";
    }

    Color first;
    Color second;
    if (!parse_radial_stop(args[0], first, 0) || !parse_radial_stop(args[1], second, 100)) {
        return std::string(expected) + " Stops must be colors with optional 0% and 100% percentages.";
    }
    return {};
}

bool parse_radial_gradient_background(const std::string& raw_value,
                                      Color& first,
                                      Color& second,
                                      int& x_percent,
                                      int& y_percent) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "radial-gradient(";
    if (value.rfind(prefix, 0) != 0 || value.back() != ')') {
        return false;
    }

    std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() == 3) {
        const std::vector<std::string> position = split_whitespace_components(args[0]);
        std::size_t position_index = 0;
        if (!position.empty() && position[0] == "circle") {
            ++position_index;
        }
        bool valid_position = position_index == position.size();
        x_percent = 50;
        y_percent = 50;
        if (!valid_position && position_index < position.size() && position[position_index] == "at") {
            ++position_index;
            valid_position = position_index + 1 == position.size() && position[position_index] == "center";
            if (!valid_position && position_index + 2 == position.size()) {
                valid_position = parse_percentage_int(position[position_index], x_percent) &&
                    parse_percentage_int(position[position_index + 1], y_percent) &&
                    x_percent >= 0 && x_percent <= 100 && y_percent >= 0 && y_percent <= 100;
            }
        }
        if (!valid_position) {
            return false;
        }
        args.erase(args.begin());
    } else {
        x_percent = 50;
        y_percent = 50;
    }
    if (args.size() != 2) {
        return false;
    }
    return parse_radial_stop(args[0], first, 0) && parse_radial_stop(args[1], second, 100);
}

bool parse_background_paint(const std::string& value,
                            BackgroundPaintKind& kind,
                            GradientAxis& axis,
                            int& stop_percent,
                            Color& color,
                            Color& color2) {
    Color first;
    Color second;
    if (parse_linear_gradient_background(value, first, second, axis)) {
        kind = BackgroundPaintKind::LinearGradient;
        stop_percent = 100;
        color = first;
        color2 = second;
        return true;
    }
    int conic_stop_percent = 100;
    if (parse_conic_gradient_background(value, first, second, conic_stop_percent)) {
        kind = BackgroundPaintKind::ConicGradient;
        axis = GradientAxis::Vertical;
        stop_percent = conic_stop_percent;
        color = first;
        color2 = second;
        return true;
    }
    int radial_x_percent = 50;
    int radial_y_percent = 50;
    if (parse_radial_gradient_background(value, first, second, radial_x_percent, radial_y_percent)) {
        kind = BackgroundPaintKind::RadialGradient;
        axis = radial_x_percent == 50 && radial_y_percent == 50
            ? GradientAxis::Vertical
            : GradientAxis::RadialPosition;
        stop_percent = radial_x_percent * 101 + radial_y_percent;
        color = first;
        color2 = second;
        return true;
    }
    if (parse_color(value, color)) {
        kind = BackgroundPaintKind::Solid;
        axis = GradientAxis::Vertical;
        stop_percent = 100;
        color2 = color;
        return true;
    }
    return false;
}

bool parse_background_image_paint(const std::string& value,
                                  BackgroundPaintKind& kind,
                                  GradientAxis& axis,
                                  int& stop_percent,
                                  Color& color,
                                  Color& color2) {
    Color first;
    Color second;
    if (parse_linear_gradient_background(value, first, second, axis)) {
        kind = BackgroundPaintKind::LinearGradient;
        stop_percent = 100;
        color = first;
        color2 = second;
        return true;
    }
    int conic_stop_percent = 100;
    if (parse_conic_gradient_background(value, first, second, conic_stop_percent)) {
        kind = BackgroundPaintKind::ConicGradient;
        axis = GradientAxis::Vertical;
        stop_percent = conic_stop_percent;
        color = first;
        color2 = second;
        return true;
    }
    int radial_x_percent = 50;
    int radial_y_percent = 50;
    if (parse_radial_gradient_background(value, first, second, radial_x_percent, radial_y_percent)) {
        kind = BackgroundPaintKind::RadialGradient;
        axis = radial_x_percent == 50 && radial_y_percent == 50
            ? GradientAxis::Vertical
            : GradientAxis::RadialPosition;
        stop_percent = radial_x_percent * 101 + radial_y_percent;
        color = first;
        color2 = second;
        return true;
    }
    return false;
}

bool parse_package_background_image_url(std::string_view raw_value, std::string_view& url) {
    std::size_t begin = 0;
    std::size_t end = raw_value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(raw_value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw_value[end - 1])) != 0) {
        --end;
    }
    if (end - begin < 6 || lowercase(std::string(raw_value.substr(begin, 3))) != "url" ||
        raw_value[begin + 3] != '(' || raw_value[end - 1] != ')') {
        return false;
    }
    begin += 4;
    --end;
    while (begin < end && std::isspace(static_cast<unsigned char>(raw_value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw_value[end - 1])) != 0) {
        --end;
    }
    if (end - begin >= 2 && ((raw_value[begin] == '\'' && raw_value[end - 1] == '\'') ||
                             (raw_value[begin] == '"' && raw_value[end - 1] == '"'))) {
        ++begin;
        --end;
    }
    url = raw_value.substr(begin, end - begin);
    if (url.empty() || url.front() != '/' || url.rfind("//", 0) == 0 ||
        url.find("..") != std::string_view::npos || url.find('\\') != std::string_view::npos ||
        url.find('?') != std::string_view::npos || url.find('#') != std::string_view::npos) {
        return false;
    }
    return std::none_of(url.begin(), url.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

void update_package_background_image_presentation(Style& style) {
    const std::uint16_t resource_id = background_image_resource_id(style.background_overlay_packed);
    if (resource_id != 0) {
        style.background_overlay_packed = pack_background_image_resource(
            resource_id, style.object_fit, style.object_position, style.image_rendering);
    }
}

bool parse_number_or_length_for_transform(const std::string& value, float& output, int em_base = kRootFontSizePx) {
    int px = 0;
    if (parse_length_px(value, px, em_base)) {
        output = static_cast<float>(px);
        return true;
    }
    return parse_float(value, output) &&
        output >= -kMaxTransformTranslationPx &&
        output <= kMaxTransformTranslationPx;
}

bool parse_scale_value(const std::string& value, float& output) {
    if (!parse_float(value, output)) {
        return false;
    }
    output = std::max(0.01F, std::min(16.0F, output));
    return true;
}

bool parse_angle_degrees(const std::string& raw_value, float& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value.empty()) {
        return false;
    }
    if (value == "0") {
        output = 0.0F;
        return true;
    }
    const auto parse_with_suffix = [&](const char* suffix, float multiplier) {
        const std::size_t suffix_length = std::strlen(suffix);
        if (value.size() <= suffix_length ||
            value.compare(value.size() - suffix_length, suffix_length, suffix) != 0) {
            return false;
        }
        float parsed = 0.0F;
        if (!parse_float(trim(std::string_view(value).substr(0, value.size() - suffix_length)), parsed)) {
            return false;
        }
        const float degrees = parsed * multiplier;
        if (!std::isfinite(degrees)) {
            return false;
        }
        output = degrees;
        return true;
    };
    constexpr float kPi = 3.14159265358979323846F;
    return parse_with_suffix("deg", 1.0F) ||
        parse_with_suffix("turn", 360.0F) ||
        parse_with_suffix("rad", 180.0F / kPi) ||
        parse_with_suffix("grad", 0.9F);
}

bool parse_transform_function(std::string_view function, Transform2D& output) {
    const std::size_t open = function.find('(');
    if (open == std::string_view::npos || function.empty() || function.back() != ')') {
        return false;
    }
    const std::string name = lowercase(trim(function.substr(0, open)));
    const std::vector<std::string> args =
        split_function_arguments(function.substr(open + 1, function.size() - open - 2));
    const auto add_transform_component = [](float& target, float value) {
        const float result = target + value;
        if (!std::isfinite(result) ||
            result < -kMaxTransformTranslationPx || result > kMaxTransformTranslationPx) {
            return false;
        }
        target = result;
        return true;
    };
    const auto multiply_transform_scale = [](float& target, float value) {
        const float result = target * value;
        if (!std::isfinite(result)) {
            return false;
        }
        target = result;
        return true;
    };
    if (name == "translate" || name == "translate3d") {
        if (args.empty() || args.size() > 3) {
            return false;
        }
        float x = 0.0F;
        float y = 0.0F;
        if (!parse_number_or_length_for_transform(args[0], x)) {
            return false;
        }
        if (args.size() >= 2 && !parse_number_or_length_for_transform(args[1], y)) {
            return false;
        }
        return add_transform_component(output.translate_x, x) &&
            add_transform_component(output.translate_y, y);
    }
    if (name == "translatex") {
        float x = 0.0F;
        if (args.size() != 1 || !parse_number_or_length_for_transform(args[0], x)) {
            return false;
        }
        return add_transform_component(output.translate_x, x);
    }
    if (name == "translatey") {
        float y = 0.0F;
        if (args.size() != 1 || !parse_number_or_length_for_transform(args[0], y)) {
            return false;
        }
        return add_transform_component(output.translate_y, y);
    }
    if (name == "scale" || name == "scale3d") {
        if (args.empty() || args.size() > 3) {
            return false;
        }
        float x = 1.0F;
        float y = 1.0F;
        if (!parse_scale_value(args[0], x)) {
            return false;
        }
        y = x;
        if (args.size() >= 2 && !parse_scale_value(args[1], y)) {
            return false;
        }
        return multiply_transform_scale(output.scale_x, x) &&
            multiply_transform_scale(output.scale_y, y);
    }
    if (name == "scalex") {
        float x = 1.0F;
        if (args.size() != 1 || !parse_scale_value(args[0], x)) {
            return false;
        }
        return multiply_transform_scale(output.scale_x, x);
    }
    if (name == "scaley") {
        float y = 1.0F;
        if (args.size() != 1 || !parse_scale_value(args[0], y)) {
            return false;
        }
        return multiply_transform_scale(output.scale_y, y);
    }
    if (name == "rotate" || name == "rotatez") {
        float degrees = 0.0F;
        if (args.size() != 1 || !parse_angle_degrees(args[0], degrees)) {
            return false;
        }
        return add_transform_component(output.rotate_degrees, degrees);
    }
    return false;
}

bool parse_transform_origin_percent_token(const std::string& token, int& percent) {
    const std::string value = lowercase(trim(token));
    if (value == "left" || value == "top") {
        percent = 0;
        return true;
    }
    if (value == "center") {
        percent = 50;
        return true;
    }
    if (value == "right" || value == "bottom") {
        percent = 100;
        return true;
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
    if (end == nullptr || std::strncmp(end, "%", 1) != 0) {
        return false;
    }
    ++end;
    while (std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    int rounded = 0;
    if (!round_finite_float_to_int(parsed, rounded)) {
        return false;
    }
    percent = std::max(-200, std::min(300, rounded));
    return true;
}

bool parse_transform_origin_percent(const std::string& raw_value, int& x_percent, int& y_percent) {
    const std::vector<std::string> tokens = split_whitespace_components(raw_value);
    if (tokens.empty() || tokens.size() > 2) {
        return false;
    }
    int first = 50;
    int second = 50;
    if (!parse_transform_origin_percent_token(tokens[0], first)) {
        return false;
    }
    if (tokens.size() == 1) {
        const std::string token = lowercase(trim(tokens[0]));
        if (token == "top" || token == "bottom") {
            x_percent = 50;
            y_percent = first;
        } else {
            x_percent = first;
            y_percent = 50;
        }
        return true;
    }
    if (!parse_transform_origin_percent_token(tokens[1], second)) {
        return false;
    }
    const std::string first_token = lowercase(trim(tokens[0]));
    const std::string second_token = lowercase(trim(tokens[1]));
    if ((first_token == "top" || first_token == "bottom") &&
        !(second_token == "top" || second_token == "bottom")) {
        x_percent = second;
        y_percent = first;
    } else {
        x_percent = first;
        y_percent = second;
    }
    return true;
}

bool parse_object_position_percent_token(const std::string& token, int& percent) {
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(token.c_str(), &end);
    if (end == token.c_str() || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || std::strncmp(end, "%", 1) != 0) {
        return false;
    }
    ++end;
    while (std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    int rounded = 0;
    if (!round_finite_float_to_int(parsed, rounded)) {
        return false;
    }
    percent = std::max(0, std::min(100, rounded));
    return true;
}

bool parse_object_position_component(const std::string& token,
                                     bool& sets_x,
                                     int& x_percent,
                                     bool& sets_y,
                                     int& y_percent) {
    if (token == "left") {
        sets_x = true;
        x_percent = 0;
        return true;
    }
    if (token == "right") {
        sets_x = true;
        x_percent = 100;
        return true;
    }
    if (token == "top") {
        sets_y = true;
        y_percent = 0;
        return true;
    }
    if (token == "bottom") {
        sets_y = true;
        y_percent = 100;
        return true;
    }
    if (token == "center") {
        if (!sets_x) {
            sets_x = true;
            x_percent = 50;
            return true;
        }
        if (!sets_y) {
            sets_y = true;
            y_percent = 50;
            return true;
        }
        return false;
    }
    int percent = 0;
    if (parse_object_position_percent_token(token, percent)) {
        if (!sets_x) {
            sets_x = true;
            x_percent = percent;
            return true;
        }
        if (!sets_y) {
            sets_y = true;
            y_percent = percent;
            return true;
        }
    }
    return false;
}

bool parse_object_position_value(const std::string& raw_value, ObjectPosition& output) {
    const std::vector<std::string> tokens = split_whitespace_components(lowercase(trim(raw_value)));
    if (tokens.empty() || tokens.size() > 2) {
        return false;
    }
    bool sets_x = false;
    bool sets_y = false;
    int x_percent = 50;
    int y_percent = 50;
    for (const std::string& token : tokens) {
        if (!parse_object_position_component(token, sets_x, x_percent, sets_y, y_percent)) {
            return false;
        }
    }
    output.x_percent = x_percent;
    output.y_percent = y_percent;
    return true;
}

std::vector<std::string> split_transform_functions(std::string_view value) {
    std::vector<std::string> functions;
    std::size_t begin = 0;
    int depth = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')' && depth > 0) {
            --depth;
            if (depth == 0) {
                functions.push_back(trim(value.substr(begin, index + 1 - begin)));
                begin = index + 1;
            }
        } else if (depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
            begin = index + 1;
        }
    }
    return functions;
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

struct SelectorMatchContext {
    const Node* hovered_node = nullptr;
    const Node* active_node = nullptr;
    const Node* focused_node = nullptr;
};

bool node_is_or_ancestor_of(const Node& node, const Node* descendant) {
    for (const Node* current = descendant; current != nullptr; current = current->parent) {
        if (current == &node) {
            return true;
        }
    }
    return false;
}

std::size_t find_selector_function_close(std::string_view value, std::size_t open) {
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = open; index < value.size(); ++index) {
        const char ch = value[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < value.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string_view::npos;
}

bool matches_selector_from_right(const Node* node,
                                 const std::vector<CssSelectorPart>& parts,
                                 std::size_t index,
                                 const SelectorMatchContext& context);

bool matches_selector_function(const Node& node,
                               std::string_view body,
                               const SelectorMatchContext& context) {
    for (const std::string& argument : split_function_arguments(body)) {
        const std::vector<CssSelectorPart> parts = parse_css_selector_parts(argument);
        if (!parts.empty() && matches_selector_from_right(&node, parts, 0, context)) {
            return true;
        }
    }
    return false;
}

bool matches_dynamic_pseudo(const Node& node,
                            const std::string& pseudo,
                            const SelectorMatchContext& context) {
    if (pseudo == "hover") {
        return node_is_or_ancestor_of(node, context.hovered_node);
    }
    if (pseudo == "active") {
        return node_is_or_ancestor_of(node, context.active_node);
    }
    if (pseudo == "focus") {
        return &node == context.focused_node;
    }
    if (pseudo == "focus-within") {
        return node_is_or_ancestor_of(node, context.focused_node);
    }
    if (pseudo == "checked") {
        return form_control_checked(node);
    }
    if (pseudo == "disabled") {
        return is_disabled_form_control(node);
    }
    return false;
}

bool matches_compound_selector(const Node& node,
                               std::string_view selector,
                               const SelectorMatchContext& context) {
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
            if ((pseudo == "is" || pseudo == "where") && index < selector.size() && selector[index] == '(') {
                const std::size_t close = find_selector_function_close(selector, index);
                if (close == std::string_view::npos ||
                    !matches_selector_function(node, selector.substr(index + 1, close - index - 1), context)) {
                    return false;
                }
                index = close + 1;
                continue;
            }
            if (matches_dynamic_pseudo(node, pseudo, context)) {
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

const Node* previous_element_sibling(const Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return nullptr;
    }
    const auto& siblings = node->parent->children;
    const Node* previous = nullptr;
    for (const auto& sibling : siblings) {
        if (sibling.get() == node) {
            return previous;
        }
        if (sibling->type == NodeType::Element) {
            previous = sibling.get();
        }
    }
    return nullptr;
}

bool matches_selector_from_right(const Node* node,
                                 const std::vector<CssSelectorPart>& parts,
                                 std::size_t index,
                                 const SelectorMatchContext& context) {
    if (node == nullptr || index >= parts.size() ||
        !matches_compound_selector(*node, parts[index].compound, context)) {
        return false;
    }
    if (index + 1 >= parts.size()) {
        return true;
    }

    if (parts[index].combinator_to_left == CssSelectorCombinator::Child) {
        return matches_selector_from_right(node->parent, parts, index + 1, context);
    }
    if (parts[index].combinator_to_left == CssSelectorCombinator::AdjacentSibling) {
        return matches_selector_from_right(previous_element_sibling(node), parts, index + 1, context);
    }
    if (parts[index].combinator_to_left == CssSelectorCombinator::GeneralSibling) {
        for (const Node* sibling = previous_element_sibling(node); sibling != nullptr;
             sibling = previous_element_sibling(sibling)) {
            if (matches_selector_from_right(sibling, parts, index + 1, context)) {
                return true;
            }
        }
        return false;
    }

    for (const Node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (matches_selector_from_right(ancestor, parts, index + 1, context)) {
            return true;
        }
    }
    return false;
}

bool matches_rule(const Node& node, const CssRule& rule, const SelectorMatchContext& context) {
    return !rule.selector_parts.empty() && matches_selector_from_right(&node, rule.selector_parts, 0, context);
}

struct CascadeSlot {
    bool set = false;
    bool important = false;
    CssSpecificity specificity;
    std::size_t source_order = 0;
};

enum class CascadeProperty : std::size_t {
    Display,
    Visibility,
    Color,
    Background,
    BackgroundSize,
    BackgroundPosition,
    BackgroundRepeat,
    Margin,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    Padding,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    BorderWidth,
    BorderTopWidth,
    BorderRightWidth,
    BorderBottomWidth,
    BorderLeftWidth,
    BorderColor,
    Border,
    BorderRadius,
    Width,
    Height,
    MinWidth,
    MinHeight,
    MaxWidth,
    MaxHeight,
    AspectRatio,
    FontSize,
    FontWeight,
    FontFamily,
    LineHeight,
    TextIndent,
    LetterSpacing,
    TextTransform,
    TextDecoration,
    BoxShadow,
    Overflow,
    OverflowWrap,
    Opacity,
    Transform,
    TransformOrigin,
    Position,
    Top,
    Right,
    Bottom,
    Left,
    ZIndex,
    TextAlign,
    JustifyContent,
    AlignContent,
    AlignItems,
    AlignSelf,
    BoxSizing,
    TextShadow,
    Outline,
    OutlineWidth,
    OutlineColor,
    OutlineOffset,
    WhiteSpace,
    TextOverflow,
    Flex,
    FlexGrow,
    FlexShrink,
    FlexBasis,
    FlexDirection,
    FlexOrder,
    FlexWrap,
    Gap,
    ColumnGap,
    RowGap,
    GridTemplateColumns,
    GridTemplateRows,
    GridAutoRows,
    GridColumn,
    GridRow,
    ObjectFit,
    ObjectPosition,
    ImageRendering,
    ListStyleType,
    Transition,
    TransitionProperty,
    TransitionDuration,
    TransitionDelay,
    TransitionTimingFunction,
    Animation,
    AnimationName,
    AnimationDuration,
    AnimationDelay,
    AnimationTimingFunction,
    AnimationIterationCount,
    AnimationDirection,
    AnimationFillMode,
    BeforeContent,
    BeforeColor,
    BeforeFontWeight,
    BeforeLeft,
    AfterContent,
    AfterColor,
    AfterFontWeight,
    AfterLeft,
    Count,
};

struct CascadeSlots {
    std::array<CascadeSlot, static_cast<std::size_t>(CascadeProperty::Count)> slots;
};

struct CustomPropertySlot {
    bool set = false;
    bool important = false;
    CssSpecificity specificity;
    std::size_t source_order = 0;
    std::string value;
};

using CustomPropertySlots = std::unordered_map<std::string, CustomPropertySlot>;

CascadeSlot& cascade_slot(CascadeSlots& slots, CascadeProperty property) {
    return slots.slots[static_cast<std::size_t>(property)];
}

bool is_custom_property_name(const std::string& property) {
    return property.size() > 2 && property[0] == '-' && property[1] == '-';
}

CascadeSlot* cascade_slot_for_property(CascadeSlots& slots, const std::string& property) {
    struct PropertySlotEntry {
        const char* name;
        CascadeProperty slot;
    };
    static constexpr PropertySlotEntry kPropertySlots[] = {
        {"align-content", CascadeProperty::AlignContent},
        {"align-items", CascadeProperty::AlignItems},
        {"align-self", CascadeProperty::AlignSelf},
        {"animation", CascadeProperty::Animation},
        {"animation-delay", CascadeProperty::AnimationDelay},
        {"animation-direction", CascadeProperty::AnimationDirection},
        {"animation-duration", CascadeProperty::AnimationDuration},
        {"animation-fill-mode", CascadeProperty::AnimationFillMode},
        {"animation-iteration-count", CascadeProperty::AnimationIterationCount},
        {"animation-name", CascadeProperty::AnimationName},
        {"animation-timing-function", CascadeProperty::AnimationTimingFunction},
        {"aspect-ratio", CascadeProperty::AspectRatio},
        {"background", CascadeProperty::Background},
        {"background-color", CascadeProperty::Background},
        {"background-image", CascadeProperty::Background},
        {"background-position", CascadeProperty::BackgroundPosition},
        {"background-repeat", CascadeProperty::BackgroundRepeat},
        {"background-size", CascadeProperty::BackgroundSize},
        {"border", CascadeProperty::Border},
        {"border-bottom-width", CascadeProperty::BorderBottomWidth},
        {"border-color", CascadeProperty::BorderColor},
        {"border-left-width", CascadeProperty::BorderLeftWidth},
        {"border-radius", CascadeProperty::BorderRadius},
        {"border-right-width", CascadeProperty::BorderRightWidth},
        {"border-top-width", CascadeProperty::BorderTopWidth},
        {"bottom", CascadeProperty::Bottom},
        {"box-shadow", CascadeProperty::BoxShadow},
        {"box-sizing", CascadeProperty::BoxSizing},
        {"color", CascadeProperty::Color},
        {"column-gap", CascadeProperty::ColumnGap},
        {"display", CascadeProperty::Display},
        {"flex", CascadeProperty::Flex},
        {"flex-basis", CascadeProperty::FlexBasis},
        {"flex-direction", CascadeProperty::FlexDirection},
        {"flex-grow", CascadeProperty::FlexGrow},
        {"flex-shrink", CascadeProperty::FlexShrink},
        {"flex-wrap", CascadeProperty::FlexWrap},
        {"font-family", CascadeProperty::FontFamily},
        {"font-size", CascadeProperty::FontSize},
        {"font-weight", CascadeProperty::FontWeight},
        {"gap", CascadeProperty::Gap},
        {"grid-auto-rows", CascadeProperty::GridAutoRows},
        {"grid-column", CascadeProperty::GridColumn},
        {"grid-row", CascadeProperty::GridRow},
        {"grid-template-columns", CascadeProperty::GridTemplateColumns},
        {"grid-template-rows", CascadeProperty::GridTemplateRows},
        {"height", CascadeProperty::Height},
        {"image-rendering", CascadeProperty::ImageRendering},
        {"justify-content", CascadeProperty::JustifyContent},
        {"left", CascadeProperty::Left},
        {"letter-spacing", CascadeProperty::LetterSpacing},
        {"line-height", CascadeProperty::LineHeight},
        {"list-style", CascadeProperty::ListStyleType},
        {"list-style-type", CascadeProperty::ListStyleType},
        {"margin-bottom", CascadeProperty::MarginBottom},
        {"margin-left", CascadeProperty::MarginLeft},
        {"margin-right", CascadeProperty::MarginRight},
        {"margin-top", CascadeProperty::MarginTop},
        {"max-height", CascadeProperty::MaxHeight},
        {"max-width", CascadeProperty::MaxWidth},
        {"min-height", CascadeProperty::MinHeight},
        {"min-width", CascadeProperty::MinWidth},
        {"object-fit", CascadeProperty::ObjectFit},
        {"object-position", CascadeProperty::ObjectPosition},
        {"opacity", CascadeProperty::Opacity},
        {"order", CascadeProperty::FlexOrder},
        {"outline", CascadeProperty::Outline},
        {"outline-color", CascadeProperty::OutlineColor},
        {"outline-offset", CascadeProperty::OutlineOffset},
        {"outline-width", CascadeProperty::OutlineWidth},
        {"overflow", CascadeProperty::Overflow},
        {"overflow-wrap", CascadeProperty::OverflowWrap},
        {"overflow-y", CascadeProperty::Overflow},
        {"padding-bottom", CascadeProperty::PaddingBottom},
        {"padding-left", CascadeProperty::PaddingLeft},
        {"padding-right", CascadeProperty::PaddingRight},
        {"padding-top", CascadeProperty::PaddingTop},
        {"position", CascadeProperty::Position},
        {"right", CascadeProperty::Right},
        {"row-gap", CascadeProperty::RowGap},
        {"text-align", CascadeProperty::TextAlign},
        {"text-decoration", CascadeProperty::TextDecoration},
        {"text-decoration-line", CascadeProperty::TextDecoration},
        {"text-indent", CascadeProperty::TextIndent},
        {"text-overflow", CascadeProperty::TextOverflow},
        {"text-shadow", CascadeProperty::TextShadow},
        {"text-transform", CascadeProperty::TextTransform},
        {"text-wrap", CascadeProperty::WhiteSpace},
        {"top", CascadeProperty::Top},
        {"transform", CascadeProperty::Transform},
        {"transform-origin", CascadeProperty::TransformOrigin},
        {"transition", CascadeProperty::Transition},
        {"transition-delay", CascadeProperty::TransitionDelay},
        {"transition-duration", CascadeProperty::TransitionDuration},
        {"transition-property", CascadeProperty::TransitionProperty},
        {"transition-timing-function", CascadeProperty::TransitionTimingFunction},
        {"visibility", CascadeProperty::Visibility},
        {"white-space", CascadeProperty::WhiteSpace},
        {"width", CascadeProperty::Width},
        {"z-index", CascadeProperty::ZIndex},
    };
    const auto* it = std::lower_bound(std::begin(kPropertySlots),
                                      std::end(kPropertySlots),
                                      property,
                                      [](const PropertySlotEntry& entry, const std::string& value) {
                                          return entry.name < value;
                                      });
    if (it != std::end(kPropertySlots) && property == it->name) {
        return &cascade_slot(slots, it->slot);
    }
    return nullptr;
}

CascadeSlot* cascade_slot_for_generated_property(CascadeSlots& slots,
                                                 CssPseudoElement pseudo,
                                                 const std::string& property) {
    const bool after = pseudo == CssPseudoElement::After;
    if (property == "content") {
        return &cascade_slot(slots, after ? CascadeProperty::AfterContent : CascadeProperty::BeforeContent);
    }
    if (property == "color") {
        return &cascade_slot(slots, after ? CascadeProperty::AfterColor : CascadeProperty::BeforeColor);
    }
    if (property == "font-weight") {
        return &cascade_slot(slots, after ? CascadeProperty::AfterFontWeight : CascadeProperty::BeforeFontWeight);
    }
    if (property == "left") {
        return &cascade_slot(slots, after ? CascadeProperty::AfterLeft : CascadeProperty::BeforeLeft);
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

bool custom_declaration_wins(const CustomPropertySlot& current,
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

enum class DeclarationApplyResult : std::uint8_t {
    Unhandled,
    Applied,
    Invalid,
};

DeclarationApplyResult apply_length_or_percent(int& length_px,
                                               int& percent,
                                               const std::string& value,
                                               int font_size) {
    int parsed_percent = -1;
    if (parse_percentage_int(value, parsed_percent)) {
        length_px = -1;
        percent = parsed_percent;
        return DeclarationApplyResult::Applied;
    }

    int px = 0;
    if (!parse_length_px(value, px, font_size)) {
        return DeclarationApplyResult::Invalid;
    }
    length_px = px;
    percent = -1;
    return DeclarationApplyResult::Applied;
}

DeclarationApplyResult apply_sizing_declaration(Style& style,
                                                const std::string& property,
                                                const std::string& value) {
    if (property == "width") {
        if (lowercase(trim(value)) == "auto") {
            style.width = -1;
            style.width_percent = -1;
            return DeclarationApplyResult::Applied;
        }
        return apply_length_or_percent(style.width, style.width_percent, value, style.font_size);
    }
    if (property == "height") {
        if (lowercase(trim(value)) == "auto") {
            style.height = -1;
            style.height_percent = -1;
            return DeclarationApplyResult::Applied;
        }
        return apply_length_or_percent(style.height, style.height_percent, value, style.font_size);
    }
    if (property == "min-width") {
        return apply_length_or_percent(style.min_width, style.min_width_percent, value, style.font_size);
    }
    if (property == "min-height") {
        return apply_length_or_percent(style.min_height, style.min_height_percent, value, style.font_size);
    }
    if (property == "max-width") {
        return apply_length_or_percent(style.max_width, style.max_width_percent, value, style.font_size);
    }
    if (property == "max-height") {
        return apply_length_or_percent(style.max_height, style.max_height_percent, value, style.font_size);
    }
    if (property == "aspect-ratio") {
        int ratio_width = 0;
        int ratio_height = 0;
        if (!parse_aspect_ratio(value, ratio_width, ratio_height)) {
            return DeclarationApplyResult::Invalid;
        }
        style.aspect_ratio_width = ratio_width;
        style.aspect_ratio_height = ratio_height;
        return DeclarationApplyResult::Applied;
    }
    return DeclarationApplyResult::Unhandled;
}

DeclarationApplyResult apply_box_model_declaration(Style& style,
                                                   const std::string& property,
                                                   const std::string& value) {
    if (property == "margin") {
        return parse_margin_edge_px(value, style.margin, style.margin_left_auto, style.margin_right_auto, style.font_size)
            ? DeclarationApplyResult::Applied
            : DeclarationApplyResult::Invalid;
    }
    if (property == "margin-top" || property == "margin-right" ||
        property == "margin-bottom" || property == "margin-left") {
        int px = 0;
        bool is_auto = false;
        if (!parse_margin_side_px(value, px, is_auto, style.font_size)) {
            return DeclarationApplyResult::Invalid;
        }
        if (property == "margin-top") {
            style.margin.top = px;
        } else if (property == "margin-right") {
            style.margin.right = px;
            style.margin_right_auto = is_auto;
        } else if (property == "margin-bottom") {
            style.margin.bottom = px;
        } else {
            style.margin.left = px;
            style.margin_left_auto = is_auto;
        }
        return DeclarationApplyResult::Applied;
    }
    if (property == "padding") {
        return parse_box_edge_px(value, style.padding, style.font_size)
            ? DeclarationApplyResult::Applied
            : DeclarationApplyResult::Invalid;
    }
    if (property == "padding-top" || property == "padding-right" ||
        property == "padding-bottom" || property == "padding-left") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return DeclarationApplyResult::Invalid;
        }
        if (property == "padding-top") {
            style.padding.top = px;
        } else if (property == "padding-right") {
            style.padding.right = px;
        } else if (property == "padding-bottom") {
            style.padding.bottom = px;
        } else {
            style.padding.left = px;
        }
        return DeclarationApplyResult::Applied;
    }
    if (property == "border-width") {
        return parse_box_edge_px(value, style.border_width, style.font_size)
            ? DeclarationApplyResult::Applied
            : DeclarationApplyResult::Invalid;
    }
    if (property == "border-top-width" || property == "border-right-width" ||
        property == "border-bottom-width" || property == "border-left-width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return DeclarationApplyResult::Invalid;
        }
        if (property == "border-top-width") {
            style.border_width.top = px;
        } else if (property == "border-right-width") {
            style.border_width.right = px;
        } else if (property == "border-bottom-width") {
            style.border_width.bottom = px;
        } else {
            style.border_width.left = px;
        }
        return DeclarationApplyResult::Applied;
    }
    if (property == "border-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return DeclarationApplyResult::Invalid;
        }
        style.border_color = parsed;
        return DeclarationApplyResult::Applied;
    }
    if (property == "border") {
        const BorderShorthandParseResult parsed = parse_border_shorthand(value, style.font_size);
        if (!parsed.has_width && !parsed.has_color) {
            return DeclarationApplyResult::Invalid;
        }
        if (parsed.has_width) {
            style.border_width = EdgeSizes{parsed.width, parsed.width, parsed.width, parsed.width};
        }
        if (parsed.has_color) {
            style.border_color = parsed.color;
        }
        return DeclarationApplyResult::Applied;
    }
    if (property == "border-top" || property == "border-right" ||
        property == "border-bottom" || property == "border-left") {
        const BorderShorthandParseResult parsed = parse_border_shorthand(value, style.font_size);
        if (!parsed.has_width && !parsed.has_color) {
            return DeclarationApplyResult::Invalid;
        }
        if (parsed.has_width) {
            if (property == "border-top") {
                style.border_width.top = parsed.width;
            } else if (property == "border-right") {
                style.border_width.right = parsed.width;
            } else if (property == "border-bottom") {
                style.border_width.bottom = parsed.width;
            } else {
                style.border_width.left = parsed.width;
            }
        }
        if (parsed.has_color) {
            style.border_color = parsed.color;
        }
        return DeclarationApplyResult::Applied;
    }
    if (property == "border-radius") {
        int percent = -1;
        if (parse_percentage_int(value, percent)) {
            style.border_radius = 0;
            style.border_radius_percent = std::max(0, percent);
            return DeclarationApplyResult::Applied;
        }
        EdgeSizes radii;
        if (!parse_box_edge_px(value, radii, style.font_size)) {
            return DeclarationApplyResult::Invalid;
        }
        if (radii.top > 127 || radii.right > 127 || radii.bottom > 127 || radii.left > 127 ||
            radii.top < 0 || radii.right < 0 || radii.bottom < 0 || radii.left < 0) {
            return DeclarationApplyResult::Invalid;
        }
        style.border_radius = encode_corner_radii(CornerRadii{radii.top, radii.right, radii.bottom, radii.left});
        style.border_radius_percent = -1;
        return DeclarationApplyResult::Applied;
    }
    return DeclarationApplyResult::Unhandled;
}

bool apply_declaration(Style& style,
                       const std::string& property,
                       const std::string& value,
                       const StyleResolver* resolver = nullptr) {
#if !JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    if (is_flex_grid_property(property)) {
        return false;
    }
#endif
    const DeclarationApplyResult sizing = apply_sizing_declaration(style, property, value);
    if (sizing != DeclarationApplyResult::Unhandled) {
        return sizing == DeclarationApplyResult::Applied;
    }

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
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
            style.display = Display::Flex;
#else
            return false;
#endif
        } else if (value == "grid" || value == "inline-grid") {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
            style.display = Display::Grid;
#else
            return false;
#endif
        } else {
            return false;
        }
        return true;
    } else if (property == "visibility") {
        if (value == "visible") {
            style.visibility_hidden = false;
        } else if (value == "hidden") {
            style.visibility_hidden = true;
        } else {
            return false;
        }
        style.visibility_specified = true;
        return true;
    } else if (property == "color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.color = parsed;
        style.color_specified = true;
        return true;
    } else if (property == "background-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.background_paint = BackgroundPaintKind::Solid;
        style.background_gradient_axis = GradientAxis::Vertical;
        style.background_gradient_stop_percent = 100;
        style.background_color = parsed;
        style.background_color2 = parsed;
        style.background_overlay_packed = 0;
        return true;
    } else if (property == "background" || property == "background-image") {
        std::string_view image_url;
        if (parse_package_background_image_url(value, image_url)) {
            if (resolver == nullptr) {
                return false;
            }
            const std::uint16_t resource_id = resolver->background_image_resource_id_for(image_url);
            if (resource_id == 0) {
                return false;
            }
            if (property == "background") {
                style.background_paint = BackgroundPaintKind::Solid;
                style.background_gradient_axis = GradientAxis::Vertical;
                style.background_gradient_stop_percent = 100;
                style.background_color = Color{0, 0, 0, 0};
                style.background_color2 = style.background_color;
            } else if (style.background_paint != BackgroundPaintKind::Solid) {
                // background-image replaces older image layers but leaves a
                // separately declared background-color fallback in place.
                style.background_paint = BackgroundPaintKind::Solid;
                style.background_gradient_axis = GradientAxis::Vertical;
                style.background_gradient_stop_percent = 100;
                style.background_color = Color{0, 0, 0, 0};
                style.background_color2 = style.background_color;
            }
            style.background_overlay_packed = pack_background_image_resource(
                resource_id, style.object_fit, style.object_position, style.image_rendering);
            return true;
        }
        if (property == "background-image" && lowercase(trim(value)) == "none") {
            style.background_overlay_packed = 0;
            return true;
        }
        const std::vector<std::string> layers = split_function_arguments(value);
        if (layers.empty() || layers.size() > 2) {
            return false;
        }
        std::array<BackgroundPaint, 2> parsed_layers;
        for (std::size_t index = 0; index < layers.size(); ++index) {
            BackgroundPaint& layer = parsed_layers[index];
            const bool parsed = property == "background"
                ? parse_background_paint(layers[index], layer.kind, layer.axis, layer.stop_percent, layer.color, layer.color2)
                : parse_background_image_paint(layers[index], layer.kind, layer.axis, layer.stop_percent, layer.color, layer.color2);
            if (!parsed) {
                return false;
            }
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
            if (layer.kind != BackgroundPaintKind::Solid) {
                return false;
            }
#endif
        }
        const BackgroundPaint& base = parsed_layers[layers.size() - 1];
        style.background_paint = base.kind;
        style.background_gradient_axis = base.axis;
        style.background_gradient_stop_percent = base.stop_percent;
        style.background_color = base.color;
        style.background_color2 = base.color2;
        style.background_overlay_packed = layers.size() == 2 ? pack_background_overlay(parsed_layers[0]) : 0;
        return true;
    }
    const DeclarationApplyResult box_model = apply_box_model_declaration(style, property, value);
    if (box_model != DeclarationApplyResult::Unhandled) {
        return box_model == DeclarationApplyResult::Applied;
    }
    if (property == "font-size") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.font_size = px;
        style.font_size_specified = true;
        return true;
    } else if (property == "font-weight") {
        int weight = 400;
        if (!parse_font_weight(value, weight)) {
            return false;
        }
        style.font_weight = weight;
        style.font_weight_specified = true;
        return true;
    } else if (property == "font-family") {
        std::uint32_t hash = 0;
        if (!parse_font_family_hash(value, hash)) {
            return false;
        }
        style.font_family_hash = hash;
        style.font_family_specified = true;
        return true;
    } else if (property == "line-height") {
        float multiplier = 0.0F;
        int px = 0;
        if (parse_float(value, multiplier)) {
            if (!round_finite_float_to_int(static_cast<float>(style.font_size) * multiplier, px)) {
                return false;
            }
            style.line_height = std::max(1, px);
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
    } else if (property == "letter-spacing") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "normal") {
            style.letter_spacing = 0;
            style.letter_spacing_specified = true;
            return true;
        }
        if (lowered.back() == '%') {
            return false;
        }
        char* unit_end = nullptr;
        errno = 0;
        const float unitless = std::strtof(lowered.c_str(), &unit_end);
        if (unit_end != lowered.c_str() && unit_end != nullptr && *unit_end == '\0' &&
            (errno == ERANGE || unitless != 0.0F)) {
            return false;
        }
        int px = 0;
        if (!parse_length_px(value, px, style.font_size) ||
            px < -std::max(1, style.font_size / 2) || px > style.font_size * 2) {
            return false;
        }
        style.letter_spacing = static_cast<std::int16_t>(px);
        style.letter_spacing_specified = true;
        return true;
    } else if (property == "text-transform") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "none") {
            style.text_transform = TextTransform::None;
        } else if (lowered == "uppercase") {
            style.text_transform = TextTransform::Uppercase;
        } else if (lowered == "lowercase") {
            style.text_transform = TextTransform::Lowercase;
        } else if (lowered == "capitalize") {
            style.text_transform = TextTransform::Capitalize;
        } else {
            return false;
        }
        style.text_transform_specified = true;
        return true;
    } else if (property == "text-decoration" || property == "text-decoration-line") {
        const std::vector<std::string> tokens = split_whitespace_components(lowercase(trim(value)));
        if (tokens.empty()) {
            return false;
        }
        bool underline = false;
        bool line_through = false;
        for (const std::string& token : tokens) {
            if (token == "none") {
                underline = false;
                line_through = false;
            } else if (token == "underline") {
                underline = true;
            } else if (token == "line-through") {
                line_through = true;
            } else if (token == "solid") {
                continue;
            } else {
                return false;
            }
        }
        style.text_decoration_underline = underline;
        style.text_decoration_line_through = line_through;
        style.text_decoration_specified = true;
        return true;
    } else if (property == "text-shadow") {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        return false;
#else
        TextShadowStyle shadow;
        if (!parse_text_shadow_style(value, style.font_size, shadow)) {
            return false;
        }
        style.text_shadow = shadow;
        style.text_shadow_specified = true;
        return true;
#endif
    } else if (property == "box-shadow") {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        return false;
#else
        BoxShadowStyle shadow;
        if (!parse_box_shadow_style(value, style.font_size, shadow)) {
            return false;
        }
        style.box_shadow = shadow;
        return true;
#endif
    } else if (property == "outline-width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.outline_width = std::max(0, px);
        return true;
    } else if (property == "outline-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.outline_color = parsed;
        return true;
    } else if (property == "outline-offset") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.outline_offset = px;
        return true;
    } else if (property == "outline") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "none" || lowered == "0" || lowered == "0px") {
            style.outline_width = 0;
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
            style.outline_width = std::max(0, width);
        } else if (style.outline_width == 0) {
            style.outline_width = 1;
        }
        if (has_color) {
            style.outline_color = color;
        }
        return true;
    } else if (property == "overflow" || property == "overflow-y") {
        const std::string lowered = lowercase(trim(value));
        const bool supported = property == "overflow-y"
            ? (lowered == "auto" || lowered == "scroll")
            : (lowered == "visible" || lowered == "hidden" || lowered == "scroll" ||
               lowered == "auto" || lowered == "clip");
        if (!supported) {
            return false;
        }
        style.overflow = lowered;
        return true;
    } else if (property == "overflow-wrap") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "normal") {
            style.overflow_wrap_anywhere = false;
        } else if (lowered == "anywhere") {
            style.overflow_wrap_anywhere = true;
        } else {
            return false;
        }
        style.overflow_wrap_specified = true;
        return true;
    } else if (property == "white-space" || property == "text-wrap") {
        const std::string lowered = lowercase(trim(value));
        if ((property == "white-space" && lowered == "normal") ||
            (property == "text-wrap" && lowered == "wrap")) {
            style.white_space_nowrap = false;
            style.white_space_specified = true;
            return true;
        }
        if (lowered == "nowrap") {
            style.white_space_nowrap = true;
            style.white_space_specified = true;
            return true;
        }
        return false;
    } else if (property == "text-overflow") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "clip") {
            style.text_overflow_ellipsis = false;
            style.text_overflow_specified = true;
            return true;
        }
        if (lowered == "ellipsis") {
            style.text_overflow_ellipsis = true;
            style.text_overflow_specified = true;
            return true;
        }
        return false;
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
        if (lowercase(transformed) == "none") {
            style.transform.clear();
            return true;
        }
        Transform2D parsed;
        if (!parse_css_transform_2d(transformed, parsed)) {
            return false;
        }
        style.transform = serialize_css_transform_2d(parsed);
        return true;
    } else if (property == "transform-origin") {
        int x_percent = 50;
        int y_percent = 50;
        if (!parse_transform_origin_percent(value, x_percent, y_percent)) {
            return false;
        }
        style.transform_origin_x_percent = x_percent;
        style.transform_origin_y_percent = y_percent;
        return true;
    } else if (property == "position") {
        const std::string lowered = lowercase(trim(value));
        if (lowered != "static" && lowered != "relative" && lowered != "absolute" &&
            lowered != "fixed" && lowered != "sticky") {
            return false;
        }
        style.position = lowered == "static" ? std::string{} : lowered;
        return true;
    } else if (property == "top" || property == "right" ||
               property == "bottom" || property == "left") {
        int px = 0;
        bool specified = false;
        if (!parse_position_inset(value, style.font_size, px, specified)) {
            return false;
        }
        if (property == "top") {
            style.inset_top = px;
            style.inset_top_specified = specified;
        } else if (property == "right") {
            style.inset_right = px;
            style.inset_right_specified = specified;
        } else if (property == "bottom") {
            style.inset_bottom = px;
            style.inset_bottom_specified = specified;
        } else {
            style.inset_left = px;
            style.inset_left_specified = specified;
        }
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
    } else if (property == "justify-content" || property == "align-content") {
        const std::string lowered = lowercase(trim(value));
        JustifyContent& alignment = property == "justify-content"
            ? style.justify_content
            : style.align_content;
        if (lowered == "center") {
            alignment = JustifyContent::Center;
        } else if (lowered == "flex-end" || lowered == "end") {
            alignment = JustifyContent::End;
        } else if (lowered == "space-around") {
            alignment = JustifyContent::SpaceAround;
        } else if (lowered == "space-between") {
            alignment = JustifyContent::SpaceBetween;
        } else if (lowered == "space-evenly") {
            alignment = JustifyContent::SpaceEvenly;
        } else if (lowered == "flex-start" || lowered == "start" || lowered == "normal") {
            alignment = JustifyContent::Start;
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
    } else if (property == "align-self") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "auto") {
            style.align_self = AlignItems::Auto;
        } else if (lowered == "center") {
            style.align_self = AlignItems::Center;
        } else if (lowered == "flex-end" || lowered == "end") {
            style.align_self = AlignItems::End;
        } else if (lowered == "flex-start" || lowered == "start") {
            style.align_self = AlignItems::Start;
        } else if (lowered == "stretch" || lowered == "normal") {
            style.align_self = AlignItems::Stretch;
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
    } else if (property == "flex") {
        int grow = 0;
        int shrink = 0;
        int basis = -1;
        if (!parse_flex_shorthand(value, style.font_size, grow, shrink, basis)) {
            return false;
        }
        style.flex_grow = grow;
        style.flex_shrink = shrink;
        style.flex_basis = basis;
        return true;
    } else if (property == "flex-grow") {
        int factor = 0;
        if (!parse_flex_factor(value, factor)) {
            return false;
        }
        style.flex_grow = factor;
        return true;
    } else if (property == "flex-shrink") {
        int factor = 0;
        if (!parse_flex_factor(value, factor)) {
            return false;
        }
        style.flex_shrink = factor;
        return true;
    } else if (property == "flex-basis") {
        int basis = -1;
        if (!parse_flex_basis_value(value, style.font_size, basis)) {
            return false;
        }
        style.flex_basis = basis;
        return true;
    } else if (property == "flex-direction") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "row") {
            style.flex_direction = FlexDirection::Row;
        } else if (lowered == "column") {
            style.flex_direction = FlexDirection::Column;
        } else {
            return false;
        }
        return true;
    } else if (property == "order") {
        int order = 0;
        if (!parse_integer(trim(value), order) || order < -32768 || order > 32767) {
            return false;
        }
        style.flex_order = static_cast<std::int16_t>(order);
        return true;
    } else if (property == "flex-wrap") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "wrap") {
            style.flex_wrap = true;
        } else if (lowered == "nowrap") {
            style.flex_wrap = false;
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
        std::array<int, 4> widths{{0, 0, 0, 0}};
        int count = 0;
        if (parse_simple_grid_template_columns(value, widths, count, style.font_size)) {
            style.grid_template_column_widths = widths;
            style.grid_template_column_count = count;
            style.grid_min_track_width = -1;
            return true;
        }
        int min_track = 0;
        if (!parse_grid_template_columns_min(value, min_track, style.font_size)) {
            return false;
        }
        style.grid_min_track_width = min_track;
        style.grid_template_column_count = 0;
        return true;
    } else if (property == "grid-template-rows") {
        std::array<std::int16_t, 4> heights{{0, 0, 0, 0}};
        int count = 0;
        if (!parse_simple_grid_template_rows(value, heights, count, style.font_size)) {
            return false;
        }
        style.grid_template_row_heights = heights;
        style.grid_template_row_count = static_cast<std::uint8_t>(count);
        return true;
    } else if (property == "grid-auto-rows") {
        int min_row = 0;
        if (!parse_grid_auto_rows_min(value, min_row, style.font_size)) {
            return false;
        }
        style.grid_auto_row_min = min_row;
        return true;
    } else if (property == "grid-column") {
        int start = -1;
        int span = 1;
        if (!parse_grid_placement(value, start, span)) {
            return false;
        }
        style.grid_column_start = static_cast<std::int16_t>(start);
        style.grid_column_span = static_cast<std::uint8_t>(span);
        return true;
    } else if (property == "grid-row") {
        int start = -1;
        int span = 1;
        if (!parse_grid_placement(value, start, span)) {
            return false;
        }
        style.grid_row_start = static_cast<std::int16_t>(start);
        style.grid_row_span = static_cast<std::uint8_t>(span);
        return true;
    } else if (property == "object-fit") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "fill") {
            style.object_fit = ObjectFit::Fill;
        } else if (lowered == "contain") {
            style.object_fit = ObjectFit::Contain;
        } else if (lowered == "cover") {
            style.object_fit = ObjectFit::Cover;
        } else if (lowered == "none") {
            style.object_fit = ObjectFit::None;
        } else if (lowered == "scale-down") {
            style.object_fit = ObjectFit::ScaleDown;
        } else {
            return false;
        }
        update_package_background_image_presentation(style);
        return true;
    } else if (property == "object-position") {
        ObjectPosition position;
        if (!parse_object_position_value(value, position)) {
            return false;
        }
        style.object_position = position;
        update_package_background_image_presentation(style);
        return true;
    } else if (property == "image-rendering") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "auto") {
            style.image_rendering = ImageRendering::Auto;
        } else if (lowered == "pixelated") {
            style.image_rendering = ImageRendering::Pixelated;
        } else if (lowered == "crisp-edges") {
            style.image_rendering = ImageRendering::CrispEdges;
        } else {
            return false;
        }
        update_package_background_image_presentation(style);
        return true;
    } else if (property == "background-size") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "cover") {
            style.object_fit = ObjectFit::Cover;
        } else if (lowered == "contain") {
            style.object_fit = ObjectFit::Contain;
        } else if (lowered == "100% 100%") {
            style.object_fit = ObjectFit::Fill;
        } else {
            return false;
        }
        update_package_background_image_presentation(style);
        return true;
    } else if (property == "background-position") {
        ObjectPosition position;
        if (!parse_object_position_value(value, position)) {
            return false;
        }
        style.object_position = position;
        update_package_background_image_presentation(style);
        return true;
    } else if (property == "background-repeat") {
        return lowercase(trim(value)) == "no-repeat";
    } else if (property == "list-style" || property == "list-style-type") {
        std::istringstream stream(lowercase(trim(value)));
        std::string token;
        while (stream >> token) {
            ListStyleType type = ListStyleType::None;
            if (parse_list_style_type(token, type)) {
                style.list_style_type = type;
                style.list_style_type_specified = true;
                return true;
            }
        }
        return false;
    } else if (property == "transition") {
        return parse_transition_shorthand(value, style);
    } else if (property == "transition-property" ||
               property == "transition-duration" ||
               property == "transition-delay" ||
               property == "transition-timing-function") {
        return parse_transition_longhand(property, value, style);
    } else if (property == "animation") {
        return parse_animation_shorthand(value, style);
    } else if (property == "animation-name" ||
               property == "animation-duration" ||
               property == "animation-delay" ||
               property == "animation-timing-function" ||
               property == "animation-iteration-count" ||
               property == "animation-direction" ||
               property == "animation-fill-mode") {
        return parse_animation_longhand(property, value, style);
    }
    return false;
}

std::size_t find_matching_paren(std::string_view value, std::size_t open) {
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = open; index < value.size(); ++index) {
        const char ch = value[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < value.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::string_view::npos;
}

bool resolve_css_vars(std::string_view value,
                      const CustomPropertyMap& custom_properties,
                      std::string& output,
                      std::size_t max_output_bytes,
                      int depth = 0) {
    constexpr int kMaxVarDepth = 8;
    if (depth > kMaxVarDepth) {
        return false;
    }

    const auto append_bounded = [&](std::string_view part) {
        if (max_output_bytes != 0 &&
            (part.size() > max_output_bytes || output.size() > max_output_bytes - part.size())) {
            return false;
        }
        output.append(part);
        return true;
    };

    if (value.find("var(") == std::string_view::npos) {
        output.clear();
        return append_bounded(value);
    }

    output.clear();
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const std::size_t start = value.find("var(", cursor);
        if (start == std::string_view::npos) {
            return append_bounded(value.substr(cursor));
        }
        if (!append_bounded(value.substr(cursor, start - cursor))) {
            return false;
        }
        const std::size_t close = find_matching_paren(value, start + 3);
        if (close == std::string_view::npos) {
            append_bounded(value.substr(start));
            return false;
        }

        const std::string_view body = value.substr(start + 4, close - start - 4);
        const std::size_t comma = body.find(',');
        const std::string name_token = trim(body.substr(0, comma));
        if (name_token.empty()) {
            if (!append_bounded(value.substr(start, close - start + 1))) {
                return false;
            }
            return false;
        }
        const std::string name = lowercase(name_token);
        std::string replacement;
        const auto found = custom_properties.find(name);
        if (found != custom_properties.end()) {
            if (!resolve_css_vars(found->second, custom_properties, replacement, max_output_bytes, depth + 1)) {
                return false;
            }
        } else if (comma != std::string_view::npos) {
            if (!resolve_css_vars(body.substr(comma + 1), custom_properties, replacement,
                                  max_output_bytes, depth + 1)) {
                return false;
            }
            replacement = trim(replacement);
        } else {
            append_bounded(value.substr(start, close - start + 1));
            return false;
        }
        if (!append_bounded(replacement)) {
            return false;
        }
        cursor = close + 1;
    }
    return true;
}

const CssDeclaration& resolve_declaration_value(const CssDeclaration& declaration,
                                                const CustomPropertyMap& custom_properties,
                                                CssDeclaration& scratch,
                                                std::size_t max_resolved_value_bytes) {
    if (declaration.value.find("var(") == std::string::npos) {
        return declaration;
    }
    scratch = declaration;
    std::string value;
    if (resolve_css_vars(declaration.value, custom_properties, value, max_resolved_value_bytes)) {
        scratch.value = trim(value);
    }
    return scratch;
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

int& edge_value(EdgeSizes& edges, CascadeProperty property) {
    switch (property) {
    case CascadeProperty::MarginTop:
    case CascadeProperty::PaddingTop:
    case CascadeProperty::BorderTopWidth:
        return edges.top;
    case CascadeProperty::MarginRight:
    case CascadeProperty::PaddingRight:
    case CascadeProperty::BorderRightWidth:
        return edges.right;
    case CascadeProperty::MarginBottom:
    case CascadeProperty::PaddingBottom:
    case CascadeProperty::BorderBottomWidth:
        return edges.bottom;
    default:
        return edges.left;
    }
}

void apply_edge_value(CascadeSlots& slots,
                      CascadeProperty property,
                      EdgeSizes& target,
                      int value,
                      const CssDeclaration& declaration,
                      const CssSpecificity& specificity,
                      std::size_t source_order) {
    CascadeSlot& slot = cascade_slot(slots, property);
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return;
    }
    edge_value(target, property) = value;
    mark_slot(slot, declaration, specificity, source_order);
}

void apply_margin_edge_value(Style& style,
                             CascadeSlots& slots,
                             CascadeProperty property,
                             int value,
                             bool is_auto,
                             const CssDeclaration& declaration,
                             const CssSpecificity& specificity,
                             std::size_t source_order) {
    CascadeSlot& slot = cascade_slot(slots, property);
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return;
    }
    edge_value(style.margin, property) = value;
    if (property == CascadeProperty::MarginLeft) {
        style.margin_left_auto = is_auto;
    } else if (property == CascadeProperty::MarginRight) {
        style.margin_right_auto = is_auto;
    }
    mark_slot(slot, declaration, specificity, source_order);
}

bool apply_edge_shorthand(Style& style,
                          CascadeSlots& slots,
                          const CssDeclaration& declaration,
                          const CssSpecificity& specificity,
                          std::size_t source_order) {
    if (declaration.property == "margin") {
        EdgeSizes parsed;
        bool left_auto = false;
        bool right_auto = false;
        if (!parse_margin_edge_px(declaration.value, parsed, left_auto, right_auto, style.font_size)) {
            return true;
        }
        apply_margin_edge_value(style, slots, CascadeProperty::MarginTop, parsed.top, false,
                                declaration, specificity, source_order);
        apply_margin_edge_value(style, slots, CascadeProperty::MarginRight, parsed.right, right_auto,
                                declaration, specificity, source_order);
        apply_margin_edge_value(style, slots, CascadeProperty::MarginBottom, parsed.bottom, false,
                                declaration, specificity, source_order);
        apply_margin_edge_value(style, slots, CascadeProperty::MarginLeft, parsed.left, left_auto,
                                declaration, specificity, source_order);
        return true;
    }
    if (declaration.property == "padding") {
        EdgeSizes parsed;
        if (!parse_box_edge_px(declaration.value, parsed, style.font_size)) {
            return true;
        }
        apply_edge_value(slots, CascadeProperty::PaddingTop, style.padding, parsed.top,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::PaddingRight, style.padding, parsed.right,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::PaddingBottom, style.padding, parsed.bottom,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::PaddingLeft, style.padding, parsed.left,
                         declaration, specificity, source_order);
        return true;
    }
    if (declaration.property == "border-width") {
        EdgeSizes parsed;
        if (!parse_box_edge_px(declaration.value, parsed, style.font_size)) {
            return true;
        }
        apply_edge_value(slots, CascadeProperty::BorderTopWidth, style.border_width, parsed.top,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::BorderRightWidth, style.border_width, parsed.right,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::BorderBottomWidth, style.border_width, parsed.bottom,
                         declaration, specificity, source_order);
        apply_edge_value(slots, CascadeProperty::BorderLeftWidth, style.border_width, parsed.left,
                         declaration, specificity, source_order);
        return true;
    }
    if (declaration.property == "border") {
        const BorderShorthandParseResult parsed = parse_border_shorthand(declaration.value, style.font_size);
        if (!parsed.has_width && !parsed.has_color) {
            return true;
        }
        if (parsed.has_width) {
            apply_edge_value(slots, CascadeProperty::BorderTopWidth, style.border_width, parsed.width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderRightWidth, style.border_width, parsed.width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderBottomWidth, style.border_width, parsed.width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderLeftWidth, style.border_width, parsed.width,
                             declaration, specificity, source_order);
        }
        if (parsed.has_color) {
            CascadeSlot& slot = cascade_slot(slots, CascadeProperty::BorderColor);
            if (declaration_wins(slot, declaration, specificity, source_order)) {
                style.border_color = parsed.color;
                mark_slot(slot, declaration, specificity, source_order);
            }
        }
        return true;
    }
    if (declaration.property == "border-top" || declaration.property == "border-right" ||
        declaration.property == "border-bottom" || declaration.property == "border-left") {
        const BorderShorthandParseResult parsed = parse_border_shorthand(declaration.value, style.font_size);
        if (!parsed.has_width && !parsed.has_color) {
            return true;
        }
        if (parsed.has_width) {
            CascadeProperty edge_property = CascadeProperty::BorderTopWidth;
            if (declaration.property == "border-right") {
                edge_property = CascadeProperty::BorderRightWidth;
            } else if (declaration.property == "border-bottom") {
                edge_property = CascadeProperty::BorderBottomWidth;
            } else if (declaration.property == "border-left") {
                edge_property = CascadeProperty::BorderLeftWidth;
            }
            apply_edge_value(slots, edge_property, style.border_width, parsed.width,
                             declaration, specificity, source_order);
        }
        if (parsed.has_color) {
            CascadeSlot& slot = cascade_slot(slots, CascadeProperty::BorderColor);
            if (declaration_wins(slot, declaration, specificity, source_order)) {
                style.border_color = parsed.color;
                mark_slot(slot, declaration, specificity, source_order);
            }
        }
        return true;
    }
    return false;
}

bool apply_cascaded_declaration(Style& style,
                                CascadeSlot& slot,
                                const CssDeclaration& declaration,
                                const CssSpecificity& specificity,
                                std::size_t source_order,
                                const StyleResolver* resolver) {
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return true;
    }
    if (apply_declaration(style, declaration.property, declaration.value, resolver)) {
        mark_slot(slot, declaration, specificity, source_order);
        return true;
    }
    return false;
}

bool parse_counter_content(const std::string& raw_value, std::string& name, std::string& suffix) {
    const std::string value = trim(raw_value);
    constexpr std::string_view prefix = "counter(";
    if (value.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::size_t close = value.find(')', prefix.size());
    if (close == std::string::npos) {
        return false;
    }
    name = trim(std::string_view(value).substr(prefix.size(), close - prefix.size()));
    suffix = trim(std::string_view(value).substr(close + 1));
    if (!suffix.empty()) {
        suffix = unquote(suffix);
    }
    return !name.empty();
}

bool apply_generated_declaration(Style& style,
                                 CssPseudoElement pseudo,
                                 const std::string& property,
                                 const std::string& value) {
    GeneratedContentKind& content_kind = pseudo == CssPseudoElement::After
        ? style.after_content_kind
        : style.before_content_kind;
    std::string& content_text = pseudo == CssPseudoElement::After
        ? style.after_content_text
        : style.before_content_text;
    std::string& counter_name_output = pseudo == CssPseudoElement::After
        ? style.after_counter_name
        : style.before_counter_name;
    std::string& counter_suffix_output = pseudo == CssPseudoElement::After
        ? style.after_counter_suffix
        : style.before_counter_suffix;
    Color& color = pseudo == CssPseudoElement::After ? style.after_color : style.before_color;
    bool& color_specified = pseudo == CssPseudoElement::After
        ? style.after_color_specified
        : style.before_color_specified;
    int& font_weight = pseudo == CssPseudoElement::After ? style.after_font_weight : style.before_font_weight;
    bool& font_weight_specified = pseudo == CssPseudoElement::After
        ? style.after_font_weight_specified
        : style.before_font_weight_specified;
    int& left = pseudo == CssPseudoElement::After ? style.after_left : style.before_left;
    bool& left_specified = pseudo == CssPseudoElement::After ? style.after_left_specified : style.before_left_specified;

    if (property == "content") {
        std::string counter_name;
        std::string suffix;
        if (parse_counter_content(value, counter_name, suffix)) {
            content_kind = GeneratedContentKind::Counter;
            counter_name_output = std::move(counter_name);
            counter_suffix_output = std::move(suffix);
            content_text.clear();
            return true;
        }
        const std::string text = unquote(value);
        if (text.empty() || text == "none" || text == "normal") {
            content_kind = GeneratedContentKind::None;
            content_text.clear();
            counter_name_output.clear();
            counter_suffix_output.clear();
            return true;
        }
        content_kind = GeneratedContentKind::Text;
        content_text = text;
        counter_name_output.clear();
        counter_suffix_output.clear();
        return true;
    }
    if (property == "color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        color = parsed;
        color_specified = true;
        return true;
    }
    if (property == "font-weight") {
        int weight = 400;
        if (!parse_font_weight(value, weight)) {
            return false;
        }
        font_weight = weight;
        font_weight_specified = true;
        return true;
    }
    if (property == "left") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        left = px;
        left_specified = true;
        return true;
    }
    return false;
}

bool apply_cascaded_generated_declaration(Style& style,
                                          CssPseudoElement pseudo,
                                          CascadeSlot& slot,
                                          const CssDeclaration& declaration,
                                          const CssSpecificity& specificity,
                                          std::size_t source_order) {
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return true;
    }
    if (apply_generated_declaration(style, pseudo, declaration.property, declaration.value)) {
        mark_slot(slot, declaration, specificity, source_order);
        return true;
    }
    return false;
}

struct LogicalDeclarationExpansion {
    std::array<CssDeclaration, 4> declarations;
    std::size_t count = 0;
};

void append_logical_declaration(LogicalDeclarationExpansion& expansion,
                                const CssDeclaration& source,
                                std::string_view property,
                                std::string_view value) {
    if (expansion.count >= expansion.declarations.size()) {
        return;
    }
    CssDeclaration& target = expansion.declarations[expansion.count++];
    target = source;
    target.property.assign(property);
    target.value.assign(value);
}

// JellyFrame currently has one horizontal LTR writing mode. Expanding these
// aliases before the cascade reuses physical-property slots and preserves the
// existing importance, specificity and source-order behavior.
bool expand_logical_declaration(const CssDeclaration& declaration,
                                LogicalDeclarationExpansion& expansion) {
    const auto expand_two_sides = [&](std::string_view first_property, std::string_view second_property) {
        const std::vector<std::string> values = split_whitespace_components(declaration.value);
        if (values.empty() || values.size() > 2) {
            return;
        }
        append_logical_declaration(expansion, declaration, first_property, values[0]);
        append_logical_declaration(expansion, declaration, second_property,
                                   values.size() == 2 ? values[1] : values[0]);
    };
    const auto expand_four_sides = [&] {
        const std::vector<std::string> values = split_whitespace_components(declaration.value);
        if (values.empty() || values.size() > 4) {
            return;
        }
        const std::string_view top = values[0];
        const std::string_view right = values.size() == 1 ? values[0] : values[1];
        const std::string_view bottom = values.size() <= 2 ? values[0] : values[2];
        const std::string_view left = values.size() == 1 ? values[0] : values.size() == 2 ? values[1] : values.size() == 3 ? values[1] : values[3];
        append_logical_declaration(expansion, declaration, "top", top);
        append_logical_declaration(expansion, declaration, "right", right);
        append_logical_declaration(expansion, declaration, "bottom", bottom);
        append_logical_declaration(expansion, declaration, "left", left);
    };
    const auto map_single = [&](std::string_view physical) {
        append_logical_declaration(expansion, declaration, physical, declaration.value);
    };

    if (declaration.property == "margin-inline") {
        expand_two_sides("margin-left", "margin-right");
    } else if (declaration.property == "margin-block") {
        expand_two_sides("margin-top", "margin-bottom");
    } else if (declaration.property == "padding-inline") {
        expand_two_sides("padding-left", "padding-right");
    } else if (declaration.property == "padding-block") {
        expand_two_sides("padding-top", "padding-bottom");
    } else if (declaration.property == "border-inline-width") {
        expand_two_sides("border-left-width", "border-right-width");
    } else if (declaration.property == "border-block-width") {
        expand_two_sides("border-top-width", "border-bottom-width");
    } else if (declaration.property == "inset") {
        expand_four_sides();
    } else if (declaration.property == "inset-inline") {
        expand_two_sides("left", "right");
    } else if (declaration.property == "inset-block") {
        expand_two_sides("top", "bottom");
    } else if (declaration.property == "place-content") {
        const std::vector<std::string> values = split_whitespace_components(declaration.value);
        if (values.empty() || values.size() > 2) {
            return true;
        }
        append_logical_declaration(expansion, declaration, "align-content", values[0]);
        append_logical_declaration(expansion, declaration, "justify-content",
                                   values.size() == 2 ? values[1] : values[0]);
    } else if (declaration.property == "inline-size") {
        map_single("width");
    } else if (declaration.property == "block-size") {
        map_single("height");
    } else if (declaration.property == "min-inline-size") {
        map_single("min-width");
    } else if (declaration.property == "min-block-size") {
        map_single("min-height");
    } else if (declaration.property == "max-inline-size") {
        map_single("max-width");
    } else if (declaration.property == "max-block-size") {
        map_single("max-height");
    } else if (declaration.property == "margin-inline-start") {
        map_single("margin-left");
    } else if (declaration.property == "margin-inline-end") {
        map_single("margin-right");
    } else if (declaration.property == "margin-block-start") {
        map_single("margin-top");
    } else if (declaration.property == "margin-block-end") {
        map_single("margin-bottom");
    } else if (declaration.property == "padding-inline-start") {
        map_single("padding-left");
    } else if (declaration.property == "padding-inline-end") {
        map_single("padding-right");
    } else if (declaration.property == "padding-block-start") {
        map_single("padding-top");
    } else if (declaration.property == "padding-block-end") {
        map_single("padding-bottom");
    } else if (declaration.property == "border-inline-start-width") {
        map_single("border-left-width");
    } else if (declaration.property == "border-inline-end-width") {
        map_single("border-right-width");
    } else if (declaration.property == "border-block-start-width") {
        map_single("border-top-width");
    } else if (declaration.property == "border-block-end-width") {
        map_single("border-bottom-width");
    } else if (declaration.property == "inset-inline-start") {
        map_single("left");
    } else if (declaration.property == "inset-inline-end") {
        map_single("right");
    } else if (declaration.property == "inset-block-start") {
        map_single("top");
    } else if (declaration.property == "inset-block-end") {
        map_single("bottom");
    } else {
        return false;
    }
    return true;
}

void apply_declarations(Style& style,
                        CascadeSlots& slots,
                        const std::vector<CssDeclaration>& declarations,
                        const CssSpecificity& specificity,
                        std::size_t source_order,
                        CssPseudoElement pseudo_element,
                        const CustomPropertyMap& custom_properties,
                        DiagnosticSink* diagnostics,
                        const StyleResolver* resolver,
                        std::size_t max_resolved_value_bytes) {
    CssDeclaration resolved_scratch;
    for (const CssDeclaration& declaration : declarations) {
        if (is_custom_property_name(declaration.property)) {
            continue;
        }
        const CssDeclaration& resolved_declaration =
            resolve_declaration_value(declaration, custom_properties, resolved_scratch,
                                      max_resolved_value_bytes);
        LogicalDeclarationExpansion expansion;
        const bool is_logical = expand_logical_declaration(resolved_declaration, expansion);
        if (is_logical && expansion.count == 0) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Warning,
                              "style-declaration-ignored",
                              "CSS declaration could not be applied by the supported style subset",
                              resolved_declaration.property + ": " + resolved_declaration.value);
            continue;
        }
        const std::size_t expansion_count = is_logical ? expansion.count : 1;
        for (std::size_t index = 0; index < expansion_count; ++index) {
            const CssDeclaration& applied_declaration = is_logical
                ? expansion.declarations[index]
                : resolved_declaration;
        if (pseudo_element != CssPseudoElement::None) {
            CascadeSlot* slot = cascade_slot_for_generated_property(slots, pseudo_element, applied_declaration.property);
            if (slot != nullptr) {
                if (!apply_cascaded_generated_declaration(style,
                                                          pseudo_element,
                                                          *slot,
                                                          applied_declaration,
                                                          specificity,
                                                          source_order)) {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      pseudo_element == CssPseudoElement::After
                                          ? "style-after-declaration-ignored"
                                          : "style-before-declaration-ignored",
                                      "Pseudo-element declaration could not be applied",
                                      applied_declaration.property + ": " + applied_declaration.value);
                }
            } else {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::Style,
                                  DiagnosticSeverity::Info,
                                  pseudo_element == CssPseudoElement::After
                                      ? "style-after-property-unsupported"
                                      : "style-before-property-unsupported",
                                  "Pseudo-element property is outside the supported subset",
                                  applied_declaration.property);
            }
            continue;
        }
        if (apply_edge_shorthand(style, slots, applied_declaration, specificity, source_order)) {
            continue;
        }
        CascadeSlot* slot = cascade_slot_for_property(slots, applied_declaration.property);
        if (slot != nullptr) {
            if (!apply_cascaded_declaration(style,
                                            *slot,
                                            applied_declaration,
                                            specificity,
                                            source_order,
                                            resolver)) {
                const std::string conic_detail =
                    (applied_declaration.property == "background" ||
                     applied_declaration.property == "background-image")
                        ? conic_gradient_failure_detail(applied_declaration.value)
                        : std::string{};
                const std::string radial_detail =
                    (applied_declaration.property == "background" ||
                     applied_declaration.property == "background-image")
                        ? radial_gradient_failure_detail(applied_declaration.value)
                        : std::string{};
                if (!conic_detail.empty()) {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      "style-conic-gradient-unsupported",
                                      "conic-gradient() is outside the supported progress-ring subset",
                                      conic_detail);
                } else if (!radial_detail.empty()) {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      "style-radial-gradient-unsupported",
                                      "radial-gradient() is outside the supported center-circle subset",
                                      radial_detail);
                } else {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      "style-declaration-ignored",
                                      "CSS declaration could not be applied by the supported style subset",
                                      applied_declaration.property + ": " + applied_declaration.value);
                }
            }
        } else {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Info,
                              "style-property-unsupported",
                              "CSS property is outside the supported subset and was ignored",
                              applied_declaration.property);
        }
        }
    }
}

std::vector<CssDeclaration> parse_inline_style(const std::string& source,
                                               std::size_t max_source_bytes,
                                               std::size_t max_declarations,
                                               DiagnosticSink* diagnostics = nullptr) {
    std::vector<CssDeclaration> declarations;
    std::size_t source_size = source.size();
    if (max_source_bytes != 0 && source_size > max_source_bytes) {
        const std::size_t bounded_end = source.rfind(';', max_source_bytes - 1);
        source_size = bounded_end == std::string::npos ? 0 : bounded_end + 1;
        report_diagnostic(diagnostics,
                          DiagnosticStage::Style,
                          DiagnosticSeverity::Warning,
                          "style-inline-input-limit",
                          "Inline style exceeded its byte budget; trailing declarations were skipped",
                          {});
    }
    if (source_size == 0) {
        return declarations;
    }
    std::size_t index = 0;
    while (index < source_size) {
        if (max_declarations != 0 && declarations.size() >= max_declarations) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Warning,
                              "style-inline-declaration-limit",
                              "Inline style declaration budget was reached; trailing declarations were skipped",
                              {});
            break;
        }
        const std::size_t colon = source.find(':', index);
        if (colon == std::string::npos || colon >= source_size) {
            const std::string remaining = trim(std::string_view(source).substr(index, source_size - index));
            if (!remaining.empty()) {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::Style,
                                  DiagnosticSeverity::Warning,
                                  "style-inline-declaration-malformed",
                                  "Inline style declaration without ':' was ignored",
                                  remaining);
            }
            break;
        }
        const std::size_t semicolon = source.find(';', colon + 1);
        const std::size_t end = semicolon == std::string::npos || semicolon >= source_size
            ? source_size
            : semicolon;
        CssDeclaration declaration;
        declaration.property = lowercase(trim(std::string_view(source).substr(index, colon - index)));
        declaration.value = trim(std::string_view(source).substr(colon + 1, end - colon - 1));
        if (!declaration.property.empty() && !declaration.value.empty()) {
            declarations.push_back(std::move(declaration));
        } else {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Warning,
                              "style-inline-declaration-malformed",
                              "Inline style declaration had an empty property or value and was ignored",
                              trim(std::string_view(source).substr(index, end - index)));
        }
        index = end < source_size ? end + 1 : source_size;
    }
    return declarations;
}

Style default_style_for(const Node& node) {
    Style style;
    if (node.type == NodeType::Text) {
        style.display = Display::Inline;
        return style;
    }

    static constexpr std::array<std::string_view, 37> block_tags = {
        "document", "html", "body", "div", "p", "section", "article", "header", "footer", "main",
        "nav", "aside", "form", "fieldset", "dialog", "details", "summary", "blockquote", "address",
        "hgroup", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "table", "tr", "td",
        "th", "dl", "dt", "dd", "app-root"
    };
    static constexpr std::array<std::string_view, 8> hidden_tags = {
        "head", "script", "style", "meta", "link", "title", "template", "noscript"
    };

    if (node.attributes.find("hidden") != node.attributes.end()) {
        style.display = Display::None;
        return style;
    }
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
        style.font_weight = 700;
        style.font_weight_specified = true;
        style.margin.bottom = 8;
    } else if (node.tag_name == "h2") {
        style.font_size = 20;
        style.font_size_specified = true;
        style.font_weight = 700;
        style.font_weight_specified = true;
        style.margin.bottom = 6;
    } else if (node.tag_name == "h3" || node.tag_name == "h4" || node.tag_name == "h5" ||
               node.tag_name == "h6" || node.tag_name == "strong" || node.tag_name == "b") {
        style.font_weight = 700;
        style.font_weight_specified = true;
    } else if (node.tag_name == "p") {
        style.margin.bottom = 6;
    } else if (node.tag_name == "ul") {
        style.list_style_type = ListStyleType::Disc;
        style.list_style_type_specified = true;
        style.padding.left = 20;
        style.margin.bottom = 6;
    } else if (node.tag_name == "ol") {
        style.list_style_type = ListStyleType::Decimal;
        style.list_style_type_specified = true;
        style.padding.left = 20;
        style.margin.bottom = 6;
    } else if (node.tag_name == "dl") {
        style.margin.bottom = 6;
    } else if (node.tag_name == "dd") {
        style.margin.left = 18;
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
        if (node.attributes.find("disabled") != node.attributes.end()) {
            style.color = Color{107, 114, 128, 255};
            style.color_specified = true;
            style.background_color = Color{229, 231, 235, 255};
        }
    } else if (node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea") {
        style.display = Display::InlineBlock;
        style.padding = EdgeSizes{4, 6, 4, 6};
        style.border_width = EdgeSizes{1, 1, 1, 1};
        style.border_color = Color{107, 114, 128, 255};
        style.background_color = Color{255, 255, 255, 255};
        if (node.attributes.find("disabled") != node.attributes.end()) {
            style.color = Color{107, 114, 128, 255};
            style.color_specified = true;
            style.background_color = Color{229, 231, 235, 255};
        }
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
    while (end > 0) {
        while (end > 0 && std::isspace(static_cast<unsigned char>(selector[end - 1])) != 0) {
            --end;
        }
        if (end == 0) {
            break;
        }

        std::size_t begin = end;
        int bracket_depth = 0;
        int paren_depth = 0;
        char quote = '\0';
        while (begin > 0) {
            const char ch = selector[begin - 1];
            if (quote != '\0') {
                if (ch == quote) {
                    quote = '\0';
                }
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == ')') {
                ++paren_depth;
            } else if (ch == '(' && paren_depth > 0) {
                --paren_depth;
            } else if (paren_depth > 0) {
                --begin;
                continue;
            } else if (ch == ']') {
                ++bracket_depth;
            } else if (ch == '[' && bracket_depth > 0) {
                --bracket_depth;
            } else if (bracket_depth == 0 && (ch == '>' || ch == '+' || ch == '~')) {
                break;
            } else if (bracket_depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
                break;
            }
            --begin;
        }

        CssSelectorPart part;
        part.compound = trim(selector.substr(begin, end - begin));
        part.combinator_to_left = CssSelectorCombinator::Descendant;
        std::size_t previous = begin;
        while (previous > 0 && std::isspace(static_cast<unsigned char>(selector[previous - 1])) != 0) {
            --previous;
        }
        if (previous > 0 && (selector[previous - 1] == '>' || selector[previous - 1] == '+' ||
                             selector[previous - 1] == '~')) {
            const char combinator = selector[previous - 1];
            if (combinator == '>') {
                part.combinator_to_left = CssSelectorCombinator::Child;
            } else if (combinator == '+') {
                part.combinator_to_left = CssSelectorCombinator::AdjacentSibling;
            } else {
                part.combinator_to_left = CssSelectorCombinator::GeneralSibling;
            }
            --previous;
            while (previous > 0 && std::isspace(static_cast<unsigned char>(selector[previous - 1])) != 0) {
                --previous;
            }
        }
        if (!part.compound.empty()) {
            parts.push_back(std::move(part));
        }
        end = previous;
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

void CssStyleSheet::push_keyframes(CssKeyframesRule rule) {
    keyframes_.push_back(std::move(rule));
}

std::size_t CssStyleSheet::size() const {
    return rules_.size();
}

std::size_t CssStyleSheet::keyframes_size() const {
    return keyframes_.size();
}

bool CssStyleSheet::empty() const {
    return rules_.empty() && keyframes_.empty();
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

const CssStyleSheet::KeyframesList& CssStyleSheet::keyframes() const {
    return keyframes_;
}

const CssKeyframesRule* CssStyleSheet::find_keyframes(std::string_view name) const {
    for (auto it = keyframes_.rbegin(); it != keyframes_.rend(); ++it) {
        if (it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

bool parse_css_transform_2d(std::string_view raw_value, Transform2D& output) {
    const std::string value = lowercase(trim(raw_value));
    output = Transform2D{};
    if (value.empty() || value == "none") {
        return true;
    }
    const std::vector<std::string> functions = split_transform_functions(value);
    if (functions.empty()) {
        return false;
    }
    for (const std::string& function : functions) {
        if (!parse_transform_function(function, output)) {
            return false;
        }
    }
    return true;
}

std::string serialize_css_transform_2d(const Transform2D& transform) {
    if (std::abs(transform.translate_x) < 0.01F &&
        std::abs(transform.translate_y) < 0.01F &&
        std::abs(transform.scale_x - 1.0F) < 0.001F &&
        std::abs(transform.scale_y - 1.0F) < 0.001F &&
        std::abs(transform.rotate_degrees) < 0.001F) {
        return {};
    }
    std::ostringstream stream;
    if (std::abs(transform.translate_x) >= 0.01F || std::abs(transform.translate_y) >= 0.01F) {
        stream << "translate(" << transform.translate_x << "px," << transform.translate_y << "px)";
    }
    if (std::abs(transform.scale_x - 1.0F) >= 0.001F ||
        std::abs(transform.scale_y - 1.0F) >= 0.001F) {
        if (stream.tellp() > 0) {
            stream << ' ';
        }
        stream << "scale(" << transform.scale_x << ',' << transform.scale_y << ')';
    }
    if (std::abs(transform.rotate_degrees) >= 0.001F) {
        if (stream.tellp() > 0) {
            stream << ' ';
        }
        stream << "rotate(" << transform.rotate_degrees << "deg)";
    }
    return stream.str();
}

bool apply_keyframe_declaration(Style& style, const CssDeclaration& declaration, DiagnosticSink* diagnostics) {
    if (declaration.property == "opacity" ||
        declaration.property == "transform" ||
        declaration.property == "color" ||
        declaration.property == "background" ||
        declaration.property == "background-color") {
        if (apply_declaration(style, declaration.property, declaration.value)) {
            return true;
        }
        report_diagnostic(diagnostics,
                          DiagnosticStage::Style,
                          DiagnosticSeverity::Warning,
                          "animation-keyframe-declaration-ignored",
                          "Keyframe declaration could not be applied by the supported animation subset",
                          declaration.property + ": " + declaration.value);
        return false;
    }
    report_diagnostic(diagnostics,
                      DiagnosticStage::Style,
                      DiagnosticSeverity::Info,
                      "animation-keyframe-property-unsupported",
                      "Keyframe property is outside the supported animation subset and was ignored",
                      declaration.property);
    return false;
}

StyleResolver::StyleResolver(Stylesheet stylesheet, StyleResolverOptions options)
    : stylesheet_(std::move(stylesheet)),
      options_(options) {
    build_rule_index();
    for (const CssRule& rule : stylesheet_) {
        for (const CssDeclaration& declaration : rule.declarations) {
            if (declaration.property != "background" && declaration.property != "background-image") {
                continue;
            }
            std::string_view url;
            if (parse_package_background_image_url(declaration.value, url)) {
                background_image_resource_id_for(url);
            }
        }
    }
}

void StyleResolver::build_rule_index() {
    for (const CssRule& rule : stylesheet_) {
        add_interaction_hints_for_selector(rule.selector, interaction_hints_);
        for (const CssDeclaration& declaration : rule.declarations) {
            if (is_custom_property_name(declaration.property)) {
                has_custom_property_declarations_ = true;
                break;
            }
        }
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

const std::vector<const CssRule*>& StyleResolver::candidate_rules_for(const Node& node) const {
    std::string key;
    if (node.type == NodeType::Element) {
        const std::string& id = node.attribute("id");
        const std::string& classes = node.attribute("class");
        key.reserve(node.tag_name.size() + id.size() + classes.size() + 8);
        if (tag_rules_.find(node.tag_name) != tag_rules_.end()) {
            key.append(node.tag_name);
        }
        key.push_back('\n');
        if (id_rules_.find(id) != id_rules_.end()) {
            key.append(id);
        }
        key.push_back('\n');

        // Selector matching treats the class attribute as an unordered set.
        // Keep the bounded candidate cache aligned with that meaning so a
        // reordered or repeated relevant class does not consume another entry.
        constexpr std::size_t kInlineIndexedClasses = 8;
        std::array<std::string_view, kInlineIndexedClasses> inline_indexed_classes;
        std::size_t inline_indexed_class_count = 0;
        std::vector<std::string_view> overflow_indexed_classes;
        std::size_t index = 0;
        while (index < classes.size()) {
            while (index < classes.size() && std::isspace(static_cast<unsigned char>(classes[index])) != 0) {
                ++index;
            }
            const std::size_t begin = index;
            while (index < classes.size() && std::isspace(static_cast<unsigned char>(classes[index])) == 0) {
                ++index;
            }
            if (begin == index) {
                continue;
            }
            const std::string_view class_name(classes.data() + begin, index - begin);
            if (class_rules_.find(std::string(class_name)) != class_rules_.end()) {
                if (inline_indexed_class_count < inline_indexed_classes.size()) {
                    inline_indexed_classes[inline_indexed_class_count++] = class_name;
                } else {
                    overflow_indexed_classes.push_back(class_name);
                }
            }
        }
        if (overflow_indexed_classes.empty()) {
            auto indexed_end = inline_indexed_classes.begin() +
                static_cast<std::ptrdiff_t>(inline_indexed_class_count);
            std::sort(inline_indexed_classes.begin(), indexed_end);
            indexed_end = std::unique(inline_indexed_classes.begin(), indexed_end);
            for (auto current = inline_indexed_classes.begin(); current != indexed_end; ++current) {
                key.append(*current);
                key.push_back('\n');
            }
        } else {
            overflow_indexed_classes.reserve(overflow_indexed_classes.size() + inline_indexed_class_count);
            overflow_indexed_classes.insert(overflow_indexed_classes.end(),
                                            inline_indexed_classes.begin(),
                                            inline_indexed_classes.begin() +
                                                static_cast<std::ptrdiff_t>(inline_indexed_class_count));
            std::sort(overflow_indexed_classes.begin(), overflow_indexed_classes.end());
            overflow_indexed_classes.erase(
                std::unique(overflow_indexed_classes.begin(), overflow_indexed_classes.end()),
                overflow_indexed_classes.end());
            for (const std::string_view class_name : overflow_indexed_classes) {
                key.append(class_name);
                key.push_back('\n');
            }
        }
    } else {
        key = "#text";
    }

    const auto cached = candidate_cache_.find(key);
    if (cached != candidate_cache_.end()) {
        ++statistics_.candidate_cache_hits;
        return cached->second;
    }
    ++statistics_.candidate_cache_misses;

    std::vector<const CssRule*> candidates;
    candidates.reserve(16);
    std::unordered_set<const CssRule*> seen_rules;
    seen_rules.reserve(32);
    const auto append_bucket = [&](const std::vector<const CssRule*>* bucket) {
        if (bucket == nullptr) {
            return;
        }
        for (const CssRule* rule : *bucket) {
            if (seen_rules.insert(rule).second) {
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

    if (options_.max_candidate_cache_entries == 0 ||
        candidate_cache_.size() >= options_.max_candidate_cache_entries) {
        // Retain hot entries instead of clearing the entire table. The scratch
        // vector is overwritten on the next miss and never grows the cache.
        uncached_candidates_ = std::move(candidates);
        ++statistics_.candidate_cache_bypasses;
        return uncached_candidates_;
    }

    auto inserted = candidate_cache_.emplace(std::move(key), std::move(candidates));
    statistics_.candidate_cache_entries = candidate_cache_.size();
    statistics_.candidate_cache_rule_refs += inserted.first->second.size();
    return inserted.first->second;
}

SelectorMatchContext selector_match_context_from_options(const StyleResolverOptions& options) {
    return SelectorMatchContext{options.hovered_node, options.active_node, options.focused_node};
}

void apply_custom_declarations(CustomPropertySlots& local,
                               const std::vector<CssDeclaration>& declarations,
                               const CssSpecificity& specificity,
                               std::size_t source_order) {
    for (const CssDeclaration& declaration : declarations) {
        if (!is_custom_property_name(declaration.property)) {
            continue;
        }
        CustomPropertySlot& slot = local[declaration.property];
        if (!custom_declaration_wins(slot, declaration, specificity, source_order)) {
            continue;
        }
        slot.set = true;
        slot.important = declaration.important;
        slot.specificity = specificity;
        slot.source_order = source_order;
        slot.value = declaration.value;
    }
}

bool node_has_inline_custom_property(const Node& node) {
    return node.type == NodeType::Element && node.attribute("style").find("--") != std::string::npos;
}

bool subtree_has_inline_custom_property(const Node& node) {
    if (node_has_inline_custom_property(node)) {
        return true;
    }
    for (const auto& child : node.children) {
        if (subtree_has_inline_custom_property(*child)) {
            return true;
        }
    }
    return false;
}

bool StyleResolver::apply_custom_properties_for_node(CustomPropertyMap& inherited,
                                                      const Node& node,
                                                      const std::vector<const CssRule*>* matched_rules) const {
    if (node.type != NodeType::Element) {
        return false;
    }
    const SelectorMatchContext context = selector_match_context_from_options(options_);
    CustomPropertySlots local;
    if (has_custom_property_declarations_) {
        const std::vector<const CssRule*>& rules = matched_rules != nullptr
            ? *matched_rules
            : candidate_rules_for(node);
        for (const CssRule* rule : rules) {
            if (rule->pseudo_element == CssPseudoElement::None &&
                (matched_rules != nullptr || matches_rule(node, *rule, context))) {
                apply_custom_declarations(local, rule->declarations, rule->specificity, rule->source_order);
            }
        }
    }
    if (node_has_inline_custom_property(node)) {
        CssSpecificity inline_specificity;
        inline_specificity.ids = 1;
        apply_custom_declarations(local,
                                  parse_inline_style(node.attribute("style"),
                                                     options_.max_inline_style_bytes,
                                                     options_.max_inline_declarations,
                                                     options_.diagnostics),
                                  inline_specificity,
                                  static_cast<std::size_t>(-1));
    }
    bool applied = false;
    for (const auto& entry : local) {
        if (entry.second.set) {
            inherited[entry.first] = entry.second.value;
            applied = true;
        }
    }
    return applied;
}

CustomPropertyMap StyleResolver::custom_properties_for(const Node& node) const {
    CustomPropertyMap inherited;

    std::vector<const Node*> path;
    bool has_inline_custom_property = false;
    for (const Node* current = &node; current != nullptr; current = current->parent) {
        path.push_back(current);
        if (node_has_inline_custom_property(*current)) {
            has_inline_custom_property = true;
        }
    }
    if (!has_custom_property_declarations_ && !has_inline_custom_property) {
        return inherited;
    }
    std::reverse(path.begin(), path.end());

    for (const Node* current : path) {
        apply_custom_properties_for_node(inherited, *current);
    }
    return inherited;
}

Style StyleResolver::resolve(const Node& node) const {
    const CustomPropertyMap custom_properties = custom_properties_for(node);
    return resolve_with_custom_properties(node, custom_properties);
}

void StyleResolveContext::clear() {
    custom_property_cache.clear();
    custom_property_scopes.clear();
    matched_rule_cache.clear();
    resolver = nullptr;
    document_root = nullptr;
    document_mutation_generation = 0;
    interaction_state_generation = 0;
    custom_property_scan_root = nullptr;
    has_inline_custom_properties = false;
}

void StyleResolver::prepare_context(StyleResolveContext& context, const Node& node) const {
    const Node* root = &node;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    if (context.resolver != this ||
        context.document_root != root ||
        context.document_mutation_generation != root->mutation_generation ||
        context.interaction_state_generation != interaction_state_generation_) {
        context.clear();
        context.resolver = this;
        context.document_root = root;
        context.document_mutation_generation = root->mutation_generation;
        context.interaction_state_generation = interaction_state_generation_;
    }
}

const CustomPropertyMap& StyleResolver::custom_properties_for(const Node& node, StyleResolveContext& context) const {
    const auto existing = context.custom_property_cache.find(&node);
    if (existing != context.custom_property_cache.end()) {
        return *existing->second;
    }
    if (!has_custom_property_declarations_) {
        const Node* root = &node;
        while (root->parent != nullptr) {
            root = root->parent;
        }
        if (context.custom_property_scan_root != root) {
            context.custom_property_scan_root = root;
            context.has_inline_custom_properties = subtree_has_inline_custom_property(*root);
        }
        if (!context.has_inline_custom_properties) {
            static const CustomPropertyMap kEmptyCustomProperties;
            return kEmptyCustomProperties;
        }
    }
    static const CustomPropertyMap kEmptyCustomProperties;
    const CustomPropertyMap* inherited = &kEmptyCustomProperties;
    if (node.parent != nullptr) {
        inherited = &custom_properties_for(*node.parent, context);
    }
    const CustomPropertyMap* resolved = inherited;
    if (has_custom_property_declarations_ || node_has_inline_custom_property(node)) {
        CustomPropertyMap local;
        const std::vector<const CssRule*>* matched_rules = has_custom_property_declarations_
            ? &matching_rules_for(node, context)
            : nullptr;
        if (apply_custom_properties_for_node(local, node, matched_rules)) {
            CustomPropertyMap scope = *inherited;
            for (const auto& entry : local) {
                scope[entry.first] = entry.second;
            }
            context.custom_property_scopes.push_back(
                std::make_unique<CustomPropertyMap>(std::move(scope)));
            resolved = context.custom_property_scopes.back().get();
        }
    }
    auto inserted = context.custom_property_cache.emplace(&node, resolved);
    return *inserted.first->second;
}

Style StyleResolver::resolve(const Node& node, StyleResolveContext& context) const {
    prepare_context(context, node);
    const CustomPropertyMap& custom_properties = custom_properties_for(node, context);
    const std::vector<const CssRule*>* matched_rules = has_custom_property_declarations_
        ? &matching_rules_for(node, context)
        : nullptr;
    return resolve_with_custom_properties(node, custom_properties, matched_rules);
}

const std::vector<const CssRule*>& StyleResolver::matching_rules_for(const Node& node,
                                                                      StyleResolveContext& context) const {
    const auto existing = context.matched_rule_cache.find(&node);
    if (existing != context.matched_rule_cache.end()) {
        return existing->second;
    }
    const SelectorMatchContext match_context = selector_match_context_from_options(options_);
    std::vector<const CssRule*> matches;
    for (const CssRule* rule : candidate_rules_for(node)) {
        if (matches_rule(node, *rule, match_context)) {
            matches.push_back(rule);
        }
    }
    return context.matched_rule_cache.emplace(&node, std::move(matches)).first->second;
}

Style StyleResolver::resolve_with_custom_properties(const Node& node,
                                                    const CustomPropertyMap& custom_properties,
                                                    const std::vector<const CssRule*>* matched_rules) const {
    Style style = default_style_for(node);
    CascadeSlots slots;
    const SelectorMatchContext context = selector_match_context_from_options(options_);

    const std::vector<const CssRule*>& rules = matched_rules != nullptr
        ? *matched_rules
        : candidate_rules_for(node);
    for (const CssRule* rule : rules) {
        if (matched_rules == nullptr && !matches_rule(node, *rule, context)) {
            continue;
        }
        apply_declarations(style, slots, rule->declarations, rule->specificity,
                           rule->source_order, rule->pseudo_element, custom_properties,
                           options_.diagnostics, this, options_.max_resolved_value_bytes);
    }
    if (node.type == NodeType::Element) {
        CssSpecificity inline_specificity;
        inline_specificity.ids = 1;
        inline_specificity.classes = 0;
        inline_specificity.elements = 0;
        apply_declarations(style, slots,
                           parse_inline_style(node.attribute("style"),
                                              options_.max_inline_style_bytes,
                                              options_.max_inline_declarations,
                                              options_.diagnostics),
                           inline_specificity,
                           static_cast<std::size_t>(-1), CssPseudoElement::None, custom_properties,
                           options_.diagnostics, this, options_.max_resolved_value_bytes);
    }
    return style;
}

const CssKeyframesRule* StyleResolver::keyframes(std::string_view name) const {
    return stylesheet_.find_keyframes(name);
}

std::uint16_t StyleResolver::background_image_resource_id_for(std::string_view url) const {
    for (std::size_t index = 0; index < background_image_resources_.size(); ++index) {
        const std::string& existing = background_image_resources_[index];
        if (existing.size() == url.size() && existing.compare(0, existing.size(), url.data(), url.size()) == 0) {
            return static_cast<std::uint16_t>(index + 1);
        }
    }
    const std::size_t limit = std::min<std::size_t>(options_.max_background_image_resources, 0xFFFFU);
    if (limit == 0 || background_image_resources_.size() >= limit) {
        return 0;
    }
    background_image_resources_.emplace_back(url);
    return static_cast<std::uint16_t>(background_image_resources_.size());
}

const std::string* StyleResolver::background_image_resource_url(std::uint16_t resource_id) const {
    if (resource_id == 0 || resource_id > background_image_resources_.size()) {
        return nullptr;
    }
    return &background_image_resources_[resource_id - 1];
}

std::size_t StyleResolver::background_image_resource_count() const {
    return background_image_resources_.size();
}

StyleResolverStatistics StyleResolver::statistics() const {
    StyleResolverStatistics snapshot = statistics_;
    snapshot.candidate_cache_entries = candidate_cache_.size();
    return snapshot;
}

InteractionInvalidationHints StyleResolver::interaction_invalidation_hints() const {
    return interaction_hints_;
}

void StyleResolver::set_interaction_state(const Node* hovered_node,
                                          const Node* active_node,
                                          const Node* focused_node) {
    if (options_.hovered_node != hovered_node ||
        options_.active_node != active_node ||
        options_.focused_node != focused_node) {
        ++interaction_state_generation_;
    }
    options_.hovered_node = hovered_node;
    options_.active_node = active_node;
    options_.focused_node = focused_node;
}

} // namespace jellyframe
