#include "render_core/event.h"

#include "render_core/dom.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

constexpr std::size_t kStackListenerSnapshotCapacity = 8;

} // namespace

Event::Event(std::string type, bool bubbles, bool cancelable)
    : type_(std::move(type)), bubbles_(bubbles), cancelable_(cancelable) {}

EventKind Event::kind() const {
    return EventKind::Generic;
}

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

EventKind MouseEvent::kind() const {
    return EventKind::Mouse;
}

WheelEvent::WheelEvent(int client_x, int client_y, int delta_x_in, int delta_y_in)
    : MouseEvent("wheel", client_x, client_y, 0, 0),
      delta_x(delta_x_in),
      delta_y(delta_y_in) {}

EventKind WheelEvent::kind() const {
    return EventKind::Wheel;
}

struct EventTarget::ListenerStore {
    struct Listener {
        ListenerId id = 0;
        ListenerCallback callback;
        EventListenerOptions options;
        bool removed = false;
    };

    struct ListenerGroup {
        std::string type;
        std::vector<Listener> listeners;
    };

    std::vector<ListenerGroup> groups;
    ListenerId next_listener_id = 1;

    std::size_t listener_count() const {
        std::size_t count = 0;
        for (const ListenerGroup& group : groups) {
            for (const Listener& listener : group.listeners) {
                if (!listener.removed) {
                    ++count;
                }
            }
        }
        return count;
    }

    std::vector<Listener>& listeners_for_type(const std::string& type) {
        for (ListenerGroup& group : groups) {
            if (group.type == type) {
                return group.listeners;
            }
        }
        groups.push_back(ListenerGroup{type, {}});
        return groups.back().listeners;
    }

    std::vector<Listener>* find_listeners(const std::string& type) {
        for (ListenerGroup& group : groups) {
            if (group.type == type) {
                return &group.listeners;
            }
        }
        return nullptr;
    }

    Listener* find_listener(ListenerId id) {
        for (ListenerGroup& group : groups) {
            for (Listener& listener : group.listeners) {
                if (listener.id == id) {
                    return &listener;
                }
            }
        }
        return nullptr;
    }
};

EventTarget::EventTarget() = default;
EventTarget::~EventTarget() = default;

EventTarget::ListenerId EventTarget::add_event_listener(std::string type,
                                                        ListenerCallback callback,
                                                        EventListenerOptions options) {
    return add_event_listener_bounded(type, std::move(callback), 0, options);
}

EventTarget::ListenerId EventTarget::add_event_listener_bounded(const std::string& type,
                                                                ListenerCallback callback,
                                                                std::size_t max_listeners,
                                                                EventListenerOptions options) {
    if (!callback) {
        return 0;
    }
    if (!listeners_) {
        listeners_ = std::make_unique<ListenerStore>();
    }
    if (max_listeners > 0 && listeners_->listener_count() >= max_listeners) {
        return 0;
    }

    ListenerStore::Listener listener;
    listener.id = listeners_->next_listener_id++;
    listener.callback = std::move(callback);
    listener.options = options;
    const ListenerId id = listener.id;
    listeners_->listeners_for_type(type).push_back(std::move(listener));
    return id;
}

std::size_t EventTarget::event_listener_count() const {
    return listeners_ == nullptr ? 0 : listeners_->listener_count();
}

bool EventTarget::remove_event_listener(ListenerId id) {
    if (!listeners_) {
        return false;
    }
    for (EventTarget::ListenerStore::ListenerGroup& group : listeners_->groups) {
        for (ListenerStore::Listener& listener : group.listeners) {
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

    auto* listeners = listeners_->find_listeners(event.type());
    if (listeners == nullptr) {
        return;
    }

    std::array<ListenerId, kStackListenerSnapshotCapacity> stack_dispatch_ids{};
    std::size_t stack_dispatch_count = 0;
    std::vector<ListenerId> overflow_dispatch_ids;
    for (const ListenerStore::Listener& listener : *listeners) {
        if (listener.removed || listener.options.capture != capture_phase) {
            continue;
        }
        if (stack_dispatch_count < stack_dispatch_ids.size()) {
            stack_dispatch_ids[stack_dispatch_count++] = listener.id;
        } else {
            if (overflow_dispatch_ids.empty()) {
                overflow_dispatch_ids.reserve(listeners->size() - stack_dispatch_ids.size());
            }
            overflow_dispatch_ids.push_back(listener.id);
        }
    }

    const auto invoke_snapshot_listener = [this, &event, capture_phase](ListenerId listener_id) {
        ListenerStore::Listener* listener = listeners_->find_listener(listener_id);
        if (listener == nullptr || listener->removed || listener->options.capture != capture_phase) {
            return true;
        }
        ListenerCallback callback = listener->callback;
        const bool once = listener->options.once;
        callback(event);
        if (once) {
            if (ListenerStore::Listener* once_listener = listeners_->find_listener(listener_id)) {
                once_listener->removed = true;
            }
        }
        if (event.immediate_propagation_stopped()) {
            return false;
        }
        return true;
    };

    for (std::size_t index = 0; index < stack_dispatch_count; ++index) {
        if (!invoke_snapshot_listener(stack_dispatch_ids[index])) {
            break;
        }
    }
    if (!event.immediate_propagation_stopped()) {
        for (ListenerId listener_id : overflow_dispatch_ids) {
            if (!invoke_snapshot_listener(listener_id)) {
                break;
            }
        }
    }

    listeners = listeners_->find_listeners(event.type());
    if (listeners == nullptr) {
        return;
    }

    listeners->erase(std::remove_if(listeners->begin(), listeners->end(), [](const ListenerStore::Listener& listener) {
        return listener.removed;
    }), listeners->end());
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

} // namespace jellyframe
