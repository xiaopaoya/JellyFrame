#include "core/css_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace wearweb {
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

bool is_plain_media_query(std::string_view prelude) {
    const std::string media = ascii_lowercase(collapse_ascii_space(prelude));
    return media.empty() || media == "all" || media == "screen";
}

bool is_supported_group_at_rule(std::string_view name, std::string_view prelude, const CssParserOptions& options) {
    if (name == "layer") {
        return options.flatten_layer_blocks;
    }
    if (name == "media") {
        return options.parse_plain_media_blocks && is_plain_media_query(prelude);
    }
    return false;
}

bool is_selector_prelude_supported(std::string_view selector) {
    if (selector.empty()) {
        return false;
    }

    // Keep parser output visually conservative: selectors that require modern
    // cascade semantics are skipped until the selector matcher supports them.
    static constexpr std::array<std::string_view, 5> kUnsupportedSelectorFeatures = {
        ":has(", ":is(", ":where(", "::part(", "::slotted("
    };
    for (std::string_view feature : kUnsupportedSelectorFeatures) {
        if (contains_ascii_case_insensitive(selector, feature)) {
            return false;
        }
    }
    if (selector.find_first_of("+~") != std::string_view::npos) {
        return false;
    }
    return true;
}

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
        if (ch == '.' || ch == ':') {
            ++specificity.classes;
            if (ch == ':' && index + 1 < selector.size() && selector[index + 1] == ':') {
                --specificity.classes;
                ++specificity.elements;
                ++index;
            }
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
            if (stylesheet_.size() >= options_.max_rules) {
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
            return;
        }
        if (delimiter != '{') {
            return;
        }

        consume();
        if (depth + 1 <= options_.max_nesting_depth && is_supported_group_at_rule(name, prelude, options_)) {
            parse_rule_list(depth + 1, true);
        } else {
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
            return;
        }

        consume();
        std::vector<CssDeclaration> declarations = parse_declaration_block();
        if (declarations.empty() || !is_selector_prelude_supported(selector_text)) {
            return;
        }

        for (std::string selector : split_selector_list(selector_text)) {
            if (stylesheet_.size() >= options_.max_rules) {
                return;
            }
            if (selector.empty() || !is_selector_prelude_supported(selector)) {
                continue;
            }
            CssRule rule;
            rule.selector = std::move(selector);
            rule.declarations = declarations;
            rule.specificity = calculate_specificity(rule.selector);
            rule.selector_parts = parse_css_selector_parts(rule.selector);
            if (rule.selector_parts.empty()) {
                continue;
            }
            rule.index_key = build_css_rule_index_key(rule.selector_parts);
            rule.source_order = next_source_order_++;
            stylesheet_.push_back(std::move(rule));
        }
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
                recover_declaration();
                continue;
            }
            skip_whitespace_and_comments();
            if (eof() || peek() != ':') {
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

} // namespace wearweb
