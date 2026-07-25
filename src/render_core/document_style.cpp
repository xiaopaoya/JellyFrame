#include "render_core/document_style.h"

#include <cctype>
#include <vector>

namespace jellyframe {
namespace {

void append_text_descendants(const Node& node, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            output.append(current->text);
            output.push_back('\n');
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

bool ascii_equal_case_insensitive(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
        std::tolower(static_cast<unsigned char>(right));
}

bool token_equals(std::string_view token, std::string_view expected) {
    if (token.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (!ascii_equal_case_insensitive(token[index], expected[index])) {
            return false;
        }
    }
    return true;
}

bool rel_has_stylesheet(std::string_view rel) {
    std::size_t index = 0;
    while (index < rel.size()) {
        while (index < rel.size() && std::isspace(static_cast<unsigned char>(rel[index])) != 0) {
            ++index;
        }
        const std::size_t begin = index;
        while (index < rel.size() && std::isspace(static_cast<unsigned char>(rel[index])) == 0) {
            ++index;
        }
        if (begin != index && token_equals(rel.substr(begin, index - begin), "stylesheet")) {
            return true;
        }
    }
    return false;
}

struct CssCollectionState {
    const DocumentStyleCollectionOptions& options;
    std::string output;
    std::size_t stylesheets = 0;
    bool limit_reported = false;
};

void report_collection_limit(CssCollectionState& state, std::string_view detail) {
    if (state.limit_reported) {
        return;
    }
    report_diagnostic(state.options.diagnostics,
                      DiagnosticStage::Css,
                      DiagnosticSeverity::Warning,
                      "css-document-resource-limit",
                      "Document stylesheet collection reached its resource budget; later stylesheets were skipped",
                      detail);
    state.limit_reported = true;
}

bool append_css_chunk(CssCollectionState& state, const std::string& css, std::string_view source) {
    if (css.empty()) {
        return true;
    }
    if (state.stylesheets >= state.options.max_stylesheets) {
        report_collection_limit(state, "stylesheet count limit at " + std::string(source));
        return false;
    }
    const bool needs_leading_newline = !state.output.empty() && state.output.back() != '\n';
    const bool needs_trailing_newline = css.back() != '\n';
    const std::size_t separators = static_cast<std::size_t>(needs_leading_newline) +
        static_cast<std::size_t>(needs_trailing_newline);
    if (state.output.size() > state.options.max_total_bytes ||
        css.size() > state.options.max_total_bytes - state.output.size() ||
        separators > state.options.max_total_bytes - state.output.size() - css.size()) {
        report_collection_limit(state, "stylesheet byte limit at " + std::string(source));
        return false;
    }
    if (needs_leading_newline) {
        state.output.push_back('\n');
    }
    state.output.append(css);
    if (needs_trailing_newline) {
        state.output.push_back('\n');
    }
    ++state.stylesheets;
    return true;
}

void collect_document_author_css(const Node& node,
                                 CssCollectionState& state,
                                 StylesheetLoadCallback load_stylesheet,
                                 void* context) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "link" &&
            rel_has_stylesheet(current->attribute("rel")) && !current->attribute("href").empty() &&
            load_stylesheet != nullptr) {
            std::string linked_css;
            if (load_stylesheet(current->attribute("href"), linked_css, context)) {
                if (!append_css_chunk(state, linked_css, current->attribute("href"))) {
                    break;
                }
            }
            continue;
        }
        if (current->type == NodeType::Element && current->tag_name == "style") {
            std::string embedded_css;
            append_text_descendants(*current, embedded_css);
            if (!append_css_chunk(state, embedded_css, "(inline style)")) {
                break;
            }
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

} // namespace

std::string collect_embedded_style_text(const Node& document) {
    return combine_author_css({}, document);
}

std::string combine_author_css(const std::string& external_css, const Node& document) {
    return combine_author_css(external_css, document, DocumentStyleCollectionOptions{});
}

std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               const DocumentStyleCollectionOptions& options) {
    return combine_author_css(external_css, document, nullptr, nullptr, options);
}

std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context) {
    return combine_author_css(external_css, document, load_stylesheet, context, DocumentStyleCollectionOptions{});
}

std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context,
                               const DocumentStyleCollectionOptions& options) {
    CssCollectionState state{options, {}, 0, false};
    if (!append_css_chunk(state, external_css, "(external stylesheet)")) {
        return state.output;
    }
    collect_document_author_css(document, state, load_stylesheet, context);
    return state.output;
}

} // namespace jellyframe
