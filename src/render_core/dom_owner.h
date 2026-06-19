#pragma once

#include "render_core/dom.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace jellyframe {

struct DetachedDomStatistics {
    std::size_t root_count = 0;
    DomStatistics aggregate;
    std::size_t max_subtree_nodes = 0;
};

class DomOwner {
public:
    DomOwner() = default;
    explicit DomOwner(std::unique_ptr<Node> root);

    DomOwner(const DomOwner&) = delete;
    DomOwner& operator=(const DomOwner&) = delete;

    DomOwner(DomOwner&&) noexcept = default;
    DomOwner& operator=(DomOwner&&) noexcept = default;

    Node* root() { return root_.get(); }
    const Node* root() const { return root_.get(); }
    bool has_root() const { return static_cast<bool>(root_); }
    void set_root(std::unique_ptr<Node> root);
    std::unique_ptr<Node> release_root();

    Node* adopt_detached_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> release_detached_node(Node& node);
    void clear_detached_nodes();
    std::size_t detached_node_count() const { return detached_nodes_.size(); }
    DetachedDomStatistics detached_statistics() const;

private:
    std::unique_ptr<Node> root_;
    std::vector<std::unique_ptr<Node>> detached_nodes_;
};

} // namespace jellyframe
