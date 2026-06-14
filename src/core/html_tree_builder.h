#pragma once

#include "core/dom.h"
#include "core/html_parser.h"
#include "core/html_tokenizer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wearweb {

class HtmlTreeBuilder final : public HtmlTokenSink {
public:
    HtmlTreeBuilder(Node& document, const HtmlParserOptions& options);

    void consume(const HtmlToken& token) override;

private:
    bool can_add_node() const;
    bool can_descend() const;
    Node& append_element(Node& parent, const HtmlToken& token);
    Node& append_synthetic_element(Node& parent, std::string_view tag_name);
    Node* find_open_element(std::string_view tag_name) const;
    bool has_open_element(std::string_view tag_name) const;
    void pop_until(std::string_view tag_name);
    void pop_current_if(std::string_view tag_name);
    Node& ensure_html();
    Node& ensure_head();
    Node& ensure_body();
    void start_tag(const HtmlToken& token);
    void start_html(const HtmlToken& token);
    void start_head(const HtmlToken& token);
    void start_body(const HtmlToken& token);
    void append_to_head(const HtmlToken& token);
    void merge_attributes(Node& node, const HtmlToken& token);
    void apply_common_implied_end_tags(std::string_view incoming_tag);
    void pop_until_if_current_or_ancestor(std::string_view tag_name,
                                          std::initializer_list<std::string_view> stop_tags);
    void end_tag(std::string_view tag_name);
    void text(std::string_view data);

    Node& document_;
    const HtmlParserOptions& options_;
    std::vector<Node*> open_elements_;
    Node* html_ = nullptr;
    Node* head_ = nullptr;
    Node* body_ = nullptr;
    std::size_t node_count_ = 1;
};

} // namespace wearweb

