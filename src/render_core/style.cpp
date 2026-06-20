#include "render_core/style.h"

#include "render_core/form_control.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jellyframe {
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
constexpr int kDefaultViewportWidthPx = 360;
constexpr int kDefaultViewportHeightPx = 240;

std::string lowercase(std::string value);
bool parse_length_px(const std::string& raw_value, int& output, int em_base = kRootFontSizePx);

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
        output = op_char == '-' ? left - right : left + right;
        return true;
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

bool parse_time_ms(const std::string& raw_value, std::uint32_t& output) {
    const std::string value = lowercase(trim(raw_value));
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || parsed < 0.0F) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    float milliseconds = parsed;
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
    output = std::max(0, std::min(1000000, static_cast<int>(parsed * 1000.0F + 0.5F)));
    return true;
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

bool parse_timing_function(const std::string& raw_value, AnimationTimingFunction& output) {
    const std::string value = lowercase(trim(raw_value));
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
    return false;
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

void set_style_transitions(Style& style, const std::vector<StyleTransition>& transitions) {
    style.transition_count = std::min<std::size_t>(style.transitions.size(), transitions.size());
    for (std::size_t index = 0; index < style.transition_count; ++index) {
        style.transitions[index] = transitions[index];
    }
}

void set_style_animations(Style& style, const std::vector<StyleAnimation>& animations) {
    style.animation_count = std::min<std::size_t>(style.animations.size(), animations.size());
    for (std::size_t index = 0; index < style.animation_count; ++index) {
        style.animations[index] = animations[index];
    }
}

bool parse_transition_shorthand(const std::string& raw_value, Style& style) {
    const std::string lowered = lowercase(trim(raw_value));
    if (lowered == "none") {
        style.transition_count = 0;
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
            if (!have_timing && parse_timing_function(token, timing)) {
                transition.timing = timing;
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
        style.animation_count = 0;
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
        for (const std::string& token : split_whitespace_components(component)) {
            AnimationTimingFunction timing = AnimationTimingFunction::Ease;
            if (!have_timing && parse_timing_function(token, timing)) {
                animation.timing = timing;
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
    if (style.animation_count == 0) {
        StyleAnimation animation;
        style.animations[0] = std::move(animation);
        style.animation_count = 1;
    }
}

bool parse_animation_longhand(const std::string& property, const std::string& raw_value, Style& style) {
    if (property == "animation-name") {
        const std::string lowered = lowercase(trim(raw_value));
        if (lowered == "none") {
            style.animation_count = 0;
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
        const std::size_t old_count = style.animation_count;
        style.animation_count = std::min<std::size_t>(style.animations.size(), std::max(old_count, names.size()));
        for (std::size_t index = 0; index < style.animation_count; ++index) {
            style.animations[index].name = names[std::min(index, names.size() - 1)];
        }
        return true;
    }

    ensure_animation_entries(style);
    const std::vector<std::string> values = split_comma_components(raw_value);
    if (values.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < style.animation_count; ++index) {
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
            if (!parse_timing_function(value, timing)) {
                return false;
            }
            style.animations[index].timing = timing;
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

    if (style.transition_count == 0) {
        StyleTransition transition;
        transition.property = AnimatableProperty::All;
        style.transitions[0] = transition;
        style.transition_count = 1;
    }
    const std::vector<std::string> values = split_comma_components(raw_value);
    if (values.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < style.transition_count; ++index) {
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
            if (!parse_timing_function(value, timing)) {
                return false;
            }
            style.transitions[index].timing = timing;
        }
    }
    std::size_t output = 0;
    for (std::size_t index = 0; index < style.transition_count; ++index) {
        if (style.transitions[index].duration_ms > 0) {
            style.transitions[output++] = style.transitions[index];
        }
    }
    style.transition_count = output;
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
    return false;
}

bool parse_linear_gradient_background(const std::string& raw_value, Color& top, Color& bottom) {
    const std::string value = lowercase(trim(raw_value));
    constexpr std::string_view prefix = "linear-gradient(";
    if (value.rfind(prefix, 0) != 0 || value.back() != ')') {
        return false;
    }

    std::vector<std::string> args =
        split_function_arguments(std::string_view(value).substr(prefix.size(), value.size() - prefix.size() - 1));
    if (args.size() == 3) {
        const std::string direction = trim(args[0]);
        if (direction == "to bottom") {
            args.erase(args.begin());
        } else if (direction == "to top") {
            args.erase(args.begin());
            std::swap(args[0], args[1]);
        } else {
            return false;
        }
    }
    if (args.size() != 2) {
        return false;
    }
    return parse_color(args[0], top) && parse_color(args[1], bottom);
}

bool parse_background_paint(const std::string& value, BackgroundPaintKind& kind, Color& color, Color& color2) {
    Color top;
    Color bottom;
    if (parse_linear_gradient_background(value, top, bottom)) {
        kind = BackgroundPaintKind::LinearGradient;
        color = top;
        color2 = bottom;
        return true;
    }
    if (parse_color(value, color)) {
        kind = BackgroundPaintKind::Solid;
        color2 = color;
        return true;
    }
    return false;
}

bool parse_number_or_length_for_transform(const std::string& value, float& output, int em_base = kRootFontSizePx) {
    int px = 0;
    if (parse_length_px(value, px, em_base)) {
        output = static_cast<float>(px);
        return true;
    }
    return parse_float(value, output);
}

bool parse_scale_value(const std::string& value, float& output) {
    if (!parse_float(value, output)) {
        return false;
    }
    output = std::max(0.01F, std::min(16.0F, output));
    return true;
}

bool parse_transform_function(std::string_view function, Transform2D& output) {
    const std::size_t open = function.find('(');
    if (open == std::string_view::npos || function.empty() || function.back() != ')') {
        return false;
    }
    const std::string name = lowercase(trim(function.substr(0, open)));
    const std::vector<std::string> args =
        split_function_arguments(function.substr(open + 1, function.size() - open - 2));
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
        output.translate_x += x;
        output.translate_y += y;
        return true;
    }
    if (name == "translatex") {
        float x = 0.0F;
        if (args.size() != 1 || !parse_number_or_length_for_transform(args[0], x)) {
            return false;
        }
        output.translate_x += x;
        return true;
    }
    if (name == "translatey") {
        float y = 0.0F;
        if (args.size() != 1 || !parse_number_or_length_for_transform(args[0], y)) {
            return false;
        }
        output.translate_y += y;
        return true;
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
        output.scale_x *= x;
        output.scale_y *= y;
        return true;
    }
    if (name == "scalex") {
        float x = 1.0F;
        if (args.size() != 1 || !parse_scale_value(args[0], x)) {
            return false;
        }
        output.scale_x *= x;
        return true;
    }
    if (name == "scaley") {
        float y = 1.0F;
        if (args.size() != 1 || !parse_scale_value(args[0], y)) {
            return false;
        }
        output.scale_y *= y;
        return true;
    }
    return false;
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
    Color,
    Background,
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
    AspectRatio,
    FontSize,
    FontWeight,
    LineHeight,
    TextIndent,
    BoxShadow,
    Overflow,
    Opacity,
    Transform,
    Position,
    Top,
    Right,
    Bottom,
    Left,
    ZIndex,
    TextAlign,
    JustifyContent,
    AlignItems,
    BoxSizing,
    Flex,
    FlexGrow,
    FlexShrink,
    FlexBasis,
    FlexWrap,
    Gap,
    ColumnGap,
    RowGap,
    GridTemplateColumns,
    GridAutoRows,
    GridColumn,
    GridRow,
    ObjectFit,
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
    BeforeContent,
    BeforeColor,
    BeforeFontWeight,
    BeforeLeft,
    Count,
};

struct CascadeSlots {
    std::array<CascadeSlot, static_cast<std::size_t>(CascadeProperty::Count)> slots;
};

using CustomPropertyMap = std::unordered_map<std::string, std::string>;

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
    if (property == "display") {
        return &cascade_slot(slots, CascadeProperty::Display);
    }
    if (property == "color") {
        return &cascade_slot(slots, CascadeProperty::Color);
    }
    if (property == "background" || property == "background-color") {
        return &cascade_slot(slots, CascadeProperty::Background);
    }
    if (property == "margin-top") {
        return &cascade_slot(slots, CascadeProperty::MarginTop);
    }
    if (property == "margin-right") {
        return &cascade_slot(slots, CascadeProperty::MarginRight);
    }
    if (property == "margin-bottom") {
        return &cascade_slot(slots, CascadeProperty::MarginBottom);
    }
    if (property == "margin-left") {
        return &cascade_slot(slots, CascadeProperty::MarginLeft);
    }
    if (property == "padding-top") {
        return &cascade_slot(slots, CascadeProperty::PaddingTop);
    }
    if (property == "padding-right") {
        return &cascade_slot(slots, CascadeProperty::PaddingRight);
    }
    if (property == "padding-bottom") {
        return &cascade_slot(slots, CascadeProperty::PaddingBottom);
    }
    if (property == "padding-left") {
        return &cascade_slot(slots, CascadeProperty::PaddingLeft);
    }
    if (property == "border-top-width") {
        return &cascade_slot(slots, CascadeProperty::BorderTopWidth);
    }
    if (property == "border-right-width") {
        return &cascade_slot(slots, CascadeProperty::BorderRightWidth);
    }
    if (property == "border-bottom-width") {
        return &cascade_slot(slots, CascadeProperty::BorderBottomWidth);
    }
    if (property == "border-left-width") {
        return &cascade_slot(slots, CascadeProperty::BorderLeftWidth);
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
    if (property == "font-weight") {
        return &cascade_slot(slots, CascadeProperty::FontWeight);
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
    if (property == "top") {
        return &cascade_slot(slots, CascadeProperty::Top);
    }
    if (property == "right") {
        return &cascade_slot(slots, CascadeProperty::Right);
    }
    if (property == "bottom") {
        return &cascade_slot(slots, CascadeProperty::Bottom);
    }
    if (property == "left") {
        return &cascade_slot(slots, CascadeProperty::Left);
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
    if (property == "flex") {
        return &cascade_slot(slots, CascadeProperty::Flex);
    }
    if (property == "flex-grow") {
        return &cascade_slot(slots, CascadeProperty::FlexGrow);
    }
    if (property == "flex-shrink") {
        return &cascade_slot(slots, CascadeProperty::FlexShrink);
    }
    if (property == "flex-basis") {
        return &cascade_slot(slots, CascadeProperty::FlexBasis);
    }
    if (property == "flex-wrap") {
        return &cascade_slot(slots, CascadeProperty::FlexWrap);
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
    if (property == "object-fit") {
        return &cascade_slot(slots, CascadeProperty::ObjectFit);
    }
    if (property == "list-style" || property == "list-style-type") {
        return &cascade_slot(slots, CascadeProperty::ListStyleType);
    }
    if (property == "transition") {
        return &cascade_slot(slots, CascadeProperty::Transition);
    }
    if (property == "transition-property") {
        return &cascade_slot(slots, CascadeProperty::TransitionProperty);
    }
    if (property == "transition-duration") {
        return &cascade_slot(slots, CascadeProperty::TransitionDuration);
    }
    if (property == "transition-delay") {
        return &cascade_slot(slots, CascadeProperty::TransitionDelay);
    }
    if (property == "transition-timing-function") {
        return &cascade_slot(slots, CascadeProperty::TransitionTimingFunction);
    }
    if (property == "animation") {
        return &cascade_slot(slots, CascadeProperty::Animation);
    }
    if (property == "animation-name") {
        return &cascade_slot(slots, CascadeProperty::AnimationName);
    }
    if (property == "animation-duration") {
        return &cascade_slot(slots, CascadeProperty::AnimationDuration);
    }
    if (property == "animation-delay") {
        return &cascade_slot(slots, CascadeProperty::AnimationDelay);
    }
    if (property == "animation-timing-function") {
        return &cascade_slot(slots, CascadeProperty::AnimationTimingFunction);
    }
    if (property == "animation-iteration-count") {
        return &cascade_slot(slots, CascadeProperty::AnimationIterationCount);
    }
    if (property == "animation-direction") {
        return &cascade_slot(slots, CascadeProperty::AnimationDirection);
    }
    return nullptr;
}

CascadeSlot* cascade_slot_for_before_property(CascadeSlots& slots, const std::string& property) {
    if (property == "content") {
        return &cascade_slot(slots, CascadeProperty::BeforeContent);
    }
    if (property == "color") {
        return &cascade_slot(slots, CascadeProperty::BeforeColor);
    }
    if (property == "font-weight") {
        return &cascade_slot(slots, CascadeProperty::BeforeFontWeight);
    }
    if (property == "left") {
        return &cascade_slot(slots, CascadeProperty::BeforeLeft);
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
    } else if (property == "background-color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.background_paint = BackgroundPaintKind::Solid;
        style.background_color = parsed;
        style.background_color2 = parsed;
        return true;
    } else if (property == "background") {
        BackgroundPaintKind kind = BackgroundPaintKind::Solid;
        Color color;
        Color color2;
        if (!parse_background_paint(value, kind, color, color2)) {
            return false;
        }
        style.background_paint = kind;
        style.background_color = color;
        style.background_color2 = color2;
        return true;
    } else if (property == "margin") {
        return parse_margin_edge_px(value, style.margin, style.margin_left_auto, style.margin_right_auto, style.font_size);
    } else if (property == "margin-top" || property == "margin-right" ||
               property == "margin-bottom" || property == "margin-left") {
        int px = 0;
        bool is_auto = false;
        if (!parse_margin_side_px(value, px, is_auto, style.font_size)) {
            return false;
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
        return true;
    } else if (property == "padding") {
        return parse_box_edge_px(value, style.padding, style.font_size);
    } else if (property == "padding-top" || property == "padding-right" ||
               property == "padding-bottom" || property == "padding-left") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
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
        return true;
    } else if (property == "border-width") {
        return parse_box_edge_px(value, style.border_width, style.font_size);
    } else if (property == "border-top-width" || property == "border-right-width" ||
               property == "border-bottom-width" || property == "border-left-width") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
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
        return true;
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
    } else if (property == "font-weight") {
        int weight = 400;
        if (!parse_font_weight(value, weight)) {
            return false;
        }
        style.font_weight = weight;
        style.font_weight_specified = true;
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
    } else if (property == "flex-wrap") {
        const std::string lowered = lowercase(trim(value));
        if (lowered == "wrap" || lowered == "wrap-reverse") {
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
        return true;
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
               property == "animation-direction") {
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
                      int depth = 0) {
    constexpr int kMaxVarDepth = 8;
    if (depth > kMaxVarDepth) {
        return false;
    }

    if (value.find("var(") == std::string_view::npos) {
        output = std::string(value);
        return true;
    }

    output.clear();
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const std::size_t start = value.find("var(", cursor);
        if (start == std::string_view::npos) {
            output.append(value.substr(cursor));
            return true;
        }
        output.append(value.substr(cursor, start - cursor));
        const std::size_t close = find_matching_paren(value, start + 3);
        if (close == std::string_view::npos) {
            output.append(value.substr(start));
            return false;
        }

        const std::string_view body = value.substr(start + 4, close - start - 4);
        const std::vector<std::string> args = split_function_arguments(body);
        if (args.empty()) {
            output.append(value.substr(start, close - start + 1));
            return false;
        }
        const std::string name = lowercase(trim(args[0]));
        std::string replacement;
        const auto found = custom_properties.find(name);
        if (found != custom_properties.end()) {
            if (!resolve_css_vars(found->second, custom_properties, replacement, depth + 1)) {
                return false;
            }
        } else if (args.size() >= 2) {
            const std::size_t fallback_begin = body.find(',');
            if (fallback_begin == std::string_view::npos ||
                !resolve_css_vars(body.substr(fallback_begin + 1), custom_properties, replacement, depth + 1)) {
                return false;
            }
            replacement = trim(replacement);
        } else {
            output.append(value.substr(start, close - start + 1));
            return false;
        }
        output.append(replacement);
        cursor = close + 1;
    }
    return true;
}

CssDeclaration resolve_declaration_value(const CssDeclaration& declaration,
                                         const CustomPropertyMap& custom_properties) {
    if (declaration.value.find("var(") == std::string::npos) {
        return declaration;
    }
    CssDeclaration resolved = declaration;
    std::string value;
    if (resolve_css_vars(declaration.value, custom_properties, value)) {
        resolved.value = trim(value);
    }
    return resolved;
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
        const std::string lowered = lowercase(trim(declaration.value));
        int width = 0;
        Color color;
        bool has_width = false;
        bool has_color = false;
        if (lowered == "none" || lowered == "0" || lowered == "0px") {
            has_width = true;
        } else {
            std::size_t index = 0;
            while (index < declaration.value.size()) {
                while (index < declaration.value.size() &&
                       std::isspace(static_cast<unsigned char>(declaration.value[index])) != 0) {
                    ++index;
                }
                const std::size_t begin = index;
                while (index < declaration.value.size() &&
                       std::isspace(static_cast<unsigned char>(declaration.value[index])) == 0) {
                    ++index;
                }
                const std::string token = declaration.value.substr(begin, index - begin);
                if (!token.empty() && !has_width && parse_length_px(token, width, style.font_size)) {
                    has_width = true;
                } else if (!token.empty() && !has_color && parse_color(token, color)) {
                    has_color = true;
                }
            }
        }
        if (!has_width && !has_color) {
            return true;
        }
        if (has_width) {
            apply_edge_value(slots, CascadeProperty::BorderTopWidth, style.border_width, width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderRightWidth, style.border_width, width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderBottomWidth, style.border_width, width,
                             declaration, specificity, source_order);
            apply_edge_value(slots, CascadeProperty::BorderLeftWidth, style.border_width, width,
                             declaration, specificity, source_order);
        }
        if (has_color) {
            CascadeSlot& slot = cascade_slot(slots, CascadeProperty::BorderColor);
            if (declaration_wins(slot, declaration, specificity, source_order)) {
                style.border_color = color;
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
                                std::size_t source_order) {
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return true;
    }
    if (apply_declaration(style, declaration.property, declaration.value)) {
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

bool apply_before_declaration(Style& style, const std::string& property, const std::string& value) {
    if (property == "content") {
        std::string counter_name;
        std::string suffix;
        if (parse_counter_content(value, counter_name, suffix)) {
            style.before_content_kind = GeneratedContentKind::Counter;
            style.before_counter_name = std::move(counter_name);
            style.before_counter_suffix = std::move(suffix);
            style.before_content_text.clear();
            return true;
        }
        const std::string text = unquote(value);
        if (text.empty() || text == "none" || text == "normal") {
            style.before_content_kind = GeneratedContentKind::None;
            style.before_content_text.clear();
            style.before_counter_name.clear();
            style.before_counter_suffix.clear();
            return true;
        }
        style.before_content_kind = GeneratedContentKind::Text;
        style.before_content_text = text;
        style.before_counter_name.clear();
        style.before_counter_suffix.clear();
        return true;
    }
    if (property == "color") {
        Color parsed;
        if (!parse_color(value, parsed)) {
            return false;
        }
        style.before_color = parsed;
        style.before_color_specified = true;
        return true;
    }
    if (property == "font-weight") {
        int weight = 400;
        if (!parse_font_weight(value, weight)) {
            return false;
        }
        style.before_font_weight = weight;
        style.before_font_weight_specified = true;
        return true;
    }
    if (property == "left") {
        int px = 0;
        if (!parse_length_px(value, px, style.font_size)) {
            return false;
        }
        style.before_left = px;
        style.before_left_specified = true;
        return true;
    }
    return false;
}

bool apply_cascaded_before_declaration(Style& style,
                                       CascadeSlot& slot,
                                       const CssDeclaration& declaration,
                                       const CssSpecificity& specificity,
                                       std::size_t source_order) {
    if (!declaration_wins(slot, declaration, specificity, source_order)) {
        return true;
    }
    if (apply_before_declaration(style, declaration.property, declaration.value)) {
        mark_slot(slot, declaration, specificity, source_order);
        return true;
    }
    return false;
}

void apply_declarations(Style& style,
                        CascadeSlots& slots,
                        const std::vector<CssDeclaration>& declarations,
                        const CssSpecificity& specificity,
                        std::size_t source_order,
                        bool pseudo_before,
                        const CustomPropertyMap& custom_properties,
                        DiagnosticSink* diagnostics) {
    for (const CssDeclaration& declaration : declarations) {
        if (is_custom_property_name(declaration.property)) {
            continue;
        }
        const CssDeclaration resolved_declaration = resolve_declaration_value(declaration, custom_properties);
        if (pseudo_before) {
            CascadeSlot* slot = cascade_slot_for_before_property(slots, resolved_declaration.property);
            if (slot != nullptr) {
                if (!apply_cascaded_before_declaration(style, *slot, resolved_declaration, specificity, source_order)) {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      "style-before-declaration-ignored",
                                      "Pseudo-element declaration could not be applied",
                                      resolved_declaration.property + ": " + resolved_declaration.value);
                }
            } else {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::Style,
                                  DiagnosticSeverity::Info,
                                  "style-before-property-unsupported",
                                  "Pseudo-element property is outside the supported subset",
                                  resolved_declaration.property);
            }
            continue;
        }
        if (apply_edge_shorthand(style, slots, resolved_declaration, specificity, source_order)) {
            continue;
        }
        CascadeSlot* slot = cascade_slot_for_property(slots, resolved_declaration.property);
        if (slot != nullptr) {
            if (!apply_cascaded_declaration(style, *slot, resolved_declaration, specificity, source_order)) {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::Style,
                                  DiagnosticSeverity::Warning,
                                  "style-declaration-ignored",
                                  "CSS declaration could not be applied by the supported style subset",
                                  resolved_declaration.property + ": " + resolved_declaration.value);
            }
        } else {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Style,
                              DiagnosticSeverity::Info,
                              "style-property-unsupported",
                              "CSS property is outside the supported subset and was ignored",
                              resolved_declaration.property);
        }
    }
}

std::vector<CssDeclaration> parse_inline_style(const std::string& source, DiagnosticSink* diagnostics = nullptr) {
    std::vector<CssDeclaration> declarations;
    std::size_t index = 0;
    while (index < source.size()) {
        const std::size_t colon = source.find(':', index);
        if (colon == std::string::npos) {
            const std::string remaining = trim(std::string_view(source).substr(index));
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
        const std::size_t end = semicolon == std::string::npos ? source.size() : semicolon;
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
        std::abs(transform.scale_y - 1.0F) < 0.001F) {
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
}

void StyleResolver::build_rule_index() {
    for (const CssRule& rule : stylesheet_) {
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
        key.reserve(node.tag_name.size() + node.attribute("id").size() + node.attribute("class").size() + 8);
        key.append(node.tag_name);
        key.push_back('\n');
        key.append(node.attribute("id"));
        key.push_back('\n');
        key.append(node.attribute("class"));
    } else {
        key = "#text";
    }

    const auto cached = candidate_cache_.find(key);
    if (cached != candidate_cache_.end()) {
        ++statistics_.candidate_cache_hits;
        return cached->second;
    }
    ++statistics_.candidate_cache_misses;

    if (options_.max_candidate_cache_entries == 0 ||
        candidate_cache_.size() >= options_.max_candidate_cache_entries) {
        if (!candidate_cache_.empty()) {
            ++statistics_.candidate_cache_clears;
        }
        candidate_cache_.clear();
        statistics_.candidate_cache_entries = 0;
        statistics_.candidate_cache_rule_refs = 0;
    }

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

CustomPropertyMap StyleResolver::custom_properties_for(const Node& node) const {
    CustomPropertyMap inherited;
    const SelectorMatchContext context = selector_match_context_from_options(options_);

    std::vector<const Node*> path;
    bool has_inline_custom_property = false;
    for (const Node* current = &node; current != nullptr; current = current->parent) {
        path.push_back(current);
        if (current->type == NodeType::Element && current->attribute("style").find("--") != std::string::npos) {
            has_inline_custom_property = true;
        }
    }
    if (!has_custom_property_declarations_ && !has_inline_custom_property) {
        return inherited;
    }
    std::reverse(path.begin(), path.end());

    for (const Node* current : path) {
        if (current->type != NodeType::Element) {
            continue;
        }
        CustomPropertySlots local;
        for (const CssRule* rule : candidate_rules_for(*current)) {
            if (!rule->pseudo_before && matches_rule(*current, *rule, context)) {
                apply_custom_declarations(local, rule->declarations, rule->specificity, rule->source_order);
            }
        }
        CssSpecificity inline_specificity;
        inline_specificity.ids = 1;
        apply_custom_declarations(local,
                                  parse_inline_style(current->attribute("style"), options_.diagnostics),
                                  inline_specificity,
                                  static_cast<std::size_t>(-1));
        for (const auto& entry : local) {
            if (entry.second.set) {
                inherited[entry.first] = entry.second.value;
            }
        }
    }
    return inherited;
}

Style StyleResolver::resolve(const Node& node) const {
    Style style = default_style_for(node);
    CascadeSlots slots;
    const SelectorMatchContext context = selector_match_context_from_options(options_);
    CustomPropertyMap custom_properties = custom_properties_for(node);

    for (const CssRule* rule : candidate_rules_for(node)) {
        if (matches_rule(node, *rule, context)) {
            apply_declarations(style, slots, rule->declarations, rule->specificity,
                               rule->source_order, rule->pseudo_before, custom_properties,
                               options_.diagnostics);
        }
    }
    if (node.type == NodeType::Element) {
        CssSpecificity inline_specificity;
        inline_specificity.ids = 1;
        inline_specificity.classes = 0;
        inline_specificity.elements = 0;
        apply_declarations(style, slots, parse_inline_style(node.attribute("style"), options_.diagnostics), inline_specificity,
                           static_cast<std::size_t>(-1), false, custom_properties,
                           options_.diagnostics);
    }
    return style;
}

const CssKeyframesRule* StyleResolver::keyframes(std::string_view name) const {
    return stylesheet_.find_keyframes(name);
}

StyleResolverStatistics StyleResolver::statistics() const {
    StyleResolverStatistics snapshot = statistics_;
    snapshot.candidate_cache_entries = candidate_cache_.size();
    return snapshot;
}

void StyleResolver::set_interaction_state(const Node* hovered_node,
                                          const Node* active_node,
                                          const Node* focused_node) {
    options_.hovered_node = hovered_node;
    options_.active_node = active_node;
    options_.focused_node = focused_node;
}

} // namespace jellyframe
