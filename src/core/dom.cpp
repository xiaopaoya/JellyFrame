#include "core/dom.h"

#include <sstream>
#include <utility>

namespace wearweb {
namespace {

const std::string kEmpty;

} // namespace

Node::Node(NodeType node_type)
    : type(node_type) {}

Node& Node::append_child(std::unique_ptr<Node> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return *children.back();
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

} // namespace wearweb

