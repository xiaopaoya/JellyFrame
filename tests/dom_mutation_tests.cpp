#include "core/dom.h"
#include "core/html_parser.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Node* find_first_by_tag(Node& node, const std::string& tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void parser_returns_clean_document() {
    HtmlParser parser;
    auto document = parser.parse("<body><p>Hello</p></body>");

    check(subtree_dirty_flags(*document) == DomDirtyNone, "parsed document starts clean");
}

void compact_attribute_list_behaves_like_small_map() {
    auto element = make_element("button");
    const auto first_insert = element->attributes.emplace("id", "save");
    const auto duplicate_insert = element->attributes.emplace("id", "cancel");
    element->attributes["class"] = "primary";

    check(first_insert.second, "first attribute insert succeeds");
    check(!duplicate_insert.second, "duplicate attribute insert keeps first value");
    check(element->attribute("id") == "save", "duplicate attribute does not overwrite");
    check(element->attribute("class") == "primary", "operator[] stores attribute value");
    check(element->attributes.size() == 2, "attribute list size is compact");

    const auto class_it = element->attributes.find("class");
    check(class_it != element->attributes.end(), "attribute find locates class");
    element->attributes.erase(class_it);
    check(element->attributes.find("class") == element->attributes.end(), "attribute erase removes entry");
}

void append_and_remove_mark_tree_dirty() {
    auto document = make_element("document");
    Node& body = document->append_child(make_element("body"));
    clear_dirty_flags(*document);

    Node& paragraph = body.append_child(make_element("p"));
    check((subtree_dirty_flags(*document) & DomDirtyTree) != 0U, "append marks tree dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "append marks layout dirty");

    const DomDirtyFlags taken = take_dirty_flags(*document);
    check((taken & DomDirtyTree) != 0U, "take returns dirty flags");
    check(subtree_dirty_flags(*document) == DomDirtyNone, "take clears dirty flags");

    check(body.remove_child(paragraph), "remove child succeeds");
    check((subtree_dirty_flags(*document) & DomDirtyTree) != 0U, "remove marks tree dirty");
}

void attributes_and_text_mark_specific_dirty_bits() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='note'>Old</p></body>");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph != nullptr, "paragraph exists");

    paragraph->set_attribute("class", "highlight");
    DomDirtyFlags flags = subtree_dirty_flags(*document);
    check((flags & DomDirtyAttributes) != 0U, "attribute mutation marks attribute dirty");
    check((flags & DomDirtyStyle) != 0U, "class mutation marks style dirty");
    clear_dirty_flags(*document);

    paragraph->set_text_content("New");
    flags = subtree_dirty_flags(*document);
    check(paragraph->text_content() == "New", "text content updated");
    check((flags & DomDirtyText) != 0U, "text content marks text dirty");
    check((flags & DomDirtyLayout) != 0U, "text content marks layout dirty");
    clear_dirty_flags(*document);

    check(paragraph->remove_attribute("class"), "attribute removed");
    flags = subtree_dirty_flags(*document);
    check((flags & DomDirtyAttributes) != 0U, "remove attribute marks attribute dirty");
}

void unchanged_text_content_stays_clean() {
    HtmlParser parser;
    auto document = parser.parse("<body><p>Same</p></body>");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph != nullptr, "paragraph exists");

    paragraph->set_text_content("Same");
    check(subtree_dirty_flags(*document) == DomDirtyNone, "same textContent does not dirty document");
}

void deep_subtree_replacement_and_teardown_are_iterative() {
    auto document = make_element("document");
    Node* current = document.get();
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }

    document->set_text_content("flat");
    check(document->children.size() == 1, "deep subtree replaced by one text node");
    check(document->children.front()->type == NodeType::Text, "replacement text node exists");
}

} // namespace

int main() {
    try {
        parser_returns_clean_document();
        compact_attribute_list_behaves_like_small_map();
        append_and_remove_mark_tree_dirty();
        attributes_and_text_mark_specific_dirty_bits();
        unchanged_text_content_stays_clean();
        deep_subtree_replacement_and_teardown_are_iterative();
    } catch (const std::exception& error) {
        std::cerr << "dom mutation test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "dom mutation tests passed\n";
    return 0;
}
