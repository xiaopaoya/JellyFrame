#include "render_core/html_parser.h"
#include "render_core/html_tokenizer.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace jellyframe;

namespace {

class CollectingTokenSink final : public HtmlTokenSink {
public:
    void consume(const HtmlToken& token) override {
        tokens.push_back(token);
    }

    std::vector<HtmlToken> tokens;
};

std::vector<HtmlToken> tokenize(const std::string& source) {
    HtmlTokenizer tokenizer;
    return tokenizer.tokenize(source);
}

const HtmlToken& token_at(const std::vector<HtmlToken>& tokens, std::size_t index, HtmlTokenType type) {
    if (index >= tokens.size()) {
        throw std::runtime_error("token index out of range");
    }
    if (tokens[index].type != type) {
        throw std::runtime_error("unexpected token type");
    }
    return tokens[index];
}

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t count_nodes(const Node& node) {
    std::size_t total = 1;
    for (const auto& child : node.children) {
        total += count_nodes(*child);
    }
    return total;
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

void parses_common_document_shell() {
    const auto tokens = tokenize("<!doctype html><div class=\"a\" disabled data-x=1>&amp;&#x41;</div>");

    check(token_at(tokens, 0, HtmlTokenType::Doctype).name == "html", "doctype name");

    const HtmlToken& div = token_at(tokens, 1, HtmlTokenType::StartTag);
    check(div.name == "div", "div start tag");
    check(div.attributes.size() == 3, "div attribute count");
    check(div.attributes[0].name == "class", "class attribute name");
    check(div.attributes[0].value == "a", "class attribute value");
    check(div.attributes[1].name == "disabled", "boolean attribute name");
    check(div.attributes[1].value.empty(), "boolean attribute value");
    check(div.attributes[2].name == "data-x", "data attribute name");
    check(div.attributes[2].value == "1", "data attribute value");

    check(token_at(tokens, 2, HtmlTokenType::Text).data == "&A", "entity text");
    check(token_at(tokens, 3, HtmlTokenType::EndTag).name == "div", "div end tag");
    token_at(tokens, 4, HtmlTokenType::EndOfFile);
}

void keeps_malformed_less_than_as_text() {
    const auto tokens = tokenize("2 < 3 && 5 > 4");
    check(token_at(tokens, 0, HtmlTokenType::Text).data == "2 < 3 && 5 > 4", "malformed less-than text");
    token_at(tokens, 1, HtmlTokenType::EndOfFile);
}

void treats_script_as_raw_text() {
    const auto tokens = tokenize("<script>if (a < b) { view.innerHTML = \"<div>\"; }</script><p>ok</p>");
    check(token_at(tokens, 0, HtmlTokenType::StartTag).name == "script", "script start tag");
    check(token_at(tokens, 1, HtmlTokenType::Text).data == "if (a < b) { view.innerHTML = \"<div>\"; }",
          "script raw text");
    check(token_at(tokens, 2, HtmlTokenType::EndTag).name == "script", "script end tag");
    check(token_at(tokens, 3, HtmlTokenType::StartTag).name == "p", "post-script p tag");
}

void treats_style_as_raw_text() {
    const auto tokens = tokenize("<style>.x::before { content: \"<\"; }</style><div>x</div>");
    check(token_at(tokens, 0, HtmlTokenType::StartTag).name == "style", "style start tag");
    check(token_at(tokens, 1, HtmlTokenType::Text).data == ".x::before { content: \"<\"; }", "style raw text");
    check(token_at(tokens, 2, HtmlTokenType::EndTag).name == "style", "style end tag");
    check(token_at(tokens, 3, HtmlTokenType::StartTag).name == "div", "post-style div tag");
}

void treats_textarea_and_title_as_rcdata() {
    const auto tokens = tokenize("<title>A &amp; B</title><textarea>x &lt; y</textarea>");
    check(token_at(tokens, 0, HtmlTokenType::StartTag).name == "title", "title start tag");
    check(token_at(tokens, 1, HtmlTokenType::Text).data == "A & B", "title decodes character references");
    check(token_at(tokens, 2, HtmlTokenType::EndTag).name == "title", "title end tag");
    check(token_at(tokens, 3, HtmlTokenType::StartTag).name == "textarea", "textarea start tag");
    check(token_at(tokens, 4, HtmlTokenType::Text).data == "x < y", "textarea decodes character references");
    check(token_at(tokens, 5, HtmlTokenType::EndTag).name == "textarea", "textarea end tag");
}

void decodes_common_named_and_numeric_references() {
    const auto tokens = tokenize("&hellip;&mdash;&ldquo;x&rdquo;&#x80;");
    check(token_at(tokens, 0, HtmlTokenType::Text).data ==
              "\xE2\x80\xA6\xE2\x80\x94\xE2\x80\x9Cx\xE2\x80\x9D\xE2\x82\xAC",
          "common named and numeric references decode");
}

void keeps_first_duplicate_attribute() {
    const auto tokens = tokenize("<input VALUE=first value=second disabled>");
    const HtmlToken& input = token_at(tokens, 0, HtmlTokenType::StartTag);
    check(input.name == "input", "input start tag");
    check(input.attributes.size() == 2, "duplicate attribute count");
    check(input.attributes[0].name == "value", "duplicate attribute name");
    check(input.attributes[0].value == "first", "duplicate attribute first value");
    check(input.attributes[1].name == "disabled", "attribute after duplicate");
}

void tokenizer_attribute_budget_consumes_extra_attributes_without_storing_them() {
    HtmlTokenizer tokenizer;
    HtmlTokenizerOptions options;
    VectorDiagnosticSink diagnostics;
    options.diagnostics = &diagnostics;
    options.max_attributes_per_tag = 2;

    const auto tokens = tokenizer.tokenize("<div a=1 b=2 c=3 d=4></div>", options);
    const HtmlToken& div = token_at(tokens, 0, HtmlTokenType::StartTag);
    check(div.attributes.size() == 2, "tokenizer caps stored attributes");
    check(div.attributes[0].name == "a" && div.attributes[1].name == "b", "tokenizer keeps first attributes");
    check(has_diagnostic_code(diagnostics, "html-tokenizer-attribute-limit"),
          "tokenizer reports attribute cap");
    check(token_at(tokens, 1, HtmlTokenType::EndTag).name == "div", "tokenizer still reaches end tag");
}

void tokenizer_byte_budgets_consume_oversized_names_and_values_without_storing_them() {
    HtmlTokenizer tokenizer;
    HtmlTokenizerOptions options;
    VectorDiagnosticSink diagnostics;
    options.diagnostics = &diagnostics;
    options.max_tag_name_bytes = 3;
    options.max_attribute_name_bytes = 2;
    options.max_attribute_value_bytes = 3;

    const auto tokens = tokenizer.tokenize("<section abcdef=123456 next=ok></section>", options);
    const HtmlToken& start = token_at(tokens, 0, HtmlTokenType::StartTag);
    check(start.name == "sec", "tokenizer truncates an oversized tag name");
    check(start.attributes.size() == 2, "tokenizer continues after oversized attribute fields");
    check(start.attributes[0].name == "ab" && start.attributes[0].value == "123",
          "tokenizer truncates oversized attribute name and value");
    check(start.attributes[1].name == "ne" && start.attributes[1].value == "ok",
          "tokenizer resets byte budgets for the next attribute");
    check(has_diagnostic_code(diagnostics, "html-tokenizer-tag-name-byte-limit"),
          "tokenizer reports tag-name byte cap");
    check(has_diagnostic_code(diagnostics, "html-tokenizer-attribute-name-byte-limit"),
          "tokenizer reports attribute-name byte cap");
    check(has_diagnostic_code(diagnostics, "html-tokenizer-attribute-value-byte-limit"),
          "tokenizer reports attribute-value byte cap");
    check(token_at(tokens, 1, HtmlTokenType::EndTag).name == "sec",
          "tokenizer still reaches the matching end tag");
}

void tokenizer_text_budget_consumes_excess_text_without_storing_it() {
    HtmlTokenizer tokenizer;
    HtmlTokenizerOptions options;
    VectorDiagnosticSink diagnostics;
    options.diagnostics = &diagnostics;
    options.max_text_token_bytes = 4;

    const auto tokens = tokenizer.tokenize("<div>abcdef<![CDATA[ghijkl]]></div>", options);
    const auto text_it = std::find_if(tokens.begin(), tokens.end(), [](const HtmlToken& token) {
        return token.type == HtmlTokenType::Text;
    });
    check(text_it != tokens.end(), "tokenizer emits bounded text");
    const HtmlToken& text = *text_it;
    check(text.data == "abcd", "tokenizer bounds text and CDATA storage");
    check(has_diagnostic_code(diagnostics, "html-text-token-limit"), "tokenizer reports text cap");
    const auto end_it = std::find_if(tokens.begin(), tokens.end(), [](const HtmlToken& token) {
        return token.type == HtmlTokenType::EndTag && token.name == "div";
    });
    check(end_it != tokens.end(), "tokenizer continues after text cap");
}

void parser_consumes_token_stream() {
    HtmlParser parser;
    auto document = parser.parse("<!doctype html><body><p>Hello &amp; welcome</p><br><p>again</p></body>");
    check(document->children.size() == 1, "document child count");
    const Node& html = *document->children[0];
    check(html.tag_name == "html", "html element");
    check(html.children.size() == 1, "html child count");
    const Node& body = *html.children[0];
    check(body.tag_name == "body", "body element");
    check(body.children.size() == 3, "body child count");
    check(body.children[0]->tag_name == "p", "first p");
    check(body.children[0]->children[0]->text == "Hello & welcome", "decoded parser text");
    check(body.children[1]->tag_name == "br", "void br");
    check(body.children[2]->tag_name == "p", "second p");
}

void parser_treats_non_void_self_closing_as_start_tag() {
    HtmlParser parser;
    auto document = parser.parse("<body><div/><span>x</span></body>");
    const Node& body = *document->children[0]->children[0];
    check(body.children.size() == 1, "span stays inside self-closing div in HTML mode");
    check(body.children[0]->tag_name == "div", "div created");
    check(body.children[0]->children.size() == 1, "div remains open");
    check(body.children[0]->children[0]->tag_name == "span", "span child inside div");
}

void parser_preserves_dom_text_whitespace() {
    HtmlParser parser;
    auto document = parser.parse("<body><p>a <b>x</b>  b</p></body>");
    const Node& p = *document->children[0]->children[0]->children[0];
    check(p.children.size() == 3, "p has text, b, text");
    check(p.children[0]->text == "a ", "leading text preserves trailing space");
    check(p.children[2]->text == "  b", "trailing text preserves repeated spaces");
}

void parser_applies_common_implied_end_tags() {
    HtmlParser parser;
    auto document = parser.parse("<ul><li>one<li>two</ul><p>a<div>b</div>");
    const Node& body = *document->children[0]->children[0];
    const Node& ul = *body.children[0];
    check(ul.tag_name == "ul", "ul element");
    check(ul.children.size() == 2, "li autoclose count");
    check(ul.children[0]->tag_name == "li", "first li");
    check(ul.children[1]->tag_name == "li", "second li");
    check(body.children[1]->tag_name == "p", "p before block");
    check(body.children[2]->tag_name == "div", "block after p");
}

void parser_respects_resource_limits() {
    HtmlParser parser;
    HtmlParserOptions options;
    options.max_nodes = 5;
    options.max_depth = 4;

    auto document = parser.parse("<div><div><div><div><span>too deep</span></div></div></div></div>", options);
    check(document->children.size() == 1, "limited document still has html");
}

void parser_reports_resource_limit_diagnostics() {
    HtmlParser parser;
    HtmlParserOptions options;
    options.max_nodes = 5;
    options.max_depth = 4;
    options.max_attributes_per_element = 1;

    HtmlParseResult result =
        parser.parse_with_diagnostics("<div a=1 b=2><div><div><div><span>too deep</span></div></div></div></div>", options);
    check(result.document != nullptr, "diagnostic parse returns document");
    check((result.diagnostics & HtmlParserDiagnosticNodeLimit) != 0U, "node limit diagnostic");
    check((result.diagnostics & HtmlParserDiagnosticDepthLimit) != 0U, "depth limit diagnostic");
    check((result.diagnostics & HtmlParserDiagnosticAttributeLimit) != 0U, "attribute limit diagnostic");
}

void parser_node_budget_caps_synthetic_and_authored_nodes_together() {
    HtmlParser parser;
    HtmlParserOptions options;
    options.max_nodes = 3;

    auto document = parser.parse("<div><span><em>x</em></span></div>", options);
    check(count_nodes(*document) == 3, "synthetic shell and authored nodes share one node budget");
    check(document->children.size() == 1, "document still contains the synthetic html root");
    check(document->children[0]->tag_name == "html", "synthetic html is retained first");
}

void parser_reports_tokenizer_and_tree_recovery_diagnostics() {
    HtmlParser parser;
    HtmlParserOptions options;
    VectorDiagnosticSink diagnostics;
    options.diagnostics = &diagnostics;

    HtmlParseResult result = parser.parse_with_diagnostics(
        "<body><div a=1 a=2 bad<'x'><span/>Text &notareal;</missing><script>unterminated",
        options);

    check(result.document != nullptr, "diagnostic parser returns document for malformed markup");
    check(has_diagnostic_code(diagnostics, "html-duplicate-attribute"), "duplicate attribute is reported");
    check(has_diagnostic_code(diagnostics, "html-suspicious-attribute-name"), "suspicious attribute name is reported");
    check(has_diagnostic_code(diagnostics, "html-character-reference-unknown"), "unknown character reference is reported");
    check(has_diagnostic_code(diagnostics, "html-unmatched-end-tag"), "unmatched end tag is reported");
    check(has_diagnostic_code(diagnostics, "html-non-void-self-closing"), "non-void self closing recovery is reported");
    check(has_diagnostic_code(diagnostics, "html-raw-text-unclosed"), "unclosed raw text is reported");
}

void parser_malformed_corpus_stays_within_budgets() {
    static constexpr std::array<std::string_view, 9> kCorpus = {
        "<",
        "</",
        "<!-- unterminated comment",
        "<![CDATA[unterminated",
        "<div data-state='unterminated><span>text",
        "<script>if (value < 3) { view.innerHTML = '<div>'; }",
        "<body><div a=1 a=2 bad<'x'><span/><missing></body>",
        "<ul><li><li><li><div><p><section>deep",
        "<style>.x { content: '<'; } .y { color: red; }",
    };

    HtmlParserOptions options;
    options.max_nodes = 24;
    options.max_depth = 6;
    options.max_attributes_per_element = 4;
    options.max_tag_name_bytes = 16;
    options.max_attribute_name_bytes = 16;
    options.max_attribute_value_bytes = 32;
    options.max_text_token_bytes = 64;

    HtmlParser parser;
    for (const std::string_view source : kCorpus) {
        const HtmlParseResult result = parser.parse_with_diagnostics(std::string(source), options);
        check(result.document != nullptr, "malformed corpus still returns a document");
        const DomStatistics statistics = compute_dom_statistics(*result.document);
        check(statistics.node_count <= options.max_nodes, "malformed corpus keeps the node budget");
        check(statistics.max_depth <= options.max_depth, "malformed corpus keeps the depth budget");
        check(statistics.max_attributes_per_element <= options.max_attributes_per_element,
              "malformed corpus keeps the attribute budget");
    }
}

void streaming_tokenizer_matches_vector_tokenizer() {
    const std::string source = "<div a=1>&amp;<script>x < y</script></div>";
    HtmlTokenizer tokenizer;
    const std::vector<HtmlToken> vector_tokens = tokenizer.tokenize(source);
    CollectingTokenSink sink;
    tokenizer.tokenize_to_sink(source, sink);

    check(vector_tokens.size() == sink.tokens.size(), "stream token count");
    for (std::size_t i = 0; i < vector_tokens.size(); ++i) {
        check(vector_tokens[i].type == sink.tokens[i].type, "stream token type");
        check(vector_tokens[i].name == sink.tokens[i].name, "stream token name");
        check(vector_tokens[i].data == sink.tokens[i].data, "stream token data");
        check(vector_tokens[i].attributes.size() == sink.tokens[i].attributes.size(), "stream attribute count");
    }
}

} // namespace

int main() {
    try {
        parses_common_document_shell();
        keeps_malformed_less_than_as_text();
        treats_script_as_raw_text();
        treats_style_as_raw_text();
        treats_textarea_and_title_as_rcdata();
        decodes_common_named_and_numeric_references();
        keeps_first_duplicate_attribute();
        tokenizer_attribute_budget_consumes_extra_attributes_without_storing_them();
        tokenizer_byte_budgets_consume_oversized_names_and_values_without_storing_them();
        tokenizer_text_budget_consumes_excess_text_without_storing_it();
        parser_consumes_token_stream();
        parser_treats_non_void_self_closing_as_start_tag();
        parser_preserves_dom_text_whitespace();
        parser_applies_common_implied_end_tags();
        parser_respects_resource_limits();
        parser_reports_resource_limit_diagnostics();
        parser_node_budget_caps_synthetic_and_authored_nodes_together();
        parser_reports_tokenizer_and_tree_recovery_diagnostics();
        parser_malformed_corpus_stays_within_budgets();
        streaming_tokenizer_matches_vector_tokenizer();
    } catch (const std::exception& error) {
        std::cerr << "tokenizer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "tokenizer tests passed\n";
    return 0;
}
