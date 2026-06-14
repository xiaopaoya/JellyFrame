#include "core/dom.h"

#include "core/form_control.h"

#include <sstream>
#include <utility>

namespace wearweb {
namespace {

const std::string kEmpty;

bool attribute_affects_form_control_state(const std::string& name) {
    return name == "type" || name == "value" || name == "checked" || name == "selected" ||
        name == "min" || name == "max" || name == "step";
}

} // namespace

Node::Node(NodeType node_type)
    : type(node_type) {}

Node::~Node() = default;

Node& Node::append_child(std::unique_ptr<Node> child) {
    child->parent = this;
    children.push_back(std::move(child));
    mark_dirty(*this, DomDirtyTree | DomDirtyLayout);
    return *children.back();
}

bool Node::remove_child(const Node& child) {
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->get() != &child) {
            continue;
        }
        (*it)->parent = nullptr;
        children.erase(it);
        mark_dirty(*this, DomDirtyTree | DomDirtyLayout);
        return true;
    }
    return false;
}

void Node::set_attribute(std::string name, std::string value) {
    const auto it = attributes.find(name);
    if (it != attributes.end() && it->second == value) {
        return;
    }
    const bool reset_form_state = attribute_affects_form_control_state(name);
    attributes[std::move(name)] = std::move(value);
    if (reset_form_state) {
        form_control_state.reset();
    }
    mark_dirty(*this, DomDirtyAttributes | DomDirtyStyle | DomDirtyLayout);
}

bool Node::remove_attribute(const std::string& name) {
    const auto it = attributes.find(name);
    if (it == attributes.end()) {
        return false;
    }
    const bool reset_form_state = attribute_affects_form_control_state(name);
    attributes.erase(it);
    if (reset_form_state) {
        form_control_state.reset();
    }
    mark_dirty(*this, DomDirtyAttributes | DomDirtyStyle | DomDirtyLayout);
    return true;
}

void Node::set_text(std::string value) {
    if (type != NodeType::Text || text == value) {
        return;
    }
    text = std::move(value);
    mark_dirty(*this, DomDirtyText | DomDirtyLayout);
}

void Node::set_text_content(std::string value) {
    if (type == NodeType::Text) {
        set_text(std::move(value));
        return;
    }
    children.clear();
    if (!value.empty()) {
        auto child = make_text(std::move(value));
        child->parent = this;
        children.push_back(std::move(child));
    }
    form_control_state.reset();
    mark_dirty(*this, DomDirtyTree | DomDirtyText | DomDirtyLayout);
}

std::string Node::text_content() const {
    if (type == NodeType::Text) {
        return text;
    }
    std::string output;
    for (const auto& child : children) {
        output += child->text_content();
    }
    return output;
}

const std::string& Node::attribute(const std::string& name) const {
    const auto it = attributes.find(name);
    if (it == attributes.end()) {
        return kEmpty;
    }
    return it->second;
}

bool Node::has_class(const std::string& class_name) const {
    std::istringstream stream(attribute("class"));
    std::string token;
    while (stream >> token) {
        if (token == class_name) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Node> make_element(std::string tag_name) {
    auto node = std::make_unique<Node>(NodeType::Element);
    node->tag_name = std::move(tag_name);
    return node;
}

std::unique_ptr<Node> make_text(std::string text) {
    auto node = std::make_unique<Node>(NodeType::Text);
    node->text = std::move(text);
    return node;
}

void mark_dirty(Node& node, DomDirtyFlags flags) {
    if (flags == DomDirtyNone) {
        return;
    }
    for (Node* current = &node; current != nullptr; current = current->parent) {
        current->dirty_flags |= flags;
    }
}

DomDirtyFlags subtree_dirty_flags(const Node& node) {
    DomDirtyFlags flags = node.dirty_flags;
    for (const auto& child : node.children) {
        flags |= subtree_dirty_flags(*child);
    }
    return flags;
}

void clear_dirty_flags(Node& node) {
    node.dirty_flags = DomDirtyNone;
    for (const auto& child : node.children) {
        clear_dirty_flags(*child);
    }
}

DomDirtyFlags take_dirty_flags(Node& node) {
    const DomDirtyFlags flags = subtree_dirty_flags(node);
    clear_dirty_flags(node);
    return flags;
}

} // namespace wearweb
