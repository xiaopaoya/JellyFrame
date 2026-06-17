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
    tokenizer.tokenize_to_sink(source, tree_builder);
    clear_dirty_flags(*root);
    return HtmlParseResult{std::move(root), tree_builder.diagnostics()};
}

} // namespace jellyframe
