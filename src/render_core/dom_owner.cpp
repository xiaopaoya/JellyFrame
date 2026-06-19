#include "render_core/dom_owner.h"

#include <algorithm>
#include <utility>

namespace jellyframe {
namespace {

void merge_dom_statistics(DomStatistics& target, const DomStatistics& source) {
    target.node_count += source.node_count;
    target.element_count += source.element_count;
    target.text_count += source.text_count;
    target.attribute_count += source.attribute_count;
    target.max_depth = std::max(target.max_depth, source.max_depth);
    target.max_child_count = std::max(target.max_child_count, source.max_child_count);
    target.max_attributes_per_element =
        std::max(target.max_attributes_per_element, source.max_attributes_per_element);
}

} // namespace

DomOwner::DomOwner(std::unique_ptr<Node> root)
    : root_(std::move(root)) {}

void DomOwner::set_root(std::unique_ptr<Node> root) {
    root_ = std::move(root);
}

std::unique_ptr<Node> DomOwner::release_root() {
    return std::move(root_);
}

Node* DomOwner::adopt_detached_node(std::unique_ptr<Node> node) {
    if (!node) {
        return nullptr;
    }
    node->parent = nullptr;
    detached_nodes_.push_back(std::move(node));
    return detached_nodes_.back().get();
}

std::unique_ptr<Node> DomOwner::release_detached_node(Node& node) {
    for (auto it = detached_nodes_.begin(); it != detached_nodes_.end(); ++it) {
        if (it->get() != &node) {
            continue;
        }
        std::unique_ptr<Node> released = std::move(*it);
        detached_nodes_.erase(it);
        return released;
    }
    return nullptr;
}

void DomOwner::clear_detached_nodes() {
    detached_nodes_.clear();
}

DetachedDomStatistics DomOwner::detached_statistics() const {
    DetachedDomStatistics statistics;
    statistics.root_count = detached_nodes_.size();
    for (const auto& node : detached_nodes_) {
        const DomStatistics subtree = compute_dom_statistics(*node);
        merge_dom_statistics(statistics.aggregate, subtree);
        statistics.max_subtree_nodes =
            std::max(statistics.max_subtree_nodes, subtree.node_count);
    }
    return statistics;
}

} // namespace jellyframe
