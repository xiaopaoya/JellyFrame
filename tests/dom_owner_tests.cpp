#include "core/dom_owner.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void root_ownership_is_explicit() {
    DomOwner owner(make_element("document"));
    check(owner.has_root(), "owner starts with root");
    check(owner.root() != nullptr && owner.root()->tag_name == "document", "root is readable");

    auto released = owner.release_root();
    check(!owner.has_root(), "root can be released");
    check(released && released->tag_name == "document", "released root keeps ownership");

    owner.set_root(std::move(released));
    check(owner.has_root(), "root can be restored");
}

void detached_nodes_can_be_adopted_released_and_measured() {
    DomOwner owner;
    auto panel = make_element("section");
    panel->attributes["id"] = "panel";
    panel->append_child(make_text("Detached"));

    Node* adopted = owner.adopt_detached_node(std::move(panel));
    check(adopted != nullptr, "detached node adopted");
    check(adopted->parent == nullptr, "detached node has no parent");
    check(owner.detached_node_count() == 1, "detached root count");

    const DetachedDomStatistics statistics = owner.detached_statistics();
    check(statistics.root_count == 1, "statistics root count");
    check(statistics.aggregate.node_count == 2, "statistics aggregate subtree nodes");
    check(statistics.aggregate.element_count == 1, "statistics aggregate elements");
    check(statistics.aggregate.text_count == 1, "statistics aggregate text nodes");
    check(statistics.aggregate.attribute_count == 1, "statistics aggregate attributes");
    check(statistics.max_subtree_nodes == 2, "statistics max subtree nodes");

    auto released = owner.release_detached_node(*adopted);
    check(released != nullptr, "detached node can be released");
    check(owner.detached_node_count() == 0, "detached count decreases after release");
}

void null_detached_node_is_ignored() {
    DomOwner owner;
    check(owner.adopt_detached_node(nullptr) == nullptr, "null detached node rejected");
    check(owner.detached_node_count() == 0, "null detached node does not change count");
}

} // namespace

int main() {
    try {
        root_ownership_is_explicit();
        detached_nodes_can_be_adopted_released_and_measured();
        null_detached_node_is_ignored();
    } catch (const std::exception& error) {
        std::cerr << "dom owner test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "dom owner tests passed\n";
    return 0;
}
