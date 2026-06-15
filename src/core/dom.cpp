#include "core/dom.h"

#include "core/form_control.h"

#include <sstream>
#include <utility>

namespace jellyframe {
namespace {

const std::string kEmpty;

bool attribute_affects_form_control_state(const std::string& name) {
    return name == "type" || name == "value" || name == "checked" || name == "selected" ||
        name == "min" || name == "max" || name == "step";
}

void destroy_node_list_iterative(std::vector<std::unique_ptr<Node>>& nodes) {
    std::vector<std::unique_ptr<Node>> pending;
    pending.swap(nodes);
    while (!pending.empty()) {
        std::unique_ptr<Node> node = std::move(pending.back());
        pending.pop_back();
        for (auto& child : node->children) {
            pending.push_back(std::move(child));
        }
        node->children.clear();
    }
}

} // namespace

AttributeList::iterator AttributeList::find(const std::string& name) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == name) {
            return it;
        }
    }
    return entries_.end();
}

AttributeList::const_iterator AttributeList::find(const std::string& name) const {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == name) {
            return it;
        }
    }
    return entries_.end();
}

std::pair<AttributeList::iterator, bool> AttributeList::emplace(std::string name, std::string value) {
    auto existing = find(name);
    if (existing != entries_.end()) {
        return {existing, false};
    }
    entries_.emplace_back(std::move(name), std::move(value));
    auto inserted = entries_.end();
    --inserted;
    return {inserted, true};
}

AttributeList::iterator AttributeList::erase(iterator it) {
    return entries_.erase(it);
}

std::string& AttributeList::operator[](std::string name) {
    auto existing = find(name);
    if (existing != entries_.end()) {
        return existing->second;
    }
    entries_.emplace_back(std::move(name), std::string{});
    return entries_.back().second;
}

Node::Node(NodeType node_type)
    : type(node_type) {}

Node::~Node() {
    destroy_node_list_iterative(children);
}

Node& Node::append_child(std::unique_ptr<Node> child) {
    child->parent = this;
    children.push_back(std::move(child));
    mark_dirty(*this, DomDirtyTree | DomDirtyLayout);
    return *children.back();
}

std::unique_ptr<Node> Node::detach_child(const Node& child) {
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->get() != &child) {
            continue;
        }
        std::unique_ptr<Node> detached = std::move(*it);
        detached->parent = nullptr;
        children.erase(it);
        mark_dirty(*this, DomDirtyTree | DomDirtyLayout);
        return detached;
    }
    return nullptr;
}

bool Node::remove_child(const Node& child) {
    if (auto detached = detach_child(child)) {
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
    if (value.empty() && children.empty()) {
        return;
    }
    if (children.size() == 1 && children.front()->type == NodeType::Text &&
        children.front()->text == value) {
        return;
    }
    destroy_node_list_iterative(children);
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
    node.local_dirty_flags |= flags;
    for (Node* current = &node; current != nullptr; current = current->parent) {
        current->dirty_flags |= flags;
    }
}

DomDirtyFlags subtree_dirty_flags(const Node& node) {
    return node.dirty_flags;
}

bool dirty_requires_render_or_layout(DomDirtyFlags flags) {
    return (flags & (DomDirtyTree | DomDirtyAttributes | DomDirtyText | DomDirtyStyle | DomDirtyLayout)) != 0U;
}

void clear_dirty_flags(Node& node) {
    if (node.dirty_flags == DomDirtyNone) {
        return;
    }
    node.dirty_flags = DomDirtyNone;
    node.local_dirty_flags = DomDirtyNone;
    for (const auto& child : node.children) {
        if (child->dirty_flags != DomDirtyNone) {
            clear_dirty_flags(*child);
        }
    }
}

DomDirtyFlags take_dirty_flags(Node& node) {
    const DomDirtyFlags flags = node.dirty_flags;
    clear_dirty_flags(node);
    return flags;
}

} // namespace jellyframe
