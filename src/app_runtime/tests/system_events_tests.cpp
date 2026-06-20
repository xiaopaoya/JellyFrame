#include "app_runtime/system_events.h"

#include <cassert>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{4, 2, 4, 1024, 1});
}

AppSystemStateSnapshot make_snapshot(std::uint64_t time_ms, bool online) {
    AppSystemStateSnapshot snapshot;
    snapshot.unix_time_ms = time_ms;
    snapshot.timezone_offset_minutes = 480;
    snapshot.battery_percent = 72;
    snapshot.charging = true;
    snapshot.network_online = online;
    snapshot.screen_on = true;
    return snapshot;
}

void queue_requires_active_instance_and_capacity() {
    AppRuntimeHost host = make_host();
    AppSystemEventQueue queue(1, 1);
    assert(!queue.push_current(host, AppSystemEventKind::TimeChanged, make_snapshot(1, true)));
    assert(queue.try_push_current(host, AppSystemEventKind::TimeChanged, make_snapshot(1, true)) ==
           AppSystemEventPushStatus::EmptyInstance);
    assert(std::string(app_system_event_push_status_name(AppSystemEventPushStatus::EmptyInstance)) ==
           "empty-instance");

    const AppInstance app = host.launch("org.example.clock", AppRole::App);
    assert(queue.push_current(host, AppSystemEventKind::TimeChanged, make_snapshot(2, true)));
    assert(queue.full());
    assert(!queue.push_current(host, AppSystemEventKind::BatteryChanged, make_snapshot(3, true)));
    assert(queue.try_push_current(host, AppSystemEventKind::BatteryChanged, make_snapshot(3, true)) ==
           AppSystemEventPushStatus::QueueFull);

    std::vector<AppSystemEvent> accepted;
    const AppSystemEventPumpResult result = queue.pump_current(host, accepted);
    assert(result.consumed == 1);
    assert(result.accepted == 1);
    assert(result.stale == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().app_instance_id == app.id);
    assert(accepted.front().snapshot.unix_time_ms == 2);
}

void pump_is_frame_bounded_and_drops_stale_instances() {
    AppRuntimeHost host = make_host();
    AppSystemEventQueue queue(4, 2);
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    assert(queue.push_current(host, AppSystemEventKind::NetworkStatusChanged, make_snapshot(10, false)));
    assert(queue.push_current(host, AppSystemEventKind::BatteryChanged, make_snapshot(11, true)));

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(second.id == first.id + 1);
    assert(queue.push_current(host, AppSystemEventKind::ScreenStateChanged, make_snapshot(12, true)));

    std::vector<AppSystemEvent> accepted;
    AppSystemEventPumpResult result = queue.pump_current(host, accepted);
    assert(result.consumed == 2);
    assert(result.accepted == 0);
    assert(result.stale == 2);
    assert(accepted.empty());
    assert(queue.size() == 1);

    result = queue.pump_current(host, accepted);
    assert(result.consumed == 1);
    assert(result.accepted == 1);
    assert(result.stale == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().kind == AppSystemEventKind::ScreenStateChanged);
    assert(accepted.front().app_instance_id == second.id);
}

void discard_and_clear_are_bounded_helpers() {
    AppRuntimeHost host = make_host();
    AppSystemEventQueue queue(4, 4);
    const AppInstance app = host.launch("org.example.settings", AppRole::App);
    assert(queue.push_current(host, AppSystemEventKind::LowPowerModeChanged, make_snapshot(1, true)));
    assert(queue.push_current(host, AppSystemEventKind::TimezoneChanged, make_snapshot(2, true)));
    assert(queue.discard_app_instance(app.id) == 2);
    assert(queue.empty());

    assert(queue.push_current(host, AppSystemEventKind::BatteryChanged, make_snapshot(3, true)));
    queue.clear();
    assert(queue.empty());
}

} // namespace

int main() {
    queue_requires_active_instance_and_capacity();
    pump_is_frame_bounded_and_drops_stale_instances();
    discard_and_clear_are_bounded_helpers();
    return 0;
}
