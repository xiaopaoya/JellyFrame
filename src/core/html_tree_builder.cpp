#include "core/html_tree_builder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>

namespace wearweb {
namespace {

template <std::size_t Size>
bool contains_name(const std::array<std::string_view, Size>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool contains_name(std::initializer_list<std::string_view> names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool is_void_element(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 14> kVoidElements = {
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link",
        "meta", "param", "source", "track", "wbr"
    };
    return contains_name(kVoidElements, tag_name);
}

bool is_metadata_element(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 8> kMetadataElements = {
        "base", "link", "meta", "noscript", "script", "style", "template", "title"
    };
    return contains_name(kMetadataElements, tag_name);
}

bool is_block_start(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 31> kBlockElements = {
        "address", "article", "aside", "blockquote", "details", "dialog", "div",
        "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
        "h3", "h4", "h5", "h6", "header", "hr", "main", "menu", "nav", "ol",
        "p", "pre", "section", "table", "ul", "video"
    };
    return contains_name(kBlockElements, tag_name);
}

bool preserves_text(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 5> kPreservingElements = {
        "pre", "script", "style", "textarea", "title"
    };
    return contains_name(kPreservingElements, tag_name);
}

std::string collapse_tree_text(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    bool last_was_space = false;
    for (const char ch : value) {
        const bool is_space = std::isspace(static_cast<unsigned char>(ch)) != 0;
        if (is_space) {
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

} // namespace

HtmlTreeBuilder::HtmlTreeBuilder(Node& document, const HtmlParserOptions& options)
    : document_(document), options_(options) {
    open_elements_.reserve(std::min<std::size_t>(options_.max_depth, 64));
    open_elements_.push_back(&document_);
}

void HtmlTreeBuilder::consume(const HtmlToken& token) {
    switch (token.type) {
    case HtmlTokenType::StartTag:
        start_tag(token);
        break;
    case HtmlTokenType::EndTag:
        end_tag(token.name);
        break;
    case HtmlTokenType::Text:
        text(token.data);
        break;
    case HtmlTokenType::Doctype:
    case HtmlTokenType::Comment:
    case HtmlTokenType::EndOfFile:
        break;
    }
}

bool HtmlTreeBuilder::can_add_node() const {
    return node_count_ < options_.max_nodes;
}

bool HtmlTreeBuilder::can_descend() const {
    return open_elements_.size() < options_.max_depth;
}

Node& HtmlTreeBuilder::append_element(Node& parent, const HtmlToken& token) {
    auto element = make_element(token.name);
    std::size_t count = 0;
    for (const HtmlAttribute& attribute : token.attributes) {
        if (count >= options_.max_attributes_per_element) {
            break;
        }
        element->attributes.emplace(attribute.name, attribute.value);
        ++count;
    }
    ++node_count_;
    return parent.append_child(std::move(element));
}

Node& HtmlTreeBuilder::append_synthetic_element(Node& parent, std::string_view tag_name) {
    ++node_count_;
    return parent.append_child(make_element(std::string(tag_name)));
}

Node* HtmlTreeBuilder::find_open_element(std::string_view tag_name) const {
    for (auto it = open_elements_.rbegin(); it != open_elements_.rend(); ++it) {
        if ((*it)->tag_name == tag_name) {
            return *it;
        }
    }
    return nullptr;
}

bool HtmlTreeBuilder::has_open_element(std::string_view tag_name) const {
    return find_open_element(tag_name) != nullptr;
}

void HtmlTreeBuilder::pop_until(std::string_view tag_name) {
    while (open_elements_.size() > 1) {
        const std::string_view current = open_elements_.back()->tag_name;
        open_elements_.pop_back();
        if (current == tag_name) {
            return;
        }
    }
}

void HtmlTreeBuilder::pop_current_if(std::string_view tag_name) {
    if (open_elements_.size() > 1 && open_elements_.back()->tag_name == tag_name) {
        open_elements_.pop_back();
    }
}

Node& HtmlTreeBuilder::ensure_html() {
    if (html_ != nullptr) {
        return *html_;
    }
    if (!can_add_node()) {
        return document_;
    }
    html_ = &append_synthetic_element(document_, "html");
    open_elements_.push_back(html_);
    return *html_;
}

Node& HtmlTreeBuilder::ensure_head() {
    Node& html = ensure_html();
    if (head_ != nullptr) {
        return *head_;
    }
    if (!can_add_node()) {
        return html;
    }
    head_ = &append_synthetic_element(html, "head");
    return *head_;
}

Node& HtmlTreeBuilder::ensure_body() {
    Node& html = ensure_html();
    if (body_ != nullptr) {
        return *body_;
    }
    if (open_elements_.size() > 1 && open_elements_.back()->tag_name == "head") {
        open_elements_.pop_back();
    }
    if (!can_add_node()) {
        return html;
    }
    body_ = &append_synthetic_element(html, "body");
    if (can_descend()) {
        open_elements_.push_back(body_);
    }
    return *body_;
}

void HtmlTreeBuilder::start_tag(const HtmlToken& token) {
    if (token.name.empty() || !can_add_node()) {
        return;
    }

    if (options_.synthesize_document_structure) {
        if (token.name == "html") {
            start_html(token);
            return;
        }
        if (token.name == "head") {
            start_head(token);
            return;
        }
        if (token.name == "body") {
            start_body(token);
            return;
        }
        if (html_ == nullptr) {
            ensure_html();
        }
        if (body_ == nullptr && is_metadata_element(token.name)) {
            append_to_head(token);
            return;
        }
        if (body_ == nullptr) {
            ensure_body();
        }
    }

    apply_common_implied_end_tags(token.name);

    Node& parent = *open_elements_.back();
    Node& appended = append_element(parent, token);
    if (!token.self_closing && !is_void_element(appended.tag_name) && can_descend()) {
        open_elements_.push_back(&appended);
    }
}

void HtmlTreeBuilder::start_html(const HtmlToken& token) {
    if (html_ == nullptr) {
        html_ = &append_element(document_, token);
        open_elements_.push_back(html_);
        return;
    }
    merge_attributes(*html_, token);
}

void HtmlTreeBuilder::start_head(const HtmlToken& token) {
    Node& html = ensure_html();
    if (head_ == nullptr) {
        head_ = &append_element(html, token);
    } else {
        merge_attributes(*head_, token);
    }
    if (open_elements_.back() != head_ && can_descend()) {
        open_elements_.push_back(head_);
    }
}

void HtmlTreeBuilder::start_body(const HtmlToken& token) {
    Node& html = ensure_html();
    if (open_elements_.size() > 1 && open_elements_.back()->tag_name == "head") {
        open_elements_.pop_back();
    }
    if (body_ == nullptr) {
        body_ = &append_element(html, token);
    } else {
        merge_attributes(*body_, token);
    }
    if (open_elements_.back() != body_ && can_descend()) {
        open_elements_.push_back(body_);
    }
}

void HtmlTreeBuilder::append_to_head(const HtmlToken& token) {
    Node& head = ensure_head();
    Node& appended = append_element(head, token);
    if (!token.self_closing && !is_void_element(appended.tag_name) && can_descend()) {
        open_elements_.push_back(&appended);
    }
}

void HtmlTreeBuilder::merge_attributes(Node& node, const HtmlToken& token) {
    std::size_t count = node.attributes.size();
    for (const HtmlAttribute& attribute : token.attributes) {
        if (count >= options_.max_attributes_per_element) {
            break;
        }
        const auto inserted = node.attributes.emplace(attribute.name, attribute.value);
        if (inserted.second) {
            ++count;
        }
    }
}

void HtmlTreeBuilder::apply_common_implied_end_tags(std::string_view incoming_tag) {
    if (incoming_tag == "p" && has_open_element("p")) {
        pop_until("p");
    } else if (is_block_start(incoming_tag) && has_open_element("p")) {
        pop_until("p");
    }

    if (incoming_tag == "li") {
        pop_until_if_current_or_ancestor("li", {"ul", "ol", "body", "html"});
    } else if (incoming_tag == "dt" || incoming_tag == "dd") {
        pop_until_if_current_or_ancestor("dt", {"dl", "body", "html"});
        pop_until_if_current_or_ancestor("dd", {"dl", "body", "html"});
    } else if (incoming_tag == "option") {
        pop_current_if("option");
    } else if (incoming_tag == "tr") {
        pop_current_if("td");
        pop_current_if("th");
        pop_current_if("tr");
    } else if (incoming_tag == "td" || incoming_tag == "th") {
        pop_current_if("td");
        pop_current_if("th");
    }
}

void HtmlTreeBuilder::pop_until_if_current_or_ancestor(std::string_view tag_name,
                                                       std::initializer_list<std::string_view> stop_tags) {
    for (auto it = open_elements_.rbegin(); it != open_elements_.rend(); ++it) {
        if ((*it)->tag_name == tag_name) {
            pop_until(tag_name);
            return;
        }
        if (contains_name(stop_tags, (*it)->tag_name)) {
            return;
        }
    }
}

void HtmlTreeBuilder::end_tag(std::string_view tag_name) {
    if (tag_name == "html") {
        while (open_elements_.size() > 1) {
            open_elements_.pop_back();
        }
        return;
    }

    if (tag_name == "body" && body_ != nullptr) {
        pop_until("body");
        return;
    }

    if (tag_name == "head" && head_ != nullptr && has_open_element("head")) {
        pop_until("head");
        return;
    }

    if (has_open_element(tag_name)) {
        pop_until(tag_name);
    }
}

void HtmlTreeBuilder::text(std::string_view data) {
    if (!can_add_node()) {
        return;
    }
    if (options_.synthesize_document_structure && body_ == nullptr &&
        (open_elements_.back() == &document_ || open_elements_.back()->tag_name == "html")) {
        const std::string collapsed = collapse_tree_text(data);
        if (collapsed.empty()) {
            return;
        }
        ensure_body();
    }

    const bool preserve = preserves_text(open_elements_.back()->tag_name);
    const std::string value = options_.collapse_whitespace && !preserve
        ? collapse_tree_text(data)
        : std::string(data);
    if (value.empty()) {
        return;
    }

    ++node_count_;
    open_elements_.back()->append_child(make_text(value));
}

} // namespace wearweb

