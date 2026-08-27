#include "render_core/dom.h"

#include "render_core/form_control.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
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
        node->parent = nullptr;
        for (auto& child : node->children) {
            child->parent = nullptr;
            pending.push_back(std::move(child));
        }
        node->children.clear();
    }
}

std::string first_class_token(const Node& node) {
    std::istringstream stream(node.attribute("class"));
    std::string token;
    stream >> token;
    return token;
}

std::string limited_token(std::string token, std::size_t max_chars = 32) {
    if (token.size() > max_chars) {
        token.resize(max_chars);
    }
    return token;
}

std::size_t element_index_of_type(const Node& node) {
    if (node.parent == nullptr || node.type != NodeType::Element) {
        return 1;
    }
    std::size_t index = 0;
    for (const auto& sibling : node.parent->children) {
        if (sibling->type != NodeType::Element || sibling->tag_name != node.tag_name) {
            continue;
        }
        ++index;
        if (sibling.get() == &node) {
            return index;
        }
    }
    return std::max<std::size_t>(1, index);
}

std::string dom_path_segment(const Node& node) {
    std::string segment = node.tag_name.empty() ? "element" : node.tag_name;
    const std::string& id = node.attribute("id");
    if (!id.empty()) {
        segment += '#';
        segment += limited_token(id);
        return segment;
    }
    const std::string class_name = first_class_token(node);
    if (!class_name.empty()) {
        segment += '.';
        segment += limited_token(class_name);
    }
    if (node.parent != nullptr) {
        segment += ":nth-of-type(";
        segment += std::to_string(element_index_of_type(node));
        segment += ')';
    }
    return segment;
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
    event_dispatch_node_destroyed(*this);
    std::vector<DestroyObserverEntry> observers;
    observers.swap(destroy_observers_);
    for (const DestroyObserverEntry& entry : observers) {
        if (entry.observer != nullptr) {
            entry.observer(*this, entry.context);
        }
    }
    destroy_node_list_iterative(children);
}

Node& Node::append_child(std::unique_ptr<Node> child) {
    return insert_child(std::move(child), children.size());
}

Node& Node::insert_child(std::unique_ptr<Node> child, std::size_t index) {
    if (!child) {
        throw std::invalid_argument("cannot insert a null DOM node");
    }
    for (const Node* current = this; current != nullptr; current = current->parent) {
        if (current == child.get()) {
            throw std::invalid_argument("DOM insertion would create a cycle");
        }
    }
    if (child->parent != nullptr) {
        throw std::invalid_argument("only detached DOM nodes can be inserted");
    }
    index = std::min(index, children.size());
    auto inserted = children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
    (*inserted)->parent = this;
    mark_dirty(*this, DomDirtyTree | DomDirtyLayout);
    return **inserted;
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
    if (children.size() == 1 && children.front()->type == NodeType::Text) {
        children.front()->set_text(std::move(value));
        return;
    }
    std::vector<std::unique_ptr<Node>> replacement_children;
    if (!value.empty()) {
        replacement_children.reserve(1);
        auto child = make_text(std::move(value));
        replacement_children.push_back(std::move(child));
    }
    destroy_node_list_iterative(children);
    children.swap(replacement_children);
    if (!children.empty()) {
        children.front()->parent = this;
    }
    form_control_state.reset();
    mark_dirty(*this, DomDirtyTree | DomDirtyText | DomDirtyLayout);
}

std::string Node::text_content() const {
    if (type == NodeType::Text) {
        return text;
    }

    std::size_t total_size = 0;
    std::vector<const Node*> pending;
    pending.reserve(children.size());
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        pending.push_back(it->get());
    }

    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            total_size += current->text.size();
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }

    std::string output;
    output.reserve(total_size);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        pending.push_back(it->get());
    }

    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            output += current->text;
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
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

void Node::set_destroy_observer(DestroyObserver observer, void* context) {
    destroy_observers_.clear();
    add_destroy_observer(observer, context);
}

void Node::clear_destroy_observer(DestroyObserver observer, void* context) {
    remove_destroy_observer(observer, context);
}

void Node::add_destroy_observer(DestroyObserver observer, void* context) {
    if (observer == nullptr) return;
    const auto existing = std::find_if(destroy_observers_.begin(), destroy_observers_.end(),
                                       [observer, context](const DestroyObserverEntry& entry) {
                                           return entry.observer == observer && entry.context == context;
                                       });
    if (existing == destroy_observers_.end()) destroy_observers_.push_back({observer, context});
}

void Node::remove_destroy_observer(DestroyObserver observer, void* context) {
    destroy_observers_.erase(
        std::remove_if(destroy_observers_.begin(), destroy_observers_.end(),
                       [observer, context](const DestroyObserverEntry& entry) {
                           return entry.observer == observer && entry.context == context;
                       }),
        destroy_observers_.end());
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
    Node* root = &node;
    for (Node* current = &node; current != nullptr; current = current->parent) {
        current->dirty_flags |= flags;
        root = current;
    }
    ++root->mutation_generation;
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

    std::vector<Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        Node* current = pending.back();
        pending.pop_back();
        if (current->dirty_flags == DomDirtyNone) {
            continue;
        }
        current->dirty_flags = DomDirtyNone;
        current->local_dirty_flags = DomDirtyNone;
        for (const auto& child : current->children) {
            if (child->dirty_flags != DomDirtyNone) {
                pending.push_back(child.get());
            }
        }
    }
}

DomDirtyFlags take_dirty_flags(Node& node) {
    const DomDirtyFlags flags = node.dirty_flags;
    clear_dirty_flags(node);
    return flags;
}

DomStatistics compute_dom_statistics(const Node& root) {
    DomStatistics statistics;
    std::vector<std::pair<const Node*, std::size_t>> pending;
    pending.push_back({&root, 1});
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        ++statistics.node_count;
        statistics.string_bytes += node->tag_name.size();
        statistics.string_bytes += node->text.size();
        statistics.max_depth = std::max(statistics.max_depth, depth);
        statistics.max_child_count = std::max(statistics.max_child_count, node->children.size());
        if (node->type == NodeType::Element) {
            ++statistics.element_count;
            statistics.attribute_count += node->attributes.size();
            statistics.max_attributes_per_element =
                std::max(statistics.max_attributes_per_element, node->attributes.size());
            for (const auto& attribute : node->attributes) {
                statistics.string_bytes += attribute.first.size();
                statistics.string_bytes += attribute.second.size();
            }
        } else {
            ++statistics.text_count;
        }
        for (const auto& child : node->children) {
            pending.push_back({child.get(), depth + 1});
        }
    }
    return statistics;
}

std::string dom_node_label(const Node* node) {
    if (node == nullptr) {
        return "node";
    }
    if (node->type == NodeType::Text) {
        node = node->parent;
    }
    if (node == nullptr) {
        return "text";
    }
    if (node->type != NodeType::Element) {
        return "node";
    }
    std::string label = node->tag_name.empty() ? "element" : node->tag_name;
    const std::string& id = node->attribute("id");
    if (!id.empty()) {
        label += '#';
        label += limited_token(id);
        return label;
    }
    const std::string class_name = first_class_token(*node);
    if (!class_name.empty()) {
        label += '.';
        label += limited_token(class_name);
    }
    return label;
}

std::string dom_node_path(const Node* node, std::size_t max_depth) {
    if (node == nullptr) {
        return "node";
    }
    if (node->type == NodeType::Text) {
        node = node->parent;
    }
    if (node == nullptr) {
        return "text";
    }
    std::vector<const Node*> ancestors;
    for (const Node* current = node; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element) {
            ancestors.push_back(current);
        }
    }
    if (ancestors.empty()) {
        return dom_node_label(node);
    }
    const bool truncated = max_depth > 0 && ancestors.size() > max_depth;
    const std::size_t count = max_depth == 0 ? ancestors.size() : std::min(max_depth, ancestors.size());
    const std::size_t begin = ancestors.size() - count;
    std::string path = truncated ? "...>" : "";
    for (std::size_t index = ancestors.size(); index-- > begin;) {
        if (!path.empty() && path.back() != '>') {
            path += '>';
        }
        path += dom_path_segment(*ancestors[index]);
    }
    return path;
}

} // namespace jellyframe
