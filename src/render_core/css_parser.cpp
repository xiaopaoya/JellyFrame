#include "render_core/css_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace jellyframe {
namespace {

bool is_ascii_space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool is_name_char(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) || ch == '-' || ch == '_' || ch == ':' || ch == '.';
}

char ascii_lower(char ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string ascii_lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && is_ascii_space(value[begin])) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && is_ascii_space(value[end - 1])) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string collapse_ascii_space(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    bool last_was_space = false;
    for (const char ch : value) {
        if (is_ascii_space(ch)) {
            if (!last_was_space && !output.empty()) {
                output.push_back(' ');
            }
            last_was_space = true;
        } else {
            output.push_back(ch);
            last_was_space = false;
        }
    }
    if (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
    return output;
}

bool contains_ascii_case_insensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (ascii_lower(haystack[i + j]) != ascii_lower(needle[j])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> split_top_level_commas(std::string_view value) {
    std::vector<std::string> items;
    std::size_t begin = 0;
    int paren_depth = 0;
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
            ++paren_depth;
        } else if (ch == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (ch == ',' && paren_depth == 0) {
            items.push_back(trim(value.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    items.push_back(trim(value.substr(begin)));
    return items;
}

bool parse_media_length_px(std::string_view value, int& output) {
    const std::string text = trim(value);
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || errno == ERANGE) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end != nullptr && std::strncmp(end, "px", 2) == 0) {
        end += 2;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    output = static_cast<int>(parsed >= 0.0F ? parsed + 0.5F : parsed - 0.5F);
    return output >= 0;
}

bool evaluate_media_feature(std::string_view condition, const CssParserOptions& options) {
    const std::string text = ascii_lowercase(collapse_ascii_space(condition));
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    const std::string feature = trim(std::string_view(text).substr(0, colon));
    int expected = 0;
    if (!parse_media_length_px(std::string_view(text).substr(colon + 1), expected)) {
        return false;
    }

    if (feature == "min-width") {
        return options.media_viewport_width >= expected;
    }
    if (feature == "max-width") {
        return options.media_viewport_width <= expected;
    }
    if (feature == "min-height") {
        return options.media_viewport_height >= expected;
    }
    if (feature == "max-height") {
        return options.media_viewport_height <= expected;
    }
    return false;
}

bool consume_media_condition(std::string_view& rest, const CssParserOptions& options) {
    rest = std::string_view(rest).substr(std::min(rest.find_first_not_of(' '), rest.size()));
    if (rest.empty() || rest.front() != '(') {
        return false;
    }
    int depth = 0;
    for (std::size_t index = 0; index < rest.size(); ++index) {
        const char ch = rest[index];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) {
                const std::string_view condition = rest.substr(1, index - 1);
                rest = rest.substr(index + 1);
                return evaluate_media_feature(condition, options);
            }
        }
    }
    return false;
}

bool starts_with_token(std::string_view value, std::string_view token) {
    return value.size() >= token.size() && value.substr(0, token.size()) == token &&
           (value.size() == token.size() || value[token.size()] == ' ');
}

bool evaluate_media_query_item(std::string_view item, const CssParserOptions& options) {
    std::string media = ascii_lowercase(collapse_ascii_space(item));
    if (media.empty() || media == "all" || media == "screen") {
        return true;
    }
    if (starts_with_token(media, "not")) {
        return false;
    }
    if (starts_with_token(media, "only")) {
        media = trim(std::string_view(media).substr(4));
    }

    std::string_view rest(media);
    if (!rest.empty() && rest.front() != '(') {
        const std::size_t space = rest.find(' ');
        const std::string_view type = space == std::string_view::npos ? rest : rest.substr(0, space);
        if (type != "all" && type != "screen") {
            return false;
        }
        if (space == std::string_view::npos) {
            return true;
        }
        rest = rest.substr(space + 1);
        rest = rest.substr(std::min(rest.find_first_not_of(' '), rest.size()));
        if (!starts_with_token(rest, "and")) {
            return false;
        }
        rest = rest.substr(3);
    }

    bool saw_condition = false;
    while (true) {
        rest = rest.substr(std::min(rest.find_first_not_of(' '), rest.size()));
        if (rest.empty()) {
            return saw_condition;
        }
        if (!consume_media_condition(rest, options)) {
            return false;
        }
        saw_condition = true;
        rest = rest.substr(std::min(rest.find_first_not_of(' '), rest.size()));
        if (rest.empty()) {
            return true;
        }
        if (!starts_with_token(rest, "and")) {
            return false;
        }
        rest = rest.substr(3);
    }
}

bool is_supported_media_query(std::string_view prelude, const CssParserOptions& options) {
    const std::string media = ascii_lowercase(collapse_ascii_space(prelude));
    for (const std::string& item : split_top_level_commas(media)) {
        if (evaluate_media_query_item(item, options)) {
            return true;
        }
    }
    return false;
}

bool starts_with_word(std::string_view value, std::string_view word) {
    if (value.size() < word.size() || value.substr(0, word.size()) != word) {
        return false;
    }
    return value.size() == word.size() || is_ascii_space(value[word.size()]) || value[word.size()] == '(';
}

std::string_view trim_view(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && is_ascii_space(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && is_ascii_space(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string_view consume_word(std::string_view& value) {
    value = trim_view(value);
    std::size_t end = 0;
    while (end < value.size() &&
           (std::isalnum(static_cast<unsigned char>(value[end])) != 0 || value[end] == '-')) {
        ++end;
    }
    const std::string_view word = value.substr(0, end);
    value = value.substr(end);
    return word;
}

bool split_supports_parentheses(std::string_view& rest, std::string_view& body) {
    rest = trim_view(rest);
    if (rest.empty() || rest.front() != '(') {
        return false;
    }
    int depth = 0;
    char quote = '\0';
    for (std::size_t index = 0; index < rest.size(); ++index) {
        const char ch = rest[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < rest.size()) {
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
                body = rest.substr(1, index - 1);
                rest = rest.substr(index + 1);
                return true;
            }
        }
    }
    return false;
}

std::size_t find_top_level_colon(std::string_view value) {
    int paren_depth = 0;
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
            ++paren_depth;
        } else if (ch == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (ch == ':' && paren_depth == 0) {
            return index;
        }
    }
    return std::string_view::npos;
}

bool is_number_with_suffix(std::string_view raw_value,
                           const std::initializer_list<std::string_view>& suffixes,
                           bool allow_unitless_zero = true) {
    const std::string value = ascii_lowercase(trim(raw_value));
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
    if (allow_unitless_zero && parsed == 0.0F && end != nullptr && *end == '\0') {
        return true;
    }
    for (std::string_view suffix : suffixes) {
        if (end != nullptr && std::strncmp(end, suffix.data(), suffix.size()) == 0) {
            end += suffix.size();
            while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
                ++end;
            }
            return end != nullptr && *end == '\0';
        }
    }
    return false;
}

bool is_supported_length_value(std::string_view value) {
    const std::string text = ascii_lowercase(trim(value));
    if (text.find("var(") != std::string::npos) {
        return true;
    }
    if (text.rfind("min(", 0) == 0 || text.rfind("max(", 0) == 0 ||
        text.rfind("clamp(", 0) == 0 || text.rfind("calc(", 0) == 0) {
        return true;
    }
    return is_number_with_suffix(text, {"px", "em", "rem", "vw", "vh", "%"});
}

bool is_supported_color_value(std::string_view value) {
    const std::string text = ascii_lowercase(trim(value));
    if (text.empty()) {
        return false;
    }
    if (text.find("var(") != std::string::npos) {
        return true;
    }
    if (text == "transparent" || text == "black" || text == "white" ||
        text == "red" || text == "green" || text == "blue") {
        return true;
    }
    if (text.size() == 4 || text.size() == 7 || text.size() == 9) {
        if (!text.empty() && text.front() == '#') {
            return std::all_of(text.begin() + 1, text.end(), [](char ch) {
                return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
            });
        }
    }
    return (text.rfind("rgb(", 0) == 0 || text.rfind("rgba(", 0) == 0) && text.back() == ')';
}

bool is_supported_background_value(std::string_view value) {
    const std::string text = ascii_lowercase(trim(value));
    if (is_supported_color_value(text)) {
        return true;
    }
    if (text.rfind("linear-gradient(", 0) != 0 || text.back() != ')') {
        return false;
    }
    std::vector<std::string> args =
        split_top_level_commas(std::string_view(text).substr(16, text.size() - 17));
    if (args.size() == 3) {
        const std::string direction = trim(args[0]);
        if (direction != "to bottom" && direction != "to top") {
            return false;
        }
        args.erase(args.begin());
    }
    return args.size() == 2 && is_supported_color_value(args[0]) && is_supported_color_value(args[1]);
}

bool supported_keyword(std::string_view value, const std::initializer_list<std::string_view>& keywords) {
    const std::string text = ascii_lowercase(trim(value));
    return std::find(keywords.begin(), keywords.end(), std::string_view(text)) != keywords.end();
}

bool is_positive_integer_value(std::string_view raw_value) {
    const std::string value = ascii_lowercase(trim(raw_value));
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    }) && value != "0";
}

bool is_supported_declaration_feature(std::string_view feature) {
    const std::size_t colon = find_top_level_colon(feature);
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string property = ascii_lowercase(trim(feature.substr(0, colon)));
    const std::string value = ascii_lowercase(trim(feature.substr(colon + 1)));
    if (property.empty() || value.empty()) {
        return false;
    }
    if (property.rfind("--", 0) == 0) {
        return true;
    }
    if (property == "display") {
        return supported_keyword(value, {"block", "inline", "inline-block", "flex",
                                        "inline-flex", "grid", "inline-grid", "none"});
    }
    if (property == "flex") {
        return value == "auto" || value == "none" ||
               value.find("px") != std::string::npos || is_number_with_suffix(value, {""}, false);
    }
    if (property == "flex-grow" || property == "flex-shrink") {
        return is_number_with_suffix(value, {""}, false);
    }
    if (property == "flex-basis") {
        return value == "auto" || is_supported_length_value(value);
    }
    if (property == "color" || property == "border-color") {
        return is_supported_color_value(value);
    }
    if (property == "background-color") {
        return is_supported_color_value(value);
    }
    if (property == "background") {
        return is_supported_background_value(value);
    }
    if (property == "width" || property == "height" || property == "min-width" ||
        property == "min-height" || property == "max-width" || property == "font-size" ||
        property == "text-indent" || property == "gap" || property == "row-gap" ||
        property == "column-gap" || property == "grid-auto-rows") {
        return is_supported_length_value(value);
    }
    if (property == "top" || property == "right" || property == "bottom" || property == "left") {
        return value == "auto" || is_supported_length_value(value);
    }
    if (property == "margin" || property == "margin-top" || property == "margin-right" ||
        property == "margin-bottom" || property == "margin-left") {
        return value == "auto" || is_supported_length_value(value);
    }
    if (property == "padding" || property == "padding-top" || property == "padding-right" ||
        property == "padding-bottom" || property == "padding-left" ||
        property == "border-width" || property == "border-top-width" ||
        property == "border-right-width" || property == "border-bottom-width" ||
        property == "border-left-width" || property == "border-radius") {
        return is_supported_length_value(value);
    }
    if (property == "border") {
        return value == "none" || value.find("var(") != std::string::npos ||
               value.find('#') != std::string::npos || value.find("rgb(") != std::string::npos ||
               value.find("px") != std::string::npos;
    }
    if (property == "aspect-ratio") {
        return value.find('/') != std::string::npos ||
               is_number_with_suffix(value, {""}, false);
    }
    if (property == "font-weight") {
        return supported_keyword(value, {"normal", "bold", "bolder", "lighter"}) ||
               is_number_with_suffix(value, {""}, false);
    }
    if (property == "line-height") {
        return is_supported_length_value(value) || is_number_with_suffix(value, {""}, false);
    }
    if (property == "text-align") {
        return supported_keyword(value, {"left", "right", "start", "end", "center"});
    }
    if (property == "box-sizing") {
        return supported_keyword(value, {"content-box", "border-box"});
    }
    if (property == "overflow") {
        return supported_keyword(value, {"visible", "hidden", "clip", "auto", "scroll"});
    }
    if (property == "opacity") {
        return is_number_with_suffix(value, {""}, false);
    }
    if (property == "position") {
        return supported_keyword(value, {"static", "relative", "absolute", "fixed", "sticky"});
    }
    if (property == "z-index") {
        return value == "auto" || is_number_with_suffix(value, {""}, false);
    }
    if (property == "transform") {
        return value == "none" || value.find("translate") != std::string::npos ||
               value.find("scale") != std::string::npos;
    }
    if (property == "transition" || property == "transition-property" ||
        property == "transition-duration" || property == "transition-delay" ||
        property == "transition-timing-function") {
        return value.find("opacity") != std::string::npos ||
               value.find("transform") != std::string::npos ||
               value.find("color") != std::string::npos ||
               value.find("ms") != std::string::npos ||
               value.find('s') != std::string::npos;
    }
    if (property == "animation-name") {
        return !value.empty();
    }
    if (property == "animation-iteration-count") {
        return value == "infinite" || is_positive_integer_value(value);
    }
    if (property == "animation" ||
        property == "animation-duration" || property == "animation-delay" ||
        property == "animation-timing-function" ||
        property == "animation-direction") {
        return value == "none" ||
               value == "infinite" ||
               value == "normal" ||
               value == "alternate" ||
               value.find("linear") != std::string::npos ||
               value.find("ease") != std::string::npos ||
               value.find("ms") != std::string::npos ||
               value.find('s') != std::string::npos ||
               is_number_with_suffix(value, {""}, false);
    }
    if (property == "justify-content") {
        return supported_keyword(value, {"start", "flex-start", "normal", "center",
                                        "space-around", "space-between"});
    }
    if (property == "align-items") {
        return supported_keyword(value, {"stretch", "normal", "start", "flex-start",
                                        "center", "end", "flex-end"});
    }
    if (property == "flex-wrap") {
        return supported_keyword(value, {"wrap", "wrap-reverse", "nowrap"});
    }
    if (property == "grid-template-columns") {
        return value.find("minmax(") != std::string::npos || value.find("repeat(") != std::string::npos ||
               value.find("fr") != std::string::npos || value.find("px") != std::string::npos;
    }
    if (property == "grid-column" || property == "grid-row") {
        return value.find("span") != std::string::npos;
    }
    if (property == "list-style" || property == "list-style-type") {
        return supported_keyword(value, {"none", "disc", "decimal", "decimal-leading-zero"});
    }
    if (property == "box-shadow") {
        return value == "none" || value.find('#') != std::string::npos ||
               value.find("rgb(") != std::string::npos || value.find("px") != std::string::npos;
    }
    return false;
}

bool evaluate_supports_condition(std::string_view condition);

bool evaluate_supports_operand(std::string_view& rest) {
    std::string_view body;
    if (!split_supports_parentheses(rest, body)) {
        return false;
    }
    const std::string_view inner = trim_view(body);
    if (inner.empty()) {
        return false;
    }
    const std::string lowered = ascii_lowercase(std::string(inner));
    if (lowered.rfind("selector(", 0) == 0 || lowered.rfind("font-tech(", 0) == 0 ||
        lowered.rfind("font-format(", 0) == 0) {
        return false;
    }
    if (find_top_level_colon(inner) != std::string_view::npos) {
        return is_supported_declaration_feature(inner);
    }
    return evaluate_supports_condition(inner);
}

bool evaluate_supports_condition(std::string_view condition) {
    std::string_view rest = trim_view(condition);
    if (rest.empty()) {
        return false;
    }
    const std::string lowered = ascii_lowercase(std::string(rest));
    if (starts_with_word(lowered, "not")) {
        rest = trim_view(rest.substr(3));
        return !evaluate_supports_condition(rest);
    }

    bool result = evaluate_supports_operand(rest);
    std::string active_operator;
    while (true) {
        rest = trim_view(rest);
        if (rest.empty()) {
            return result;
        }
        const std::string op = ascii_lowercase(std::string(consume_word(rest)));
        if (op != "and" && op != "or") {
            return false;
        }
        if (!active_operator.empty() && active_operator != op) {
            return false;
        }
        active_operator = op;
        const bool rhs = evaluate_supports_operand(rest);
        result = op == "and" ? (result && rhs) : (result || rhs);
    }
}

bool is_supported_supports_condition(std::string_view prelude) {
    return evaluate_supports_condition(prelude);
}

bool is_supported_group_at_rule(std::string_view name, std::string_view prelude, const CssParserOptions& options) {
    if (name == "layer") {
        return options.flatten_layer_blocks;
    }
    if (name == "media") {
        return options.parse_plain_media_blocks && is_supported_media_query(prelude, options);
    }
    if (name == "supports") {
        return options.parse_supports_blocks && is_supported_supports_condition(prelude);
    }
    return false;
}

bool is_selector_prelude_supported(std::string_view selector) {
    if (selector.empty()) {
        return false;
    }

    // Keep parser output visually conservative: selectors that require
    // unsupported tree or shadow semantics are skipped as full rules.
    static constexpr std::array<std::string_view, 3> kUnsupportedSelectorFeatures = {
        ":has(", "::part(", "::slotted("
    };
    for (std::string_view feature : kUnsupportedSelectorFeatures) {
        if (contains_ascii_case_insensitive(selector, feature)) {
            return false;
        }
    }
    return true;
}

bool strip_before_pseudo(std::string& selector) {
    constexpr std::string_view pseudo = "::before";
    const std::size_t pseudo_pos = selector.rfind(pseudo);
    if (pseudo_pos == std::string::npos) {
        return false;
    }
    if (pseudo_pos + pseudo.size() != selector.size()) {
        return false;
    }
    selector = trim(std::string_view(selector).substr(0, pseudo_pos));
    return !selector.empty();
}

void add_specificity(CssSpecificity& target, const CssSpecificity& value) {
    target.ids += value.ids;
    target.classes += value.classes;
    target.elements += value.elements;
}

bool specificity_less_than(const CssSpecificity& left, const CssSpecificity& right) {
    if (left.ids != right.ids) {
        return left.ids < right.ids;
    }
    if (left.classes != right.classes) {
        return left.classes < right.classes;
    }
    return left.elements < right.elements;
}

CssSpecificity max_specificity_in_selector_list(std::string_view selector_list);

CssSpecificity calculate_specificity(std::string_view selector) {
    CssSpecificity specificity;
    bool in_attribute = false;
    char quote = '\0';
    for (std::size_t index = 0; index < selector.size(); ++index) {
        const char ch = selector[index];
        if (quote != '\0') {
            if (ch == '\\' && index + 1 < selector.size()) {
                ++index;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '[') {
            in_attribute = true;
            ++specificity.classes;
            continue;
        }
        if (ch == ']') {
            in_attribute = false;
            continue;
        }
        if (in_attribute) {
            continue;
        }
        if (ch == '#') {
            ++specificity.ids;
            continue;
        }
        if (ch == '.') {
            ++specificity.classes;
            continue;
        }
        if (ch == ':') {
            if (index + 1 < selector.size() && selector[index + 1] == ':') {
                ++specificity.elements;
                ++index;
                continue;
            }
            const std::size_t name_begin = index + 1;
            std::size_t name_end = name_begin;
            while (name_end < selector.size() &&
                   (std::isalnum(static_cast<unsigned char>(selector[name_end])) != 0 ||
                    selector[name_end] == '-' || selector[name_end] == '_')) {
                ++name_end;
            }
            const std::string pseudo = ascii_lowercase(std::string(selector.substr(name_begin, name_end - name_begin)));
            if ((pseudo == "is" || pseudo == "where") && name_end < selector.size() && selector[name_end] == '(') {
                int depth = 0;
                std::size_t close = std::string_view::npos;
                for (std::size_t cursor = name_end; cursor < selector.size(); ++cursor) {
                    if (selector[cursor] == '(') {
                        ++depth;
                    } else if (selector[cursor] == ')') {
                        --depth;
                        if (depth == 0) {
                            close = cursor;
                            break;
                        }
                    }
                }
                if (close != std::string_view::npos) {
                    if (pseudo == "is") {
                        add_specificity(specificity,
                                        max_specificity_in_selector_list(
                                            selector.substr(name_end + 1, close - name_end - 1)));
                    }
                    index = close;
                    continue;
                }
            }
            ++specificity.classes;
            continue;
        }
        const bool at_identifier_start =
            (std::isalpha(static_cast<unsigned char>(ch)) != 0) &&
            (index == 0 || is_ascii_space(selector[index - 1]) ||
             selector[index - 1] == '>' || selector[index - 1] == '+' ||
             selector[index - 1] == '~' || selector[index - 1] == ',');
        if (at_identifier_start) {
            ++specificity.elements;
        }
    }
    return specificity;
}

CssSpecificity max_specificity_in_selector_list(std::string_view selector_list) {
    CssSpecificity result;
    for (const std::string& item : split_top_level_commas(selector_list)) {
        const CssSpecificity current = calculate_specificity(item);
        if (specificity_less_than(result, current)) {
            result = current;
        }
    }
    return result;
}

bool finish_important(std::string& value) {
    std::string lowered = ascii_lowercase(trim(value));
    constexpr std::string_view important = "!important";
    if (lowered.size() < important.size()) {
        value = trim(value);
        return false;
    }

    if (lowered.compare(lowered.size() - important.size(), important.size(), important) == 0) {
        value = trim(std::string_view(value).substr(0, value.size() - important.size()));
        return true;
    }

    value = trim(value);
    return false;
}

class CssParserRun {
public:
    CssParserRun(std::string_view source, const CssParserOptions& options)
        : source_(source), options_(options) {}

    Stylesheet parse() {
        parse_rule_list(0, false);
        return std::move(stylesheet_);
    }

private:
    bool eof() const {
        return index_ >= source_.size();
    }

    char peek() const {
        return eof() ? '\0' : source_[index_];
    }

    char consume() {
        return eof() ? '\0' : source_[index_++];
    }

    void skip_whitespace_and_comments() {
        while (!eof()) {
            if (is_ascii_space(peek())) {
                consume();
                continue;
            }
            if (index_ + 1 < source_.size() && source_[index_] == '/' && source_[index_ + 1] == '*') {
                index_ += 2;
                while (index_ + 1 < source_.size() && !(source_[index_] == '*' && source_[index_ + 1] == '/')) {
                    ++index_;
                }
                if (index_ + 1 < source_.size()) {
                    index_ += 2;
                }
                continue;
            }
            break;
        }
    }

    void parse_rule_list(std::size_t depth, bool stop_at_block_end) {
        while (!eof()) {
            skip_whitespace_and_comments();
            if (eof()) {
                return;
            }
            if (stop_at_block_end && peek() == '}') {
                consume();
                return;
            }
            if (total_rule_count() >= options_.max_rules) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-rule-limit",
                                  "CSS rule budget was reached; remaining rules were skipped",
                                  "This keeps low-memory targets bounded but may remove later cascade overrides.");
                skip_until_eof_or_block_end(stop_at_block_end);
                return;
            }
            if (peek() == '@') {
                parse_at_rule(depth);
            } else {
                parse_qualified_rule();
            }
        }
    }

    void parse_at_rule(std::size_t depth) {
        consume();
        const std::size_t name_begin = index_;
        while (!eof() && is_name_char(peek())) {
            consume();
        }
        const std::string name = ascii_lowercase(std::string(source_.substr(name_begin, index_ - name_begin)));
        const std::size_t prelude_begin = index_;
        const char delimiter = consume_component_until_rule_delimiter();
        const std::string prelude = trim(source_.substr(prelude_begin, index_ - prelude_begin));

        if (delimiter == ';') {
            consume();
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Info,
                              "css-at-rule-ignored",
                              "Non-block CSS at-rule was ignored",
                              name.empty() ? std::string("@") : "@" + name);
            return;
        }
        if (delimiter != '{') {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-at-rule-malformed",
                              "Malformed CSS at-rule was skipped",
                              name.empty() ? std::string("@") : "@" + name);
            return;
        }

        consume();
        if (name == "keyframes") {
            parse_keyframes_rule(prelude);
            return;
        }
        if (depth + 1 <= options_.max_nesting_depth && is_supported_group_at_rule(name, prelude, options_)) {
            parse_rule_list(depth + 1, true);
        } else {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-at-rule-skipped",
                              "CSS at-rule block was skipped or lazily ignored",
                              "@" + name + (prelude.empty() ? std::string{} : " " + prelude));
            skip_balanced_block();
        }
    }

    void parse_qualified_rule() {
        const std::size_t selector_begin = index_;
        const char delimiter = consume_component_until_rule_delimiter();
        const std::string selector_text = collapse_ascii_space(source_.substr(selector_begin, index_ - selector_begin));
        if (delimiter != '{') {
            if (delimiter == ';') {
                consume();
            }
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-rule-malformed",
                              "Malformed CSS qualified rule was skipped",
                              selector_text);
            return;
        }

        consume();
        std::vector<CssDeclaration> declarations = parse_declaration_block();
        if (declarations.empty()) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Info,
                              "css-empty-rule",
                              "CSS rule had no usable declarations",
                              selector_text);
            return;
        }
        if (!is_selector_prelude_supported(selector_text)) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-selector-skipped",
                              "CSS selector requires unsupported selector semantics and was skipped",
                              selector_text);
            return;
        }

        for (std::string selector : split_selector_list(selector_text)) {
            if (total_rule_count() >= options_.max_rules) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-rule-limit",
                                  "CSS rule budget was reached; remaining selector-list items were skipped",
                                  selector_text);
                return;
            }
            if (selector.empty() || !is_selector_prelude_supported(selector)) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-selector-skipped",
                                  "CSS selector-list item was skipped",
                                  selector);
                continue;
            }
            const bool pseudo_before = strip_before_pseudo(selector);
            CssRule rule;
            rule.selector = std::move(selector);
            rule.pseudo_before = pseudo_before;
            rule.declarations = declarations;
            rule.specificity = calculate_specificity(rule.selector);
            rule.selector_parts = parse_css_selector_parts(rule.selector);
            if (rule.selector_parts.empty()) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-selector-unmatched",
                                  "CSS selector could not be indexed and was skipped",
                                  rule.selector);
                continue;
            }
            rule.index_key = build_css_rule_index_key(rule.selector_parts);
            rule.source_order = next_source_order_++;
            stylesheet_.push_back(std::move(rule));
        }
    }

    void parse_keyframes_rule(const std::string& prelude) {
        CssKeyframesRule rule;
        rule.name = trim(prelude);
        rule.source_order = next_source_order_++;
        if (rule.name.empty()) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-keyframes-malformed",
                              "CSS @keyframes rule without a name was skipped",
                              {});
            skip_balanced_block();
            return;
        }
        if (total_rule_count() >= options_.max_rules) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Warning,
                              "css-rule-limit",
                              "CSS rule budget was reached; @keyframes was skipped",
                              rule.name);
            skip_balanced_block();
            return;
        }

        while (!eof()) {
            skip_whitespace_and_comments();
            if (eof()) {
                break;
            }
            if (peek() == '}') {
                consume();
                break;
            }
            const std::size_t selector_begin = index_;
            const char delimiter = consume_component_until_rule_delimiter();
            const std::string selector = ascii_lowercase(trim(source_.substr(selector_begin, index_ - selector_begin)));
            if (delimiter != '{') {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-keyframe-malformed",
                                  "Malformed CSS keyframe selector was skipped",
                                  selector);
                if (delimiter == ';') {
                    consume();
                }
                continue;
            }
            consume();
            std::vector<CssDeclaration> declarations = parse_declaration_block();
            if (selector == "from" || selector == "0%") {
                rule.from_declarations = std::move(declarations);
            } else if (selector == "to" || selector == "100%") {
                rule.to_declarations = std::move(declarations);
            } else {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Info,
                                  "css-keyframe-selector-ignored",
                                  "Only from/to or 0%/100% keyframes are supported; intermediate keyframe was ignored",
                                  selector);
            }
        }

        if (rule.from_declarations.empty() && rule.to_declarations.empty()) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Css,
                              DiagnosticSeverity::Info,
                              "css-keyframes-empty",
                              "CSS @keyframes rule had no usable from/to declarations",
                              rule.name);
            return;
        }
        stylesheet_.push_keyframes(std::move(rule));
    }

    std::vector<CssDeclaration> parse_declaration_block() {
        std::vector<CssDeclaration> declarations;
        while (!eof()) {
            skip_whitespace_and_comments();
            if (eof()) {
                break;
            }
            if (peek() == '}') {
                consume();
                break;
            }
            if (declarations.size() >= options_.max_declarations_per_rule) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-declaration-limit",
                                  "CSS declaration budget was reached; remaining declarations in the block were skipped",
                                  {});
                skip_balanced_block_tail();
                break;
            }

            const std::size_t name_begin = index_;
            while (!eof() && peek() != ':' && peek() != ';' && peek() != '{' && peek() != '}') {
                if (starts_comment()) {
                    break;
                }
                consume();
            }

            std::string property = ascii_lowercase(trim(source_.substr(name_begin, index_ - name_begin)));
            if (property.empty()) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-declaration-malformed",
                                  "Malformed CSS declaration was skipped",
                                  {});
                recover_declaration();
                continue;
            }
            skip_whitespace_and_comments();
            if (eof() || peek() != ':') {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-declaration-malformed",
                                  "CSS declaration without ':' was skipped",
                                  property);
                recover_declaration();
                continue;
            }
            consume();

            std::string value = consume_declaration_value();
            CssDeclaration declaration;
            declaration.property = std::move(property);
            declaration.important = finish_important(value);
            declaration.value = std::move(value);
            if (!declaration.value.empty()) {
                declarations.push_back(std::move(declaration));
            } else {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Css,
                                  DiagnosticSeverity::Warning,
                                  "css-empty-declaration",
                                  "CSS declaration had an empty value and was skipped",
                                  declaration.property);
            }
        }
        return declarations;
    }

    char consume_component_until_rule_delimiter() {
        while (!eof()) {
            const char ch = peek();
            if (ch == '{' || ch == ';' || ch == '}') {
                return ch;
            }
            consume_component_char();
        }
        return '\0';
    }

    std::string consume_declaration_value() {
        std::string value;
        int paren_depth = 0;
        int bracket_depth = 0;
        while (!eof()) {
            if (starts_comment()) {
                skip_whitespace_and_comments();
                if (!value.empty() && value.back() != ' ') {
                    value.push_back(' ');
                }
                continue;
            }

            const char ch = peek();
            if (ch == '"' || ch == '\'') {
                consume_string_into(value, consume());
                continue;
            }
            if (ch == '(') {
                ++paren_depth;
                value.push_back(consume());
            } else if (ch == ')' && paren_depth > 0) {
                --paren_depth;
                value.push_back(consume());
            } else if (ch == '[') {
                ++bracket_depth;
                value.push_back(consume());
            } else if (ch == ']' && bracket_depth > 0) {
                --bracket_depth;
                value.push_back(consume());
            } else if ((ch == ';' || ch == '}') && paren_depth == 0 && bracket_depth == 0) {
                if (ch == ';') {
                    consume();
                }
                break;
            } else {
                value.push_back(consume());
            }
        }
        return value;
    }

    void consume_component_char() {
        const char ch = consume();
        if (ch == '"' || ch == '\'') {
            skip_string(ch);
        } else if (ch == '(') {
            skip_until_matching(')');
        } else if (ch == '[') {
            skip_until_matching(']');
        } else if (starts_comment_at_previous()) {
            skip_comment_tail();
        }
    }

    bool starts_comment() const {
        return index_ + 1 < source_.size() && source_[index_] == '/' && source_[index_ + 1] == '*';
    }

    bool starts_comment_at_previous() const {
        return index_ >= 1 && index_ < source_.size() && source_[index_ - 1] == '/' && source_[index_] == '*';
    }

    void skip_comment_tail() {
        ++index_;
        while (index_ + 1 < source_.size() && !(source_[index_] == '*' && source_[index_ + 1] == '/')) {
            ++index_;
        }
        if (index_ + 1 < source_.size()) {
            index_ += 2;
        }
    }

    void skip_string(char quote) {
        while (!eof()) {
            const char ch = consume();
            if (ch == '\\' && !eof()) {
                consume();
            } else if (ch == quote) {
                return;
            }
        }
    }

    void consume_string_into(std::string& output, char quote) {
        output.push_back(quote);
        while (!eof()) {
            const char ch = consume();
            output.push_back(ch);
            if (ch == '\\' && !eof()) {
                output.push_back(consume());
            } else if (ch == quote) {
                return;
            }
        }
    }

    void skip_until_matching(char close) {
        while (!eof()) {
            const char ch = consume();
            if (ch == '"' || ch == '\'') {
                skip_string(ch);
            } else if (ch == close) {
                return;
            }
        }
    }

    void skip_balanced_block() {
        int depth = 1;
        while (!eof() && depth > 0) {
            if (starts_comment()) {
                skip_whitespace_and_comments();
                continue;
            }
            const char ch = consume();
            if (ch == '"' || ch == '\'') {
                skip_string(ch);
            } else if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
            }
        }
    }

    void skip_balanced_block_tail() {
        while (!eof()) {
            if (peek() == '}') {
                consume();
                return;
            }
            consume_component_char();
        }
    }

    void skip_until_eof_or_block_end(bool stop_at_block_end) {
        while (!eof()) {
            if (stop_at_block_end && peek() == '}') {
                return;
            }
            consume_component_char();
        }
    }

    void recover_declaration() {
        while (!eof()) {
            if (peek() == ';') {
                consume();
                return;
            }
            if (peek() == '}') {
                return;
            }
            if (peek() == '{') {
                consume();
                skip_balanced_block();
                return;
            }
            consume_component_char();
        }
    }

    std::vector<std::string> split_selector_list(std::string_view selector_text) {
        std::vector<std::string> selectors;
        std::size_t begin = 0;
        int paren_depth = 0;
        int bracket_depth = 0;
        char quote = '\0';
        for (std::size_t i = 0; i < selector_text.size(); ++i) {
            const char ch = selector_text[i];
            if (quote != '\0') {
                if (ch == '\\' && i + 1 < selector_text.size()) {
                    ++i;
                } else if (ch == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(') {
                ++paren_depth;
            } else if (ch == ')' && paren_depth > 0) {
                --paren_depth;
            } else if (ch == '[') {
                ++bracket_depth;
            } else if (ch == ']' && bracket_depth > 0) {
                --bracket_depth;
            } else if (ch == ',' && paren_depth == 0 && bracket_depth == 0) {
                selectors.push_back(trim(selector_text.substr(begin, i - begin)));
                begin = i + 1;
            }
        }
        selectors.push_back(trim(selector_text.substr(begin)));
        return selectors;
    }

    std::size_t total_rule_count() const {
        return stylesheet_.size() + stylesheet_.keyframes_size();
    }

    std::string_view source_;
    const CssParserOptions& options_;
    std::size_t index_ = 0;
    std::size_t next_source_order_ = 0;
    Stylesheet stylesheet_;
};

} // namespace

Stylesheet CssParser::parse(const std::string& source) const {
    return parse(source, CssParserOptions{});
}

Stylesheet CssParser::parse(const std::string& source, const CssParserOptions& options) const {
    CssParserRun run(source, options);
    return run.parse();
}

} // namespace jellyframe
