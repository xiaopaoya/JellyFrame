#pragma once

#include "render_core/dom.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jellyframe::esp32s3 {

struct BandRoute {
    const char* control_id;
    const char* view_id;
};

inline constexpr std::array<BandRoute, 13> kBandRoutes{{
    {"home-to-activity", "view-activity"},
    {"home-to-apps", "view-apps"},
    {"home-to-weather", "view-weather"},
    {"home-to-quick", "view-quick"},
    {"home-to-notices", "view-notices"},
    {"apps-home", "view-home"},
    {"apps-activity", "view-activity"},
    {"apps-weather", "view-weather"},
    {"apps-quick", "view-quick"},
    {"activity-home", "view-home"},
    {"weather-home", "view-home"},
    {"quick-home", "view-home"},
    {"notices-home", "view-home"},
}};

inline constexpr std::array<const char*, 6> kBandViewIds{{
    "view-home", "view-apps", "view-activity", "view-weather", "view-quick", "view-notices",
}};

inline jellyframe::Node* find_band_node_by_id(jellyframe::Node& root, const std::string& id) {
    std::vector<jellyframe::Node*> stack{&root};
    while (!stack.empty()) {
        jellyframe::Node* node = stack.back();
        stack.pop_back();
        if (node->attribute("id") == id) {
            return node;
        }
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
            stack.push_back(child->get());
        }
    }
    return nullptr;
}

inline bool set_band_view(jellyframe::Node& document, std::string_view active_id) {
    bool active_view_found = false;
    for (const char* view_id : kBandViewIds) {
        if (jellyframe::Node* view = find_band_node_by_id(document, view_id)) {
            const bool active = std::string_view(view_id) == active_id;
            active_view_found = active_view_found || active;
            view->set_attribute("class", active ? "band-view active" : "band-view");
        }
    }
    return active_view_found;
}

template <typename OnRoute, typename OnMissing>
std::size_t bind_band_navigation(jellyframe::Node& document,
                                 OnRoute&& on_route,
                                 OnMissing&& on_missing) {
    auto route_handler = std::forward<OnRoute>(on_route);
    std::size_t bound_count = 0;
    for (const BandRoute& route : kBandRoutes) {
        if (jellyframe::Node* control = find_band_node_by_id(document, route.control_id)) {
            control->add_event_listener("click", [route_handler, view_id = route.view_id](jellyframe::Event&) {
                route_handler(view_id);
            });
            ++bound_count;
        } else {
            on_missing(route.control_id);
        }
    }
    return bound_count;
}

} // namespace jellyframe::esp32s3
