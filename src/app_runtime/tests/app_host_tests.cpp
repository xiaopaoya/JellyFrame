#include "app_runtime/app_host.h"
#include "app_runtime/app_budget.h"
#include "app_runtime/system_events.h"
#include "render_core/software_renderer.h"

#include <cassert>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{
        4,
        2,
        4,
        4096,
        2,
    });
}

const std::vector<std::uint8_t>& tiny_jffont_bytes() {
    static const std::vector<std::uint8_t> bytes = {
        'J', 'F', 'F', 'O', 'N', 'T', '0', 0,
        0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x08, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0x05, 0x07, 0x06, 0x01,
        0x2d, 0x4e, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x01,
        0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
        0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10,
    };
    return bytes;
}

const BitmapFont& tiny_system_font() {
    static const std::uint8_t glyph_a_rows[] = {
        0x40,
        0xa0,
        0xe0,
        0xa0,
        0xa0,
    };
    static const BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 3, 5, 4, 1, glyph_a_rows},
    };
    static const BitmapFont font{glyphs, 1, 8, 4};
    return font;
}

void current_instance_submission_and_handles_are_scoped() {
    AppRuntimeHost host = make_host();
    assert(!host.submit_current(HostServiceJobKind::NetworkFetch).accepted);
    assert(host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 16) == 0);
    const std::vector<std::uint8_t>& bytes = tiny_jffont_bytes();
    assert(host.load_current_jffont(bytes.data(), bytes.size()).status == AppFontLoadStatus::EmptyInstance);

    const AppInstance app = host.launch("org.example.app", AppRole::App);
    const auto submitted = host.submit_current(HostServiceJobKind::NetworkFetch, 0, 3, 1000);
    assert(submitted.accepted);
    assert(host.requests().size() == 1);

    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(request.job_id == submitted.job_id);
    assert(request.app_instance_id == app.id);
    assert(request.priority == 3);
    assert(request.timeout_ms == 1000);

    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    assert(handle != 0);
    const HostHandleInfo* info = host.handles().lookup(handle);
    assert(info != nullptr);
    assert(info->app_instance_id == app.id);
}

void launch_cleans_previous_instance_state() {
    AppRuntimeHost host = make_host();
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    const auto submitted = host.submit_current(HostServiceJobKind::NetworkFetch);
    assert(submitted.accepted);
    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 32);
    assert(handle != 0);
    assert(host.push_completion(HostServiceCompletion{submitted.job_id,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      first.id}));

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(second.id == first.id + 1);
    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(host.handles().active_count() == 0);
}

void app_fonts_follow_active_instance_lifecycle() {
    AppRuntimeHost host = make_host();
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    const std::vector<std::uint8_t>& bytes = tiny_jffont_bytes();
    const AppFontLoadResult loaded = host.load_current_jffont(bytes.data(), bytes.size());
    assert(loaded.loaded());
    assert(host.fonts().size() == 1);
    assert(host.fonts().app_instance_id() == first.id);

    TextMetrics metrics;
    TextMeasureProvider provider = host.fonts().measure_provider();
    assert(provider.measure("A\xe4\xb8\xad", 8, 400, &metrics, provider.context));
    assert(metrics.width == 14);
    assert(metrics.line_height == 8);

    FrameBuffer frame(32, 16, Color{255, 255, 255, 255});
    TextPainter painter = host.fonts().painter();
    assert(painter.paint(frame,
                         Rect{0, 0, 32, 16},
                         Color{0, 0, 0, 255},
                         "A",
                         8,
                         400,
                         TextCommandAlign::Start,
                         true,
                         painter.context));
    assert(count_non_background_pixels(frame, Color{255, 255, 255, 255}) > 0);

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(second.id == first.id + 1);
    assert(host.fonts().empty());
    assert(host.load_current_jffont(bytes.data(), bytes.size()).loaded());

    const AppTeardownResult teardown = host.exit_current();
    assert(teardown.app_instance_id == second.id);
    assert(teardown.released_font_resources == 1);
    assert(host.fonts().empty());
}

void app_font_set_uses_system_font_before_app_supplement() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.fonts", AppRole::App);
    host.fonts().set_system_font(&tiny_system_font());
    assert(host.load_current_jffont(tiny_jffont_bytes().data(), tiny_jffont_bytes().size()).loaded());

    TextMetrics metrics;
    TextMeasureProvider provider = host.fonts().measure_provider();
    assert(provider.measure("A\xe4\xb8\xad", 8, 400, &metrics, provider.context));
    assert(metrics.width == 12);
    assert(metrics.line_height == 8);

    FrameBuffer frame(24, 16, Color{255, 255, 255, 255});
    TextPainter painter = host.fonts().painter();
    assert(painter.paint(frame,
                         Rect{0, 0, 24, 16},
                         Color{0, 0, 0, 255},
                         "A\xe4\xb8\xad",
                         8,
                         400,
                         TextCommandAlign::Start,
                         true,
                         painter.context));
    assert(count_non_background_pixels(frame, Color{255, 255, 255, 255}) > 0);
}

void app_font_set_can_attach_borrowed_jffont_view() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.borrowed-fonts", AppRole::App);
    const std::vector<std::uint8_t>& bytes = tiny_jffont_bytes();
    const AppFontLoadResult loaded = host.attach_current_jffont_view(bytes.data(), bytes.size());
    assert(loaded.loaded());
    assert(host.fonts().size() == 1);

    TextMetrics metrics;
    TextMeasureProvider provider = host.fonts().measure_provider();
    assert(provider.measure("\xe4\xb8\xad", 8, 400, &metrics, provider.context));
    assert(metrics.width == 8);
    assert(metrics.line_height == 8);

    const AppTeardownResult teardown = host.exit_current();
    assert(teardown.released_font_resources == 1);
    assert(host.fonts().empty());
}

void crash_current_tears_down_active_instance_state() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.crashy", AppRole::App);
    const auto request = host.submit_current(HostServiceJobKind::NetworkFetch);
    assert(request.accepted);
    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    assert(handle != 0);
    assert(host.load_current_jffont(tiny_jffont_bytes().data(), tiny_jffont_bytes().size()).loaded());
    assert(host.push_completion(HostServiceCompletion{request.job_id,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      app.id,
                                                      handle,
                                                      0,
                                                      64}));

    const AppTeardownResult result = host.crash_current();
    assert(result.crashed);
    assert(result.reason == AppTeardownReason::RuntimeError);
    assert(result.app_instance_id == app.id);
    assert(result.cancelled_requests == 1);
    assert(result.discarded_completions == 1);
    assert(result.released_handles == 1);
    assert(result.released_font_resources == 1);
    assert(host.current_app_instance_id() == 0);
    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(host.handles().active_count() == 0);
    assert(host.fonts().empty());
}

void terminate_current_supports_watchdog_and_user_kill_reasons() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.loop", AppRole::App);
    const auto request = host.submit_current(HostServiceJobKind::NetworkFetch);
    assert(request.accepted);
    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    assert(handle != 0);
    assert(host.push_completion(HostServiceCompletion{request.job_id,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      app.id,
                                                      handle,
                                                      0,
                                                      64}));

    const AppTeardownResult watchdog = host.terminate_current(AppTeardownReason::ScriptWatchdog);
    assert(watchdog.app_instance_id == app.id);
    assert(watchdog.reason == AppTeardownReason::ScriptWatchdog);
    assert(watchdog.crashed);
    assert(watchdog.cancelled_requests == 1);
    assert(watchdog.discarded_completions == 1);
    assert(watchdog.released_handles == 1);
    assert(host.current_app_instance_id() == 0);
    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(host.handles().active_count() == 0);

    const AppInstance user_app = host.launch("org.example.user-close", AppRole::App);
    const AppTeardownResult user_kill = host.terminate_current(AppTeardownReason::UserKill);
    assert(user_kill.app_instance_id == user_app.id);
    assert(user_kill.reason == AppTeardownReason::UserKill);
    assert(!user_kill.crashed);
}

void frame_pump_limits_completions_and_filters_stale_instances() {
    AppRuntimeHost host = make_host();
    const AppInstance first = host.launch("org.example.first", AppRole::App);
    const std::uint32_t stale_handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 128);
    assert(stale_handle != 0);

    const AppInstance second = host.launch("org.example.second", AppRole::App);
    assert(host.push_completion(HostServiceCompletion{1,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      first.id,
                                                      stale_handle,
                                                      0,
                                                      128}));
    assert(host.push_completion(HostServiceCompletion{2,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      second.id,
                                                      0,
                                                      0,
                                                      16}));
    assert(host.push_completion(HostServiceCompletion{3,
                                                      HostServiceJobKind::ImageDecode,
                                                      HostServiceStatus::Completed,
                                                      second.id,
                                                      0,
                                                      0,
                                                      16}));

    std::vector<HostServiceCompletion> accepted;
    const AppCompletionPumpResult first_pump = host.pump_frame_completions(accepted);
    assert(first_pump.consumed == 2);
    assert(first_pump.accepted == 1);
    assert(first_pump.stale == 1);
    assert(first_pump.released_stale_handles == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 2);
    assert(host.completions().size() == 1);

    accepted.clear();
    const AppCompletionPumpResult second_pump = host.pump_frame_completions(accepted);
    assert(second_pump.consumed == 1);
    assert(second_pump.accepted == 1);
    assert(second_pump.stale == 0);
    assert(accepted.size() == 1);
    assert(accepted.front().job_id == 3);
}

void frame_scratch_pump_reuses_completion_storage() {
    AppRuntimeHostOptions options{
        4,
        2,
        4,
        4096,
        1,
    };
    AppRuntimeHost host(options);
    AppFrameScratch scratch;
    scratch.reserve_from_options(options);

    const AppInstance app = host.launch("org.example.scratch", AppRole::App);
    assert(host.push_completion(HostServiceCompletion{1,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      app.id}));
    assert(host.push_completion(HostServiceCompletion{2,
                                                      HostServiceJobKind::StorageKv,
                                                      HostServiceStatus::Completed,
                                                      app.id}));

    const AppCompletionPumpResult pumped = host.pump_frame_completions(scratch);
    assert(pumped.accepted == 2);
    assert(scratch.accepted_completions.size() == 2);
    assert(scratch.accepted_completions.capacity() >= 2);
    assert(scratch.completion_batch.size() == 2);
    assert(scratch.completion_batch.capacity() >= 2);

    scratch.end_frame();
    assert(scratch.accepted_completions.empty());
    assert(scratch.completion_batch.empty());
    assert(scratch.accepted_completions.capacity() >= 2);
    assert(scratch.completion_batch.capacity() >= 2);
    scratch.release();
    assert(scratch.accepted_completions.capacity() == 0);
    assert(scratch.completion_batch.capacity() == 0);
}

void options_follow_host_capabilities() {
    HostDeviceCapabilities caps;
    caps.async.max_in_flight_jobs = 7;
    caps.async.max_completion_events_per_frame = 3;
    const AppRuntimeHostOptions options = AppRuntimeHost::options_from_capabilities(caps, 5, 2048);
    assert(options.max_in_flight_jobs == 7);
    assert(options.max_completion_events_per_frame == 3);
    assert(options.max_host_handles == 5);
    assert(options.max_host_handle_bytes == 2048);
    assert(options.max_app_fonts == 1);

    AppRuntimeHost host(options);
    assert(host.requests().capacity() == 7);
    assert(host.completions().capacity() == 7);
    assert(host.handles().capacity() == 5);
    assert(host.fonts().capacity() == 1);
    assert(host.max_completion_events_per_frame() == 3);
}

void budget_snapshot_reports_runtime_usage_and_caps() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.budget", AppRole::App);
    const auto request = host.submit_current(HostServiceJobKind::NetworkFetch);
    assert(request.accepted);
    const std::uint32_t handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 128);
    assert(handle != 0);
    assert(host.push_completion(HostServiceCompletion{request.job_id,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      app.id,
                                                      handle,
                                                      0,
                                                      128}));
    assert(host.attach_current_jffont_view(tiny_jffont_bytes().data(), tiny_jffont_bytes().size()).loaded());

    AppSystemEventQueue system_events(3, 2);
    AppSystemStateSnapshot snapshot;
    assert(system_events.push_current(host, AppSystemEventKind::BatteryChanged, snapshot));

    HostBudgets budgets;
    budgets.max_timers = 7;
    budgets.max_event_listeners = 11;
    budgets.max_detached_dom_nodes = 5;
    budgets.max_input_events_per_frame = 4;
    budgets.max_timer_callbacks_per_frame = 3;
    budgets.max_animation_callbacks_per_frame = 2;
    budgets.max_active_animations = 9;
    budgets.max_dom_nodes = 123;
    budgets.max_framebuffer_pixels = 456;

    FrameLoopPendingWork pending;
    pending.pending_input_events = 2;
    pending.pending_timer_callbacks = 1;
    pending.pending_animation_callbacks = 1;

    AppBudgetSnapshotInput input;
    input.host_budgets = &budgets;
    input.system_events = &system_events;
    input.pending_work = &pending;
    input.active_animations = 3;
    input.script_timers = 4;
    input.script_event_listeners = 6;
    input.detached_dom_nodes = 1;

    const AppBudgetSnapshot budget = collect_app_budget_snapshot(host, input);
    assert(budget.app_instance_id == app.id);
    assert(budget.role == AppRole::App);
    assert(budget.state == AppLifecycleState::Foreground);
    assert(budget.service_requests.used == 1);
    assert(budget.service_requests.limit == 4);
    assert(budget.service_completions.used == 1);
    assert(budget.service_completions.limit == 4);
    assert(budget.host_handles.used == 1);
    assert(budget.host_handles.limit == 4);
    assert(budget.host_handle_bytes.used == 128);
    assert(budget.host_handle_bytes.limit == 4096);
    assert(budget.app_fonts.used == 1);
    assert(budget.app_fonts.limit == 2);
    assert(budget.system_events.used == 1);
    assert(budget.system_events.limit == 3);
    assert(budget.input_events_per_frame.used == 2);
    assert(budget.input_events_per_frame.limit == 4);
    assert(budget.timer_callbacks_per_frame.used == 1);
    assert(budget.timer_callbacks_per_frame.limit == 3);
    assert(budget.animation_callbacks_per_frame.used == 1);
    assert(budget.animation_callbacks_per_frame.limit == 2);
    assert(budget.active_animations.used == 3);
    assert(budget.active_animations.limit == 9);
    assert(budget.script_timers.used == 4);
    assert(budget.script_timers.limit == 7);
    assert(budget.script_event_listeners.used == 6);
    assert(budget.script_event_listeners.limit == 11);
    assert(budget.detached_dom_nodes.used == 1);
    assert(budget.detached_dom_nodes.limit == 5);
    assert(budget.dom_nodes.limit == 123);
    assert(budget.framebuffer_pixels.limit == 456);
    assert(!app_budget_snapshot_has_exhausted_runtime_budget(budget));
}

void budget_snapshot_detects_exhausted_runtime_budget() {
    AppRuntimeHost host(AppRuntimeHostOptions{1, 1, 1, 64, 1});
    host.launch("org.example.exhausted", AppRole::App);
    assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);

    const AppBudgetSnapshot budget = collect_app_budget_snapshot(host);
    assert(budget.service_requests.exhausted());
    assert(app_budget_snapshot_has_exhausted_runtime_budget(budget));
}

void bad_app_teardown_leaves_next_app_clean_and_discards_stale_work() {
    AppRuntimeHost host(AppRuntimeHostOptions{8, 4, 4, 512, 1});
    AppSystemEventQueue system_events(8, 4);
    const AppInstance bad = host.launch("org.example.bad", AppRole::App);
    for (int i = 0; i < 3; ++i) {
        assert(host.submit_current(HostServiceJobKind::NetworkFetch).accepted);
        assert(system_events.push_current(host, AppSystemEventKind::TimeChanged, AppSystemStateSnapshot{}));
    }
    const std::uint32_t bad_handle = host.allocate_current_handle(HostServiceHandleKind::FetchResponse, 64);
    assert(bad_handle != 0);
    assert(host.push_completion(HostServiceCompletion{90,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      bad.id,
                                                      bad_handle,
                                                      0,
                                                      64}));

    const AppTeardownResult teardown = host.terminate_current(AppTeardownReason::ScriptWatchdog);
    assert(teardown.reason == AppTeardownReason::ScriptWatchdog);
    assert(teardown.crashed);
    assert(teardown.cancelled_requests == 3);
    assert(teardown.discarded_completions == 1);
    assert(teardown.released_handles == 1);
    assert(system_events.discard_app_instance(bad.id) == 3);

    const AppInstance shell = host.launch("org.jellyframe.system.launcher", AppRole::Launcher);
    assert(shell.id != bad.id);
    assert(host.requests().empty());
    assert(host.completions().empty());
    assert(host.handles().active_count() == 0);
    assert(system_events.empty());

    assert(host.push_completion(HostServiceCompletion{91,
                                                      HostServiceJobKind::NetworkFetch,
                                                      HostServiceStatus::Completed,
                                                      bad.id,
                                                      bad_handle,
                                                      0,
                                                      64}));
    assert(system_events.push_current(host, AppSystemEventKind::BatteryChanged, AppSystemStateSnapshot{}));

    AppFrameScratch scratch;
    scratch.reserve_from_options(AppRuntimeHostOptions{8, 4, 4, 512, 1});
    const AppCompletionPumpResult completion_pump = host.pump_frame_completions(scratch);
    assert(completion_pump.consumed == 1);
    assert(completion_pump.accepted == 0);
    assert(completion_pump.stale == 1);
    assert(completion_pump.released_stale_handles == 0);

    std::vector<AppSystemEvent> accepted_events;
    const AppSystemEventPumpResult event_pump = system_events.pump_current(host, accepted_events);
    assert(event_pump.consumed == 1);
    assert(event_pump.accepted == 1);
    assert(event_pump.stale == 0);
    assert(accepted_events.size() == 1);
    assert(accepted_events.front().app_instance_id == shell.id);
}

} // namespace

int main() {
    current_instance_submission_and_handles_are_scoped();
    launch_cleans_previous_instance_state();
    app_fonts_follow_active_instance_lifecycle();
    app_font_set_uses_system_font_before_app_supplement();
    app_font_set_can_attach_borrowed_jffont_view();
    crash_current_tears_down_active_instance_state();
    terminate_current_supports_watchdog_and_user_kill_reasons();
    frame_pump_limits_completions_and_filters_stale_instances();
    frame_scratch_pump_reuses_completion_storage();
    options_follow_host_capabilities();
    budget_snapshot_reports_runtime_usage_and_caps();
    budget_snapshot_detects_exhausted_runtime_budget();
    bad_app_teardown_leaves_next_app_clean_and_discards_stale_work();
    return 0;
}
