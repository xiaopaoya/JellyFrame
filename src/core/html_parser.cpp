#include "core/html_parser.h"

#include "core/html_tokenizer.h"
#include "core/html_tree_builder.h"

namespace jellyframe {

std::unique_ptr<Node> HtmlParser::parse(const std::string& source) const {
    return parse(source, HtmlParserOptions{});
}

std::unique_ptr<Node> HtmlParser::parse(const std::string& source, const HtmlParserOptions& options) const {
    return parse_with_diagnostics(source, options).document;
}

HtmlParseResult HtmlParser::parse_with_diagnostics(const std::string& source) const {
    return parse_with_diagnostics(source, HtmlParserOptions{});
}

HtmlParseResult HtmlParser::parse_with_diagnostics(const std::string& source, const HtmlParserOptions& options) const {
    auto root = make_element("document");
    HtmlTokenizer tokenizer;
    HtmlTreeBuilder tree_builder(*root, options);
    HtmlTokenizerOptions tokenizer_options;
    tokenizer_options.diagnostics = options.diagnostics;
    tokenizer.tokenize_to_sink(source, tree_builder, tokenizer_options);
    clear_dirty_flags(*root);
    const HtmlParserDiagnosticFlags flags = tree_builder.diagnostics();
    if ((flags & HtmlParserDiagnosticNodeLimit) != 0U) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::Html,
                          DiagnosticSeverity::Warning,
                          "html-node-limit",
                          "DOM node budget was reached; remaining nodes were dropped",
                          "Increase max_dom_nodes for richer pages, or simplify generated markup.");
    }
    if ((flags & HtmlParserDiagnosticDepthLimit) != 0U) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::Html,
                          DiagnosticSeverity::Warning,
                          "html-depth-limit",
                          "DOM nesting depth budget was reached; deeper descendants were dropped",
                          "Deep compatibility markup is expensive on MCU targets and is intentionally capped.");
    }
    if ((flags & HtmlParserDiagnosticAttributeLimit) != 0U) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::Html,
                          DiagnosticSeverity::Warning,
                          "html-attribute-limit",
                          "Element attribute budget was reached; extra attributes were ignored",
                          "Keep runtime-relevant state in a small number of attributes or data-* fields.");
    }
    return HtmlParseResult{std::move(root), flags};
}

} // namespace jellyframe
