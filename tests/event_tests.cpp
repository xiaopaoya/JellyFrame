#include "core/dom.h"
#include "core/event.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Node& append_element(Node& parent, const char* tag) {
    return parent.append_child(make_element(tag));
}

void dispatch_runs_capture_target_and_bubble() {
    auto document = make_element("document");
    Node& html = append_element(*document, "html");
    Node& body = append_element(html, "body");
    Node& button = append_element(body, "button");

    std::vector<std::string> order;
    document->add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Capturing, "document capture phase");
        order.push_back("document-capture");
    }, EventListenerOptions{true, false});
    body.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Capturing, "body capture phase");
        order.push_back("body-capture");
    }, EventListenerOptions{true, false});
    button.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::AtTarget, "button target capture phase");
        order.push_back("button-capture");
    }, EventListenerOptions{true, false});
    button.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::AtTarget, "button target bubble phase");
        order.push_back("button-bubble");
    });
    body.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Bubbling, "body bubble phase");
        order.push_back("body-bubble");
    });

    MouseEvent event("click", 10, 12);
    const bool allowed = dispatch_event(button, event);

    check(allowed, "click default allowed");
    check(event.target() == &button, "event target preserved");
    check(order.size() == 5, "event listener count");
    check(order[0] == "document-capture", "capture order root first");
    check(order[1] == "body-capture", "capture order parent");
    check(order[2] == "button-capture", "target capture before target bubble");
    check(order[3] == "button-bubble", "target bubble");
    check(order[4] == "body-bubble", "bubble parent");
}

void prevent_default_stop_and_once_work() {
    auto document = make_element("document");
    Node& body = append_element(*document, "body");
    Node& button = append_element(body, "button");

    int once_count = 0;
    int body_bubble_count = 0;
    button.add_event_listener("click", [&](Event& event) {
        ++once_count;
        event.prevent_default();
        event.stop_propagation();
    }, EventListenerOptions{false, true});
    body.add_event_listener("click", [&](Event&) {
        ++body_bubble_count;
    });

    MouseEvent first("click", 1, 2);
    const bool first_allowed = dispatch_event(button, first);
    MouseEvent second("click", 1, 2);
    const bool second_allowed = dispatch_event(button, second);

    check(!first_allowed, "prevent default affects dispatch return");
    check(second_allowed, "once listener removed before second dispatch");
    check(once_count == 1, "once listener called once");
    check(body_bubble_count == 1, "stop propagation only affected first dispatch");
}

} // namespace

int main() {
    try {
        dispatch_runs_capture_target_and_bubble();
        prevent_default_stop_and_once_work();
    } catch (const std::exception& error) {
        std::cerr << "event test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "event tests passed\n";
    return 0;
}
