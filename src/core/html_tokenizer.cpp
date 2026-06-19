#include "core/html_tokenizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>

namespace jellyframe {
namespace {

enum class State {
    Data,
    TagOpen,
    EndTagOpen,
    MarkupDeclarationOpen,
    TagName,
    BeforeAttributeName,
    AttributeName,
    AfterAttributeName,
    BeforeAttributeValue,
    AttributeValueDoubleQuoted,
    AttributeValueSingleQuoted,
    AttributeValueUnquoted,
    AfterAttributeValueQuoted,
    SelfClosingStartTag,
    RawText,
    RcData,
    EndOfFile,
};

constexpr char kReplacement[] = "\xEF\xBF\xBD";

bool is_ascii_alpha(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalpha(byte) != 0;
}

bool is_ascii_alnum(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0;
}

bool is_ascii_space(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isspace(byte) != 0;
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

std::string normalize_newlines(std::string_view source) {
    std::string result;
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '\r') {
            if (index + 1 < source.size() && source[index + 1] == '\n') {
                ++index;
            }
            result.push_back('\n');
        } else {
            result.push_back(source[index]);
        }
    }
    return result;
}

void append_codepoint_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        output += kReplacement;
        return;
    }

    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool starts_with_case_insensitive(std::string_view source, std::size_t index, std::string_view needle) {
    if (index + needle.size() > source.size()) {
        return false;
    }
    for (std::size_t offset = 0; offset < needle.size(); ++offset) {
        if (ascii_lower(source[index + offset]) != ascii_lower(needle[offset])) {
            return false;
        }
    }
    return true;
}

bool starts_with_raw_text_end_tag(std::string_view source, std::size_t index, std::string_view tag_name) {
    if (index + 2 + tag_name.size() > source.size() || source[index] != '<' || source[index + 1] != '/') {
        return false;
    }
    for (std::size_t offset = 0; offset < tag_name.size(); ++offset) {
        if (ascii_lower(source[index + 2 + offset]) != tag_name[offset]) {
            return false;
        }
    }
    const std::size_t after_name = index + 2 + tag_name.size();
    return after_name >= source.size() || is_ascii_space(source[after_name]) ||
        source[after_name] == '>' || source[after_name] == '/';
}

bool is_raw_text_element(const std::string& name) {
    return name == "script" || name == "style";
}

bool is_rcdata_element(const std::string& name) {
    return name == "textarea" || name == "title";
}

bool is_duplicate_attribute(const HtmlToken& token, const std::string& name) {
    return std::any_of(token.attributes.begin(), token.attributes.end(), [&](const HtmlAttribute& attribute) {
        return attribute.name == name;
    });
}

std::string lookup_named_character_reference(const std::string& name) {
    struct NamedReference {
        std::string_view name;
        std::string_view replacement;
    };
    static constexpr std::array<NamedReference, 34> kNamedReferences = {{
        {"amp", "&"},
        {"apos", "'"},
        {"bull", "\xE2\x80\xA2"},
        {"cent", "\xC2\xA2"},
        {"copy", "\xC2\xA9"},
        {"deg", "\xC2\xB0"},
        {"divide", "\xC3\xB7"},
        {"euro", "\xE2\x82\xAC"},
        {"gt", ">"},
        {"hellip", "\xE2\x80\xA6"},
        {"laquo", "\xC2\xAB"},
        {"ldquo", "\xE2\x80\x9C"},
        {"lsaquo", "\xE2\x80\xB9"},
        {"lsquo", "\xE2\x80\x98"},
        {"lt", "<"},
        {"mdash", "\xE2\x80\x94"},
        {"micro", "\xC2\xB5"},
        {"middot", "\xC2\xB7"},
        {"nbsp", "\xC2\xA0"},
        {"ndash", "\xE2\x80\x93"},
        {"para", "\xC2\xB6"},
        {"plusmn", "\xC2\xB1"},
        {"pound", "\xC2\xA3"},
        {"quot", "\""},
        {"raquo", "\xC2\xBB"},
        {"rdquo", "\xE2\x80\x9D"},
        {"reg", "\xC2\xAE"},
        {"rsaquo", "\xE2\x80\xBA"},
        {"rsquo", "\xE2\x80\x99"},
        {"sect", "\xC2\xA7"},
        {"times", "\xC3\x97"},
        {"trade", "\xE2\x84\xA2"},
        {"yen", "\xC2\xA5"},
        {"ZeroWidthSpace", "\xE2\x80\x8B"},
    }};
    for (const NamedReference& reference : kNamedReferences) {
        if (name == reference.name) {
            return std::string(reference.replacement);
        }
    }
    return {};
}

std::uint32_t remap_numeric_character_reference(std::uint32_t value) {
    switch (value) {
    case 0x80: return 0x20AC;
    case 0x82: return 0x201A;
    case 0x83: return 0x0192;
    case 0x84: return 0x201E;
    case 0x85: return 0x2026;
    case 0x86: return 0x2020;
    case 0x87: return 0x2021;
    case 0x88: return 0x02C6;
    case 0x89: return 0x2030;
    case 0x8A: return 0x0160;
    case 0x8B: return 0x2039;
    case 0x8C: return 0x0152;
    case 0x8E: return 0x017D;
    case 0x91: return 0x2018;
    case 0x92: return 0x2019;
    case 0x93: return 0x201C;
    case 0x94: return 0x201D;
    case 0x95: return 0x2022;
    case 0x96: return 0x2013;
    case 0x97: return 0x2014;
    case 0x98: return 0x02DC;
    case 0x99: return 0x2122;
    case 0x9A: return 0x0161;
    case 0x9B: return 0x203A;
    case 0x9C: return 0x0153;
    case 0x9E: return 0x017E;
    case 0x9F: return 0x0178;
    default:
        return value;
    }
}

class TokenizerRun {
public:
    TokenizerRun(std::string_view source, HtmlTokenSink& sink, HtmlTokenizerOptions options)
        : source_(source), sink_(sink), options_(options) {}

    void run() {
        while (state_ != State::EndOfFile) {
            switch (state_) {
            case State::Data:
                data_state();
                break;
            case State::TagOpen:
                tag_open_state();
                break;
            case State::EndTagOpen:
                end_tag_open_state();
                break;
            case State::MarkupDeclarationOpen:
                markup_declaration_open_state();
                break;
            case State::TagName:
                tag_name_state();
                break;
            case State::BeforeAttributeName:
                before_attribute_name_state();
                break;
            case State::AttributeName:
                attribute_name_state();
                break;
            case State::AfterAttributeName:
                after_attribute_name_state();
                break;
            case State::BeforeAttributeValue:
                before_attribute_value_state();
                break;
            case State::AttributeValueDoubleQuoted:
                attribute_value_quoted_state('"');
                break;
            case State::AttributeValueSingleQuoted:
                attribute_value_quoted_state('\'');
                break;
            case State::AttributeValueUnquoted:
                attribute_value_unquoted_state();
                break;
            case State::AfterAttributeValueQuoted:
                after_attribute_value_quoted_state();
                break;
            case State::SelfClosingStartTag:
                self_closing_start_tag_state();
                break;
            case State::RawText:
                raw_text_state();
                break;
            case State::RcData:
                rcdata_state();
                break;
            case State::EndOfFile:
                break;
            }
        }

        flush_text();
        emit_token(HtmlToken{HtmlTokenType::EndOfFile});
    }

private:
    std::string snippet_at(std::size_t index) const {
        if (index >= source_.size()) {
            return {};
        }
        constexpr std::size_t kMaxSnippetBytes = 48;
        const std::size_t size = std::min(kMaxSnippetBytes, source_.size() - index);
        return std::string(source_.substr(index, size));
    }

    void report(DiagnosticSeverity severity,
                std::string_view code,
                std::string_view message,
                std::string_view detail = {}) const {
        report_diagnostic(options_.diagnostics, DiagnosticStage::Html, severity, code, message, detail);
    }

    bool eof() const {
        return index_ >= source_.size();
    }

    char peek() const {
        return eof() ? '\0' : source_[index_];
    }

    char consume() {
        return eof() ? '\0' : source_[index_++];
    }

    void reconsume_in(State state) {
        if (index_ > 0) {
            --index_;
        }
        state_ = state;
    }

    void append_text_char(char ch) {
        if (ch == '\0') {
            report(DiagnosticSeverity::Warning,
                   "html-null-character",
                   "HTML null character was replaced with U+FFFD",
                   snippet_at(index_ > 0 ? index_ - 1 : 0));
            text_buffer_ += kReplacement;
        } else {
            text_buffer_.push_back(ch);
        }
    }

    void flush_text() {
        if (text_buffer_.empty()) {
            return;
        }
        HtmlToken token;
        token.type = HtmlTokenType::Text;
        token.data = std::move(text_buffer_);
        text_buffer_.clear();
        emit_token(token);
    }

    void begin_token(HtmlTokenType type) {
        current_token_ = HtmlToken{};
        current_token_.type = type;
        current_attribute_ = HtmlAttribute{};
        has_current_attribute_ = false;
    }

    void begin_attribute() {
        finish_attribute();
        current_attribute_ = HtmlAttribute{};
        has_current_attribute_ = true;
    }

    void finish_attribute() {
        if (!has_current_attribute_ || current_attribute_.name.empty()) {
            has_current_attribute_ = false;
            return;
        }
        if (!is_duplicate_attribute(current_token_, current_attribute_.name)) {
            current_token_.attributes.push_back(std::move(current_attribute_));
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-duplicate-attribute",
                   "Duplicate HTML attribute was ignored",
                   current_attribute_.name);
        }
        current_attribute_ = HtmlAttribute{};
        has_current_attribute_ = false;
    }

    void emit_current_token() {
        finish_attribute();
        if (current_token_.type == HtmlTokenType::StartTag || current_token_.type == HtmlTokenType::EndTag) {
            current_token_.name = ascii_lowercase(current_token_.name);
        }
        const bool enter_raw_text =
            current_token_.type == HtmlTokenType::StartTag && is_raw_text_element(current_token_.name) &&
            !current_token_.self_closing;
        const bool enter_rcdata =
            current_token_.type == HtmlTokenType::StartTag && is_rcdata_element(current_token_.name) &&
            !current_token_.self_closing;
        raw_text_end_tag_ = (enter_raw_text || enter_rcdata) ? current_token_.name : std::string{};
        emit_token(current_token_);
        state_ = enter_raw_text ? State::RawText : enter_rcdata ? State::RcData : State::Data;
    }

    std::string consume_character_reference() {
        const std::size_t start = index_;
        if (eof()) {
            return "&";
        }

        if (peek() == '#') {
            consume();
            bool hex = false;
            if (!eof() && (peek() == 'x' || peek() == 'X')) {
                hex = true;
                consume();
            }

            std::uint32_t value = 0;
            bool has_digits = false;
            while (!eof()) {
                const char ch = peek();
                int digit = -1;
                if (ch >= '0' && ch <= '9') {
                    digit = ch - '0';
                } else if (hex && ch >= 'a' && ch <= 'f') {
                    digit = ch - 'a' + 10;
                } else if (hex && ch >= 'A' && ch <= 'F') {
                    digit = ch - 'A' + 10;
                }

                if (digit < 0 || (!hex && digit > 9)) {
                    break;
                }
                has_digits = true;
                value = value * (hex ? 16U : 10U) + static_cast<std::uint32_t>(digit);
                consume();
            }

            if (!has_digits) {
                index_ = start;
                report(DiagnosticSeverity::Warning,
                       "html-character-reference-invalid",
                       "Numeric character reference had no digits and was kept as text",
                       snippet_at(start > 0 ? start - 1 : start));
                return "&";
            }
            if (!eof() && peek() == ';') {
                consume();
            } else {
                report(DiagnosticSeverity::Info,
                       "html-character-reference-missing-semicolon",
                       "Numeric character reference is missing a semicolon",
                       snippet_at(start > 0 ? start - 1 : start));
            }

            std::string output;
            append_codepoint_utf8(output, remap_numeric_character_reference(value));
            return output;
        }

        std::string name;
        while (!eof() && is_ascii_alnum(peek())) {
            name.push_back(consume());
        }
        const bool had_semicolon = !eof() && peek() == ';';
        if (had_semicolon) {
            consume();
        }

        const std::string replacement = lookup_named_character_reference(name);
        if (!replacement.empty()) {
            if (!had_semicolon) {
                report(DiagnosticSeverity::Info,
                       "html-character-reference-missing-semicolon",
                       "Named character reference is missing a semicolon",
                       "&" + name);
            }
            return replacement;
        }

        index_ = start;
        report(DiagnosticSeverity::Warning,
               "html-character-reference-unknown",
               "Unknown named character reference was kept as text",
               name.empty() ? snippet_at(start > 0 ? start - 1 : start) : "&" + name);
        return "&";
    }

    void data_state() {
        if (eof()) {
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (ch == '&') {
            text_buffer_ += consume_character_reference();
        } else if (ch == '<') {
            state_ = State::TagOpen;
        } else {
            append_text_char(ch);
        }
    }

    void tag_open_state() {
        if (eof()) {
            text_buffer_.push_back('<');
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (ch == '!') {
            flush_text();
            state_ = State::MarkupDeclarationOpen;
        } else if (ch == '/') {
            state_ = State::EndTagOpen;
        } else if (ch == '?') {
            flush_text();
            report(DiagnosticSeverity::Warning,
                   "html-processing-instruction",
                   "Processing-instruction-like markup was treated as a bogus comment",
                   snippet_at(index_ > 0 ? index_ - 1 : 0));
            consume_bogus_comment();
            state_ = State::Data;
        } else if (is_ascii_alpha(ch)) {
            flush_text();
            begin_token(HtmlTokenType::StartTag);
            reconsume_in(State::TagName);
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-invalid-tag-open",
                   "Invalid '<' sequence was treated as text",
                   snippet_at(index_ > 0 ? index_ - 1 : 0));
            text_buffer_.push_back('<');
            reconsume_in(State::Data);
        }
    }

    void end_tag_open_state() {
        if (eof()) {
            text_buffer_ += "</";
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_alpha(ch)) {
            flush_text();
            begin_token(HtmlTokenType::EndTag);
            reconsume_in(State::TagName);
        } else if (ch == '>') {
            report(DiagnosticSeverity::Warning,
                   "html-empty-end-tag",
                   "Empty end tag was ignored",
                   "</>");
            state_ = State::Data;
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-invalid-end-tag",
                   "Invalid end tag was treated as a bogus comment",
                   snippet_at(index_ > 1 ? index_ - 2 : 0));
            consume_bogus_comment();
            state_ = State::Data;
        }
    }

    void markup_declaration_open_state() {
        if (starts_with_case_insensitive(source_, index_, "--")) {
            index_ += 2;
            consume_comment();
        } else if (starts_with_case_insensitive(source_, index_, "doctype")) {
            index_ += 7;
            consume_doctype();
        } else if (starts_with_case_insensitive(source_, index_, "[CDATA[")) {
            index_ += 7;
            report(DiagnosticSeverity::Info,
                   "html-cdata-as-text",
                   "CDATA section was treated as text by the simplified HTML tokenizer",
                   snippet_at(index_));
            consume_cdata_as_text();
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-bogus-declaration",
                   "Unknown markup declaration was treated as a bogus comment",
                   snippet_at(index_));
            consume_bogus_comment();
        }
        state_ = State::Data;
    }

    void tag_name_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-tag-unclosed",
                   "Tag reached EOF before '>'",
                   current_token_.name);
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            state_ = current_token_.type == HtmlTokenType::StartTag ? State::BeforeAttributeName : State::Data;
            if (current_token_.type == HtmlTokenType::EndTag) {
                consume_until_tag_end();
                emit_current_token();
            }
        } else if (ch == '/') {
            state_ = current_token_.type == HtmlTokenType::StartTag ? State::SelfClosingStartTag : State::Data;
            if (current_token_.type == HtmlTokenType::EndTag) {
                consume_until_tag_end();
                emit_current_token();
            }
        } else if (ch == '>') {
            emit_current_token();
        } else if (ch == '\0') {
            report(DiagnosticSeverity::Warning,
                   "html-null-character",
                   "Null character in tag name was replaced with U+FFFD",
                   current_token_.name);
            current_token_.name += kReplacement;
        } else {
            current_token_.name.push_back(ascii_lower(ch));
        }
    }

    void before_attribute_name_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-tag-unclosed",
                   "Tag reached EOF while reading attributes",
                   current_token_.name);
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            return;
        }
        if (ch == '/') {
            state_ = State::SelfClosingStartTag;
        } else if (ch == '>') {
            emit_current_token();
        } else {
            if (ch == '<' || ch == '=' || ch == '"' || ch == '\'') {
                report(DiagnosticSeverity::Warning,
                       "html-suspicious-attribute-name",
                       "Suspicious character started an HTML attribute name",
                       snippet_at(index_ > 0 ? index_ - 1 : 0));
            }
            begin_attribute();
            reconsume_in(State::AttributeName);
        }
    }

    void attribute_name_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-tag-unclosed",
                   "Tag reached EOF while reading an attribute name",
                   current_token_.name);
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            state_ = State::AfterAttributeName;
        } else if (ch == '/') {
            finish_attribute();
            state_ = State::SelfClosingStartTag;
        } else if (ch == '=') {
            state_ = State::BeforeAttributeValue;
        } else if (ch == '>') {
            finish_attribute();
            emit_current_token();
        } else if (ch == '\0') {
            report(DiagnosticSeverity::Warning,
                   "html-null-character",
                   "Null character in attribute name was replaced with U+FFFD",
                   current_attribute_.name);
            current_attribute_.name += kReplacement;
        } else {
            if (ch == '<' || ch == '"' || ch == '\'') {
                report(DiagnosticSeverity::Warning,
                       "html-suspicious-attribute-name",
                       "Suspicious character was included in an HTML attribute name",
                       current_attribute_.name + ch);
            }
            current_attribute_.name.push_back(ascii_lower(ch));
        }
    }

    void after_attribute_name_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-tag-unclosed",
                   "Tag reached EOF after an attribute name",
                   current_token_.name);
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            return;
        }
        if (ch == '/') {
            finish_attribute();
            state_ = State::SelfClosingStartTag;
        } else if (ch == '=') {
            state_ = State::BeforeAttributeValue;
        } else if (ch == '>') {
            finish_attribute();
            emit_current_token();
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-missing-attribute-value",
                   "Attribute without a value was kept as a boolean attribute",
                   current_attribute_.name);
            begin_attribute();
            reconsume_in(State::AttributeName);
        }
    }

    void before_attribute_value_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-attribute-value-unclosed",
                   "Tag reached EOF before an attribute value",
                   current_attribute_.name);
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            return;
        }
        if (ch == '"') {
            state_ = State::AttributeValueDoubleQuoted;
        } else if (ch == '\'') {
            state_ = State::AttributeValueSingleQuoted;
        } else if (ch == '>') {
            report(DiagnosticSeverity::Warning,
                   "html-missing-attribute-value",
                   "Attribute value was missing before tag close",
                   current_attribute_.name);
            finish_attribute();
            emit_current_token();
        } else {
            reconsume_in(State::AttributeValueUnquoted);
        }
    }

    void attribute_value_quoted_state(char quote) {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-quoted-attribute-unclosed",
                   "Quoted attribute value reached EOF before its closing quote",
                   std::string(1, quote) + current_attribute_.name);
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (ch == quote) {
            state_ = State::AfterAttributeValueQuoted;
        } else if (ch == '&') {
            current_attribute_.value += consume_character_reference();
        } else if (ch == '\0') {
            report(DiagnosticSeverity::Warning,
                   "html-null-character",
                   "Null character in attribute value was replaced with U+FFFD",
                   current_attribute_.name);
            current_attribute_.value += kReplacement;
        } else {
            current_attribute_.value.push_back(ch);
        }
    }

    void attribute_value_unquoted_state() {
        if (eof()) {
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            finish_attribute();
            state_ = State::BeforeAttributeName;
        } else if (ch == '&') {
            current_attribute_.value += consume_character_reference();
        } else if (ch == '>') {
            finish_attribute();
            emit_current_token();
        } else if (ch == '\0') {
            report(DiagnosticSeverity::Warning,
                   "html-null-character",
                   "Null character in unquoted attribute value was replaced with U+FFFD",
                   current_attribute_.name);
            current_attribute_.value += kReplacement;
        } else {
            if (ch == '"' || ch == '\'' || ch == '<' || ch == '=' || ch == '`') {
                report(DiagnosticSeverity::Warning,
                       "html-unquoted-attribute-value-suspicious-char",
                       "Suspicious character was kept inside an unquoted attribute value",
                       current_attribute_.name + "=" + current_attribute_.value + ch);
            }
            current_attribute_.value.push_back(ch);
        }
    }

    void after_attribute_value_quoted_state() {
        if (eof()) {
            finish_attribute();
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (is_ascii_space(ch)) {
            finish_attribute();
            state_ = State::BeforeAttributeName;
        } else if (ch == '/') {
            finish_attribute();
            state_ = State::SelfClosingStartTag;
        } else if (ch == '>') {
            finish_attribute();
            emit_current_token();
        } else {
            finish_attribute();
            report(DiagnosticSeverity::Warning,
                   "html-missing-space-after-attribute",
                   "Missing whitespace after quoted attribute value was recovered",
                   snippet_at(index_ > 0 ? index_ - 1 : 0));
            reconsume_in(State::BeforeAttributeName);
        }
    }

    void self_closing_start_tag_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-tag-unclosed",
                   "Self-closing tag reached EOF before '>'",
                   current_token_.name);
            emit_current_token();
            state_ = State::EndOfFile;
            return;
        }

        const char ch = consume();
        if (ch == '>') {
            current_token_.self_closing = true;
            emit_current_token();
        } else {
            report(DiagnosticSeverity::Warning,
                   "html-invalid-self-closing-tag",
                   "Unexpected characters after '/' in a tag were parsed as attributes",
                   snippet_at(index_ > 0 ? index_ - 1 : 0));
            reconsume_in(State::BeforeAttributeName);
        }
    }

    void raw_text_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-raw-text-unclosed",
                   "Raw-text element reached EOF before its end tag",
                   raw_text_end_tag_);
            state_ = State::EndOfFile;
            return;
        }

        if (peek() == '<' && starts_with_raw_text_end_tag(source_, index_, raw_text_end_tag_)) {
            flush_text();
            index_ += 2;
            begin_token(HtmlTokenType::EndTag);
            state_ = State::TagName;
            return;
        }

        append_text_char(consume());
    }

    void rcdata_state() {
        if (eof()) {
            report(DiagnosticSeverity::Warning,
                   "html-rcdata-unclosed",
                   "RCDATA element reached EOF before its end tag",
                   raw_text_end_tag_);
            state_ = State::EndOfFile;
            return;
        }

        if (peek() == '<' && starts_with_raw_text_end_tag(source_, index_, raw_text_end_tag_)) {
            flush_text();
            index_ += 2;
            begin_token(HtmlTokenType::EndTag);
            state_ = State::TagName;
            return;
        }

        const char ch = consume();
        if (ch == '&') {
            text_buffer_ += consume_character_reference();
        } else {
            append_text_char(ch);
        }
    }

    void consume_comment() {
        const std::size_t end = source_.find("-->", index_);
        std::string data;
        if (end == std::string::npos) {
            report(DiagnosticSeverity::Warning,
                   "html-comment-unclosed",
                   "HTML comment reached EOF before '-->'",
                   snippet_at(index_));
            data = std::string(source_.substr(index_));
            index_ = source_.size();
        } else {
            data = std::string(source_.substr(index_, end - index_));
            index_ = end + 3;
        }
        if (options_.emit_comments) {
            HtmlToken token;
            token.type = HtmlTokenType::Comment;
            token.data = std::move(data);
            emit_token(token);
        }
    }

    void consume_doctype() {
        const std::size_t begin = index_;
        const std::size_t end = source_.find('>', index_);
        if (end == std::string::npos) {
            report(DiagnosticSeverity::Warning,
                   "html-doctype-unclosed",
                   "DOCTYPE reached EOF before '>'",
                   snippet_at(begin));
            index_ = source_.size();
        } else {
            index_ = end + 1;
        }
        HtmlToken token;
        token.type = HtmlTokenType::Doctype;
        token.name = ascii_lowercase(trim_ascii(source_.substr(begin, (end == std::string::npos ? index_ : end) - begin)));
        emit_token(token);
    }

    void consume_cdata_as_text() {
        const std::size_t end = source_.find("]]>", index_);
        if (end == std::string::npos) {
            report(DiagnosticSeverity::Warning,
                   "html-cdata-unclosed",
                   "CDATA section reached EOF before ']]>'",
                   snippet_at(index_));
            const std::string_view remaining = source_.substr(index_);
            text_buffer_.append(remaining.data(), remaining.size());
            index_ = source_.size();
        } else {
            const std::string_view data = source_.substr(index_, end - index_);
            text_buffer_.append(data.data(), data.size());
            index_ = end + 3;
        }
    }

    void consume_bogus_comment() {
        const std::size_t begin = index_;
        consume_until_tag_end();
        if (options_.emit_comments && index_ > begin) {
            HtmlToken token;
            token.type = HtmlTokenType::Comment;
            token.data = std::string(source_.substr(begin, index_ - begin));
            emit_token(token);
        }
    }

    void consume_until_tag_end() {
        while (!eof() && peek() != '>') {
            consume();
        }
        if (!eof() && peek() == '>') {
            consume();
        }
    }

    static std::string trim_ascii(std::string_view value) {
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

    void emit_token(const HtmlToken& token) {
        sink_.consume(token);
    }

    std::string_view source_;
    HtmlTokenSink& sink_;
    HtmlTokenizerOptions options_;
    std::size_t index_ = 0;
    State state_ = State::Data;
    std::string text_buffer_;
    HtmlToken current_token_;
    HtmlAttribute current_attribute_;
    bool has_current_attribute_ = false;
    std::string raw_text_end_tag_;
};

} // namespace

class VectorTokenSink final : public HtmlTokenSink {
public:
    void consume(const HtmlToken& token) override {
        tokens.push_back(token);
    }

    std::vector<HtmlToken> tokens;
};

std::vector<HtmlToken> HtmlTokenizer::tokenize(const std::string& source,
                                               const HtmlTokenizerOptions& options) const {
    VectorTokenSink sink;
    tokenize_to_sink(source, sink, options);
    return std::move(sink.tokens);
}

void HtmlTokenizer::tokenize_to_sink(const std::string& source,
                                     HtmlTokenSink& sink,
                                     const HtmlTokenizerOptions& options) const {
    if (source.find('\r') == std::string::npos) {
        TokenizerRun run(source, sink, options);
        run.run();
        return;
    }

    const std::string normalized = normalize_newlines(source);
    TokenizerRun run(normalized, sink, options);
    run.run();
}

} // namespace jellyframe
