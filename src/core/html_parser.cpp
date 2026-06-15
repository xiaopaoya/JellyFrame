#include "core/html_parser.h"

#include "core/html_tokenizer.h"
#include "core/html_tree_builder.h"

namespace jellyframe {

std::unique_ptr<Node> HtmlParser::parse(const std::string& source) const {
    return parse(source, HtmlParserOptions{});
}

std::unique_ptr<Node> HtmlParser::parse(const std::string& source, const HtmlParserOptions& options) const {
    auto root = make_element("document");
    HtmlTokenizer tokenizer;
    HtmlTreeBuilder tree_builder(*root, options);
    tokenizer.tokenize_to_sink(source, tree_builder);
    clear_dirty_flags(*root);
    return root;
}

} // namespace jellyframe
