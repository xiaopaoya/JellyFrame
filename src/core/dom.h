#pragma once

#include "core/event.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wearweb {

struct FormControlState;

enum class NodeType {
    Element,
    Text,
};

enum DomDirtyFlag : std::uint32_t {
    DomDirtyNone = 0,
    DomDirtyTree = 1U << 0,
    DomDirtyAttributes = 1U << 1,
    DomDirtyText = 1U << 2,
    DomDirtyStyle = 1U << 3,
    DomDirtyLayout = 1U << 4,
};

using DomDirtyFlags = std::uint32_t;

struct Node : public EventTarget {
    explicit Node(NodeType node_type);
    ~Node();

    NodeType type;
    std::string tag_name;
    std::string text;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;
    mutable std::unique_ptr<FormControlState> form_control_state;
    DomDirtyFlags dirty_flags = DomDirtyNone;
    DomDirtyFlags local_dirty_flags = DomDirtyNone;

    Node& append_child(std::unique_ptr<Node> child);
    std::unique_ptr<Node> detach_child(const Node& child);
    bool remove_child(const Node& child);
    void set_attribute(std::string name, std::string value);
    bool remove_attribute(const std::string& name);
    void set_text(std::string value);
    void set_text_content(std::string value);
    std::string text_content() const;
    const std::string& attribute(const std::string& name) const;
    bool has_class(const std::string& class_name) const;
};

std::unique_ptr<Node> make_element(std::string tag_name);
std::unique_ptr<Node> make_text(std::string text);
void mark_dirty(Node& node, DomDirtyFlags flags);
DomDirtyFlags subtree_dirty_flags(const Node& node);
void clear_dirty_flags(Node& node);
DomDirtyFlags take_dirty_flags(Node& node);

} // namespace wearweb
