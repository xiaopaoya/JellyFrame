#include "app_runtime/system_events.h"

#include <algorithm>

namespace jellyframe {

AppSystemEventQueue::AppSystemEventQueue(std::size_t capacity, std::size_t max_events_per_frame)
    : capacity_(capacity),
      max_events_per_frame_(max_events_per_frame) {}

bool AppSystemEventQueue::push_current(const AppRuntimeHost& host,
                                       AppSystemEventKind kind,
                                       const AppSystemStateSnapshot& snapshot) {
    const std::uint32_t app_instance_id = host.current_app_instance_id();
    if (app_instance_id == 0 || full()) {
        return false;
    }
    events_.push_back(AppSystemEvent{app_instance_id, kind, snapshot});
    return true;
}

AppSystemEventPumpResult AppSystemEventQueue::pump_current(const AppRuntimeHost& host,
                                                           std::vector<AppSystemEvent>& output) {
    AppSystemEventPumpResult result;
    const std::uint32_t active_instance = host.current_app_instance_id();
    const std::size_t limit = max_events_per_frame_ == 0 ? capacity_ : max_events_per_frame_;
    const std::size_t count = std::min(limit, events_.size());
    for (std::size_t index = 0; index < count; ++index) {
        const AppSystemEvent& event = events_[index];
        ++result.consumed;
        if (event.app_instance_id != active_instance || active_instance == 0) {
            ++result.stale;
            continue;
        }
        output.push_back(event);
        ++result.accepted;
    }
    events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(result.consumed));
    return result;
}

std::size_t AppSystemEventQueue::discard_app_instance(std::uint32_t app_instance_id) {
    const std::size_t before = events_.size();
    events_.erase(std::remove_if(events_.begin(), events_.end(), [app_instance_id](const AppSystemEvent& event) {
        return event.app_instance_id == app_instance_id;
    }), events_.end());
    return before - events_.size();
}

void AppSystemEventQueue::clear() {
    events_.clear();
}

} // namespace jellyframe
