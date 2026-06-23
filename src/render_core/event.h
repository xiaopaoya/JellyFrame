#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace jellyframe {

struct Node;

enum class EventPhase {
    None = 0,
    Capturing = 1,
    AtTarget = 2,
    Bubbling = 3,
};

enum class EventKind {
    Generic,
    Mouse,
    Wheel,
};

struct EventListenerOptions {
    bool capture = false;
    bool once = false;
};

class Event {
public:
    explicit Event(std::string type, bool bubbles = true, bool cancelable = true);
    virtual ~Event() = default;

    virtual EventKind kind() const;
    const std::string& type() const;
    bool bubbles() const;
    bool cancelable() const;
    bool default_prevented() const;
    bool propagation_stopped() const;
    bool immediate_propagation_stopped() const;

    const Node* target() const;
    const Node* current_target() const;
    EventPhase event_phase() const;

    void prevent_default();
    void stop_propagation();
    void stop_immediate_propagation();

private:
    friend class EventTarget;
    friend bool dispatch_event(const Node& target, Event& event);

    std::string type_;
    bool bubbles_ = true;
    bool cancelable_ = true;
    bool default_prevented_ = false;
    bool propagation_stopped_ = false;
    bool immediate_propagation_stopped_ = false;
    const Node* target_ = nullptr;
    const Node* current_target_ = nullptr;
    EventPhase event_phase_ = EventPhase::None;

    void reset_for_dispatch(const Node& target);
    void set_current_target(const Node& current_target, EventPhase phase);
    void clear_current_target();
    void clear_immediate_stop();
};

class MouseEvent : public Event {
public:
    MouseEvent(std::string type, int client_x, int client_y, int button = 0, int buttons = 0);

    EventKind kind() const override;

    int client_x = 0;
    int client_y = 0;
    int button = 0;
    int buttons = 0;
    bool alt_key = false;
    bool ctrl_key = false;
    bool meta_key = false;
    bool shift_key = false;
};

class WheelEvent : public MouseEvent {
public:
    WheelEvent(int client_x, int client_y, int delta_x, int delta_y);

    EventKind kind() const override;

    int delta_x = 0;
    int delta_y = 0;
    int delta_mode = 0;
};

class EventTarget {
public:
    using ListenerId = std::size_t;
    using ListenerCallback = std::function<void(Event&)>;

    EventTarget();
    ~EventTarget();

    ListenerId add_event_listener(std::string type, ListenerCallback callback, EventListenerOptions options = {});
    bool remove_event_listener(ListenerId id);

protected:
    void invoke_event_listeners(Event& event, bool capture_phase) const;

private:
    friend bool dispatch_event(const Node& target, Event& event);

    struct ListenerStore;
    mutable std::unique_ptr<ListenerStore> listeners_;
};

bool dispatch_event(const Node& target, Event& event);

} // namespace jellyframe
