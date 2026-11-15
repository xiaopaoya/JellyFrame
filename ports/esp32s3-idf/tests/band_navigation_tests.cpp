#include "jellyframe_esp32s3_band_navigation.h"

#include "render_core/html_parser.h"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::string> forecast_text(jellyframe::Node& document) {
    std::vector<std::string> result;
    std::vector<jellyframe::Node*> stack{&document};
    while (!stack.empty()) {
        jellyframe::Node* node = stack.back();
        stack.pop_back();
        if (node->tag_name == "article" && node->parent != nullptr &&
            node->parent->has_class("forecast")) {
            result.push_back(node->text_content());
        }
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
            stack.push_back(child->get());
        }
    }
    return result;
}

bool active(jellyframe::Node& document, const char* id) {
    jellyframe::Node* view = jellyframe::esp32s3::find_band_node_by_id(document, id);
    return view != nullptr && view->has_class("active");
}

} // namespace

int main() {
    const std::string source = read_file(JELLYFRAME_BAND_SHELL_FIXTURE);
    if (!check(!source.empty(), "real band shell fixture is readable")) {
        return 1;
    }
    std::unique_ptr<jellyframe::Node> document = jellyframe::HtmlParser().parse(source);
    if (!check(document != nullptr, "real band shell fixture parses")) {
        return 1;
    }

    const std::vector<std::string> expected_forecast{"1026", "1229", "1431", "1630"};
    if (!check(forecast_text(*document) == expected_forecast,
               "the source contains all four forecast hour/temperature labels")) {
        return 1;
    }

    unsigned transitions = 0;
    unsigned missing_controls = 0;
    const std::size_t bound = jellyframe::esp32s3::bind_band_navigation(
        *document,
        [&](const char* view_id) {
            if (jellyframe::esp32s3::set_band_view(*document, view_id)) {
                ++transitions;
            }
        },
        [&](const char*) { ++missing_controls; });
    if (!check(bound == jellyframe::esp32s3::kBandRoutes.size() && missing_controls == 0,
               "native route table binds every real shell control")) {
        return 1;
    }

    jellyframe::Node* weather = jellyframe::esp32s3::find_band_node_by_id(*document, "home-to-weather");
    if (!check(weather != nullptr, "home-to-weather control exists")) {
        return 1;
    }
    jellyframe::Event enter_weather("click");
    jellyframe::dispatch_event(*weather, enter_weather);
    if (!check(transitions == 1 && active(*document, "view-weather") && !active(*document, "view-home"),
               "native home-to-weather listener activates Weather")) {
        return 1;
    }
    if (!check(forecast_text(*document) == expected_forecast,
               "all four forecast labels remain in the DOM after entering Weather")) {
        return 1;
    }

    jellyframe::Node* back = jellyframe::esp32s3::find_band_node_by_id(*document, "weather-home");
    if (!check(back != nullptr, "Weather back control exists")) {
        return 1;
    }
    jellyframe::Event return_home("click");
    jellyframe::dispatch_event(*back, return_home);
    if (!check(transitions == 2 && active(*document, "view-home") && !active(*document, "view-weather"),
               "native Weather-to-home listener restores Home")) {
        return 1;
    }
    if (!check(forecast_text(*document) == expected_forecast,
               "all four forecast labels remain in the DOM after returning Home")) {
        return 1;
    }

    std::cout << "native_band_navigation=pass transitions=" << transitions
              << " forecast_cards=" << expected_forecast.size()
              << " scope=event_mapping_and_dom_state_only\n";
    return 0;
}
