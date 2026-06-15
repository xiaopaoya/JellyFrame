#pragma once

#include "core/event.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jellyframe {

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
    DomDirtyPaint = 1U << 5,
};

using DomDirtyFlags = std::uint32_t;

class AttributeList {
public:
    using Entry = std::pair<std::string, std::string>;
    using Storage = std::vector<Entry>;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;

    iterator begin() { return entries_.begin(); }
    iterator end() { return entries_.end(); }
    const_iterator begin() const { return entries_.begin(); }
    const_iterator end() const { return entries_.end(); }
    const_iterator cbegin() const { return entries_.cbegin(); }
    const_iterator cend() const { return entries_.cend(); }

    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }

    iterator find(const std::string& name);
    const_iterator find(const std::string& name) const;
    std::pair<iterator, bool> emplace(std::string name, std::string value);
    iterator erase(iterator it);
    std::string& operator[](std::string name);

private:
    Storage entries_;
};

struct Node : public EventTarget {
    explicit Node(NodeType node_type);
    ~Node();

    NodeType type;
    std::string tag_name;
    std::string text;
    AttributeList attributes;
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
bool dirty_requires_render_or_layout(DomDirtyFlags flags);
void clear_dirty_flags(Node& node);
DomDirtyFlags take_dirty_flags(Node& node);

} // namespace jellyframe
