#include "core/event.h"

#include "core/dom.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wearweb {

Event::Event(std::string type, bool bubbles, bool cancelable)
    : type_(std::move(type)), bubbles_(bubbles), cancelable_(cancelable) {}

const std::string& Event::type() const {
    return type_;
}

bool Event::bubbles() const {
    return bubbles_;
}

bool Event::cancelable() const {
    return cancelable_;
}

bool Event::default_prevented() const {
    return default_prevented_;
}

bool Event::propagation_stopped() const {
    return propagation_stopped_;
}

bool Event::immediate_propagation_stopped() const {
    return immediate_propagation_stopped_;
}

const Node* Event::target() const {
    return target_;
}

const Node* Event::current_target() const {
    return current_target_;
}

EventPhase Event::event_phase() const {
    return event_phase_;
}

void Event::prevent_default() {
    if (cancelable_) {
        default_prevented_ = true;
    }
}

void Event::stop_propagation() {
    propagation_stopped_ = true;
}

void Event::stop_immediate_propagation() {
    immediate_propagation_stopped_ = true;
    propagation_stopped_ = true;
}

void Event::reset_for_dispatch(const Node& target) {
    target_ = &target;
    current_target_ = nullptr;
    event_phase_ = EventPhase::None;
    default_prevented_ = false;
    propagation_stopped_ = false;
    immediate_propagation_stopped_ = false;
}

void Event::set_current_target(const Node& current_target, EventPhase phase) {
    current_target_ = &current_target;
    event_phase_ = phase;
    immediate_propagation_stopped_ = false;
}

void Event::clear_current_target() {
    current_target_ = nullptr;
    event_phase_ = EventPhase::None;
    immediate_propagation_stopped_ = false;
}

void Event::clear_immediate_stop() {
    immediate_propagation_stopped_ = false;
}

MouseEvent::MouseEvent(std::string type, int client_x_in, int client_y_in, int button_in, int buttons_in)
    : Event(std::move(type), true, true),
      client_x(client_x_in),
      client_y(client_y_in),
      button(button_in),
      buttons(buttons_in) {}

WheelEvent::WheelEvent(int client_x, int client_y, int delta_x_in, int delta_y_in)
    : MouseEvent("wheel", client_x, client_y, 0, 0),
      delta_x(delta_x_in),
      delta_y(delta_y_in) {}

struct EventTarget::ListenerStore {
    struct Listener {
        ListenerId id = 0;
        ListenerCallback callback;
        EventListenerOptions options;
        bool removed = false;
    };

    std::unordered_map<std::string, std::vector<Listener>> by_type;
    ListenerId next_listener_id = 1;
};

EventTarget::EventTarget() = default;
EventTarget::~EventTarget() = default;

EventTarget::ListenerId EventTarget::add_event_listener(std::string type,
                                                        ListenerCallback callback,
                                                        EventListenerOptions options) {
    if (!callback) {
        return 0;
    }
    if (!listeners_) {
        listeners_ = std::make_unique<ListenerStore>();
    }

    ListenerStore::Listener listener;
    listener.id = listeners_->next_listener_id++;
    listener.callback = std::move(callback);
    listener.options = options;
    listeners_->by_type[std::move(type)].push_back(std::move(listener));
    return listener.id;
}

bool EventTarget::remove_event_listener(ListenerId id) {
    if (!listeners_) {
        return false;
    }
    for (auto& entry : listeners_->by_type) {
        for (ListenerStore::Listener& listener : entry.second) {
            if (listener.id == id && !listener.removed) {
                listener.removed = true;
                return true;
            }
        }
    }
    return false;
}

void EventTarget::invoke_event_listeners(Event& event, bool capture_phase) const {
    if (!listeners_) {
        return;
    }

    const auto it = listeners_->by_type.find(event.type());
    if (it == listeners_->by_type.end()) {
        return;
    }

    auto& listeners = it->second;
    for (ListenerStore::Listener& listener : listeners) {
        if (listener.removed || listener.options.capture != capture_phase) {
            continue;
        }
        listener.callback(event);
        if (listener.options.once) {
            listener.removed = true;
        }
        if (event.immediate_propagation_stopped()) {
            break;
        }
    }

    listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [](const ListenerStore::Listener& listener) {
        return listener.removed;
    }), listeners.end());
}

bool dispatch_event(const Node& target, Event& event) {
    std::vector<const Node*> path;
    for (const Node* node = &target; node != nullptr; node = node->parent) {
        path.push_back(node);
    }

    event.reset_for_dispatch(target);

    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const Node* node = *it;
        if (node == &target) {
            break;
        }
        event.set_current_target(*node, EventPhase::Capturing);
        node->invoke_event_listeners(event, true);
        if (event.propagation_stopped()) {
            event.clear_current_target();
            return !event.default_prevented();
        }
    }

    event.set_current_target(target, EventPhase::AtTarget);
    target.invoke_event_listeners(event, true);
    if (!event.immediate_propagation_stopped()) {
        target.invoke_event_listeners(event, false);
    }

    if (event.bubbles() && !event.propagation_stopped()) {
        for (std::size_t index = 1; index < path.size(); ++index) {
            const Node* node = path[index];
            event.set_current_target(*node, EventPhase::Bubbling);
            node->invoke_event_listeners(event, false);
            if (event.propagation_stopped()) {
                break;
            }
        }
    }

    event.clear_current_target();
    return !event.default_prevented();
}

} // namespace wearweb
