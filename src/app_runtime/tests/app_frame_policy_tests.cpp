#include "app_runtime/app_frame_policy.h"

#include <cassert>

using namespace jellyframe;

namespace {

AppSystemStateSnapshot visible_state() {
    AppSystemStateSnapshot state;
    state.screen_on = true;
    state.low_power_mode = false;
    state.network_online = true;
    return state;
}

FrameLoopOptions full_budget() {
    FrameLoopOptions options;
    options.max_input_events_per_frame = 8;
    options.max_timer_callbacks_per_frame = 6;
    options.max_animation_callbacks_per_frame = 4;
    options.animation_frame_rate = 30;
    return options;
}

void foreground_visible_allows_normal_ui_work() {
    AppInstance app{1, "org.example.app", AppRole::App, AppLifecycleState::Foreground};
    const AppFramePolicy policy = app_frame_policy_for(app, visible_state());
    assert(policy.accepts_input);
    assert(policy.pumps_timers);
    assert(policy.pumps_animation);
    assert(policy.presents_frames);

    const FrameLoopOptions options = apply_app_frame_policy(full_budget(), policy);
    assert(options.max_input_events_per_frame == 8);
    assert(options.max_timer_callbacks_per_frame == 6);
    assert(options.max_animation_callbacks_per_frame == 4);
    assert(options.animation_frame_rate == 30);
}

void low_power_keeps_ui_responsive_but_stops_animation() {
    AppInstance app{1, "org.example.app", AppRole::App, AppLifecycleState::Foreground};
    AppSystemStateSnapshot state = visible_state();
    state.low_power_mode = true;

    const AppFramePolicy policy = app_frame_policy_for(app, state);
    assert(policy.accepts_input);
    assert(policy.pumps_timers);
    assert(!policy.pumps_animation);
    assert(policy.presents_frames);

    const FrameLoopOptions options = apply_app_frame_policy(full_budget(), policy);
    assert(options.max_input_events_per_frame == 8);
    assert(options.max_timer_callbacks_per_frame == 6);
    assert(options.max_animation_callbacks_per_frame == 0);
    assert(options.animation_frame_rate == 0);
}

void screen_off_pauses_ui_callbacks_and_presentation() {
    AppInstance app{1, "org.example.app", AppRole::App, AppLifecycleState::Foreground};
    AppSystemStateSnapshot state = visible_state();
    state.screen_on = false;

    const AppFramePolicy policy = app_frame_policy_for(app, state);
    assert(!policy.accepts_input);
    assert(!policy.pumps_timers);
    assert(!policy.pumps_animation);
    assert(!policy.presents_frames);

    const FrameLoopOptions options = apply_app_frame_policy(full_budget(), policy);
    assert(options.max_input_events_per_frame == 0);
    assert(options.max_timer_callbacks_per_frame == 0);
    assert(options.max_animation_callbacks_per_frame == 0);
    assert(options.animation_frame_rate == 0);
}

void suspended_instance_pauses_all_ui_work_until_resume() {
    AppInstance app{1, "org.example.app", AppRole::App, AppLifecycleState::Suspended};
    const AppFramePolicy suspended = app_frame_policy_for(app, visible_state());
    assert(!suspended.accepts_input);
    assert(!suspended.pumps_timers);
    assert(!suspended.pumps_animation);
    assert(!suspended.presents_frames);
    assert(suspended.resume_should_repaint);

    app.state = AppLifecycleState::Foreground;
    const AppFramePolicy resumed = app_frame_policy_for(app, visible_state());
    assert(app_resume_needs_repaint(suspended, resumed));
}

void background_services_default_to_paused_outside_foreground() {
    AppInstance app{1, "org.example.tracker", AppRole::App, AppLifecycleState::Suspended};
    const AppServiceActivityPolicy policy = app_service_activity_policy_for(app, visible_state());
    assert(!policy.network_fetch);
    assert(!policy.audio_playback);
    assert(!policy.sensor_sampling);
    assert(policy.should_pause_audio);
    assert(policy.should_throttle_sensors);
}

void background_policy_can_allow_audio_network_and_sensors_separately() {
    AppInstance app{1, "org.example.tracker", AppRole::App, AppLifecycleState::Suspended};
    AppSystemStateSnapshot state = visible_state();
    state.screen_on = false;

    AppBackgroundServicePolicy background;
    background.network_while_suspended = true;
    background.network_while_screen_off = true;
    background.audio_while_suspended = true;
    background.audio_while_screen_off = true;
    background.sensors_while_suspended = true;
    background.sensors_while_screen_off = false;

    AppServiceActivityPolicy policy = app_service_activity_policy_for(app, state, background);
    assert(policy.network_fetch);
    assert(policy.audio_playback);
    assert(!policy.sensor_sampling);
    assert(!policy.should_pause_audio);
    assert(policy.should_throttle_sensors);
}

void low_power_can_throttle_sensors_without_stopping_audio() {
    AppInstance app{1, "org.example.tracker", AppRole::App, AppLifecycleState::Foreground};
    AppSystemStateSnapshot state = visible_state();
    state.low_power_mode = true;

    AppBackgroundServicePolicy background;
    background.sensors_in_low_power = false;
    AppServiceActivityPolicy policy = app_service_activity_policy_for(app, state, background);
    assert(policy.network_fetch);
    assert(policy.audio_playback);
    assert(!policy.sensor_sampling);
    assert(!policy.should_pause_audio);
    assert(policy.should_throttle_sensors);

    background.sensors_in_low_power = true;
    policy = app_service_activity_policy_for(app, state, background);
    assert(policy.sensor_sampling);
    assert(policy.should_throttle_sensors);
}

} // namespace

int main() {
    foreground_visible_allows_normal_ui_work();
    low_power_keeps_ui_responsive_but_stops_animation();
    screen_off_pauses_ui_callbacks_and_presentation();
    suspended_instance_pauses_all_ui_work_until_resume();
    background_services_default_to_paused_outside_foreground();
    background_policy_can_allow_audio_network_and_sensors_separately();
    low_power_can_throttle_sensors_without_stopping_audio();
    return 0;
}
