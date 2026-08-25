#include "script/script_runtime.h"

#include "render_core/html_parser.h"
#include "render_core/layout.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

using Clock = std::chrono::steady_clock;

double average_microseconds(int iterations, const std::function<void()>& work) {
    const auto begin = Clock::now();
    for (int index = 0; index < iterations; ++index) {
        work();
    }
    const auto end = Clock::now();
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()) /
        static_cast<double>(iterations);
}

Node* find_by_id(Node& node, const std::string& id) {
    if (node.type == NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (Node* found = find_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    constexpr int kElementCount = 32;
    constexpr int kIterations = 1000;

    std::string html = "<body>";
    for (int index = 0; index < kElementCount; ++index) {
        html += "<p id='card" + std::to_string(index) + "'>Card</p>";
    }
    html += "</body>";

    HtmlParser parser;
    auto document = parser.parse(html);
    LayoutBox root;
    root.node = document.get();
    root.rect = Rect{0, 0, 172, 320};
    for (int index = 0; index < kElementCount; ++index) {
        Node* node = find_by_id(*document, "card" + std::to_string(index));
        if (node == nullptr) {
            throw std::runtime_error("script microbench fixture is incomplete");
        }
        LayoutBoxPtr box(new LayoutBox());
        box->node = node;
        box->rect = Rect{8, index * 10, 156, 8};
        root.children.push_back(std::move(box));
    }

    std::unique_ptr<ScriptRuntime> runtime = create_script_runtime();
    runtime->bind_document(*document);
    const ScriptEvaluationResult setup = runtime->eval(
        "var cards = [];"
        "for (var index = 0; index < 32; ++index) {"
        "  var card = document.getElementById('card' + index);"
        "  cards.push(card); card.getBoundingClientRect();"
        "}");
    if (!setup.ok) {
        throw std::runtime_error("script microbench setup failed: " + setup.error);
    }
    runtime->capture_layout_snapshot(root);

    const double capture_us = average_microseconds(kIterations, [&]() {
        runtime->capture_layout_snapshot(root);
    });
    const double read_us = average_microseconds(kIterations, [&]() {
        const ScriptEvaluationResult result = runtime->eval("cards[0].getBoundingClientRect().width;");
        if (!result.ok) {
            throw std::runtime_error("script microbench read failed: " + result.error);
        }
    });

    std::cout << "layout_snapshot_capture nodes=" << kElementCount
              << " iterations=" << kIterations << " avg_us=" << capture_us << '\n';
    std::cout << "layout_snapshot_js_read iterations=" << kIterations << " avg_us=" << read_us << '\n';
    return 0;
}
