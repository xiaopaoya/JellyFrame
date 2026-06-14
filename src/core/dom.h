#pragma once

#include "core/event.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wearweb {

enum class NodeType {
    Element,
    Text,
};

struct Node : public EventTarget {
    explicit Node(NodeType node_type);

    NodeType type;
    std::string tag_name;
    std::string text;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    Node& append_child(std::unique_ptr<Node> child);
    const std::string& attribute(const std::string& name) const;
    bool has_class(const std::string& class_name) const;
};

std::unique_ptr<Node> make_element(std::string tag_name);
std::unique_ptr<Node> make_text(std::string text);

} // namespace wearweb
