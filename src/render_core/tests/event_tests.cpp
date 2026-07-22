#include "render_core/dom.h"
#include "render_core/event.h"

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

void listener_mutation_during_dispatch_is_stable() {
    auto document = make_element("document");
    Node& button = append_element(*document, "button");

    int first_count = 0;
    int added_count = 0;
    EventTarget::ListenerId removable_id = 0;

    button.add_event_listener("click", [&](Event&) {
        ++first_count;
        button.add_event_listener("click", [&](Event&) {
            ++added_count;
        });
        if (first_count == 1) {
            check(button.remove_event_listener(removable_id), "listener removed during dispatch");
        }
    });
    removable_id = button.add_event_listener("click", [&](Event&) {
        throw std::runtime_error("removed listener should not run in the same dispatch");
    });

    MouseEvent first("click", 1, 2);
    dispatch_event(button, first);
    check(first_count == 1, "first listener ran once");
    check(added_count == 0, "new listener waits until the next dispatch");

    MouseEvent second("click", 1, 2);
    dispatch_event(button, second);
    check(first_count == 2, "existing listener still runs on second dispatch");
    check(added_count == 1, "added listener runs on later dispatch");
}

void bounded_cpp_listener_registration_respects_limit() {
    auto button = make_element("button");
    int count = 0;
    const EventTarget::ListenerId first =
        button->add_event_listener_bounded("click", [&](Event&) { ++count; }, 1);
    const EventTarget::ListenerId second =
        button->add_event_listener_bounded("click", [&](Event&) { count += 10; }, 1);

    check(first != 0, "first bounded listener registered");
    check(second == 0, "second bounded listener rejected");
    check(button->event_listener_count() == 1, "bounded listener count");
    check(button->remove_event_listener(first), "returned bounded listener id removes listener");
    check(button->event_listener_count() == 0, "removed bounded listener count");

    const EventTarget::ListenerId third =
        button->add_event_listener_bounded("click", [&](Event&) { ++count; }, 1);
    check(third != 0 && third != first, "bounded listener ids remain stable after removal");

    MouseEvent event("click", 0, 0);
    dispatch_event(*button, event);
    check(count == 1, "only registered bounded listener runs");
}

void dispatch_stops_safely_when_a_listener_destroys_its_target() {
    auto document = make_element("document");
    Node& parent = append_element(*document, "div");
    Node& child = append_element(parent, "button");
    int child_calls = 0;
    int parent_calls = 0;
    child.add_event_listener("click", [&](Event&) {
        ++child_calls;
        parent.set_text_content("replaced");
    });
    child.add_event_listener("click", [&](Event&) { ++child_calls; });
    parent.add_event_listener("click", [&](Event&) { ++parent_calls; });

    MouseEvent event("click", 0, 0);
    check(dispatch_event(child, event), "destroying listener preserves dispatch result");
    check(child_calls == 1, "destroyed target does not invoke later listeners");
    check(parent_calls == 0, "destroyed path does not bubble");
}

} // namespace

int main() {
    try {
        dispatch_runs_capture_target_and_bubble();
        prevent_default_stop_and_once_work();
        listener_mutation_during_dispatch_is_stable();
        bounded_cpp_listener_registration_respects_limit();
        dispatch_stops_safely_when_a_listener_destroys_its_target();
    } catch (const std::exception& error) {
        std::cerr << "event test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "event tests passed\n";
    return 0;
}
