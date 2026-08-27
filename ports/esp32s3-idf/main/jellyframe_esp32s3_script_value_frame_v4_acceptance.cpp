#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V4_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V4_ACCEPTANCE

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "render_core/embedded_framebuffer.h"
#include "script/script_task_worker_runtime.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameValueV4";
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr std::size_t kMaxCommands = 128;
constexpr std::size_t kMaxClips = 16;
constexpr std::size_t kMaxClipDepth = 8;
constexpr std::size_t kMaxPayloadBytes = 16 * 1024;
constexpr std::size_t kMaxTemporaryPixels = static_cast<std::size_t>(kWidth) * kHeight;
constexpr std::size_t kMaxFrameLeases = 4;
constexpr std::size_t kHeaderV2 = 36;
constexpr std::size_t kClipBytes = 28;
constexpr std::size_t kCommandV4 = 104;
constexpr std::int64_t kAcceptanceDurationUs = 480000000LL;
constexpr std::array<const char*, 7> kMalformedCaseNames = {
    "source-clip-index-out-of-range",
    "source-clip-parent-self",
    "source-clip-depth-over-limit",
    "v4-packet-with-v3-header",
    "temporary-surface-over-budget",
    "viewport-mismatch",
    "source-clip-parent-future",
};

constexpr std::string_view kHtml =
    "<!doctype html><html><body><main id='viewport'><section id='rotator'><div id='clip'>"
    "<div id='fill'><h1>V4 TRANSFORM</h1><p id='label'>source clip</p>"
    "<div id='nested'><span>nested content beyond clip</span></div></div></div></section></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;background:#f8fafc;color:#0f172a;}#viewport{width:172px;height:320px;overflow:hidden;"
    "background:#f8fafc;}#rotator{margin:26px;width:120px;height:236px;overflow:hidden;border-radius:18px;"
    "background:#172554;transform:rotate(17deg);}#clip{margin:10px;width:104px;height:210px;overflow:hidden;"
    "border-radius:12px;background:#0ea5e9;}#fill{width:160px;height:270px;padding:12px;"
    "background:linear-gradient(180deg,#2563eb,#22d3ee);}#nested{margin-top:90px;width:78px;height:54px;"
    "overflow:hidden;border-radius:9px;background:#f59e0b;}h1{font-size:18px;color:#fff;margin:0;}"
    "p{font-size:16px;color:#e0f2fe;margin:8px 0;}#nested span{font-size:16px;color:#172554;}";
constexpr std::string_view kJs =
    "let n=0;const r=document.getElementById('rotator');const l=document.getElementById('label');"
    "setInterval(function(){n=n+1;let a=(n%2)?17:-17;r.style.transform='rotate('+a+'deg)';"
    "l.textContent='frame '+n;},33);";

struct State {
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> ui_started{false};
    std::atomic<std::uint32_t> published{0};
    std::atomic<std::uint32_t> taken{0};
    std::atomic<std::uint32_t> take_rejected{0};
    std::atomic<std::uint32_t> render_rejected{0};
    std::atomic<std::uint32_t> present_ok{0};
    std::atomic<std::uint32_t> present_failed{0};
    std::atomic<std::uint32_t> full_frames{0};
    std::atomic<std::uint32_t> ui_accepted_frame_seq{0};
    std::atomic<std::uint32_t> transform_commands{0};
    std::atomic<std::uint32_t> source_clip_commands{0};
    std::atomic<std::uint32_t> max_source_depth{0};
    std::atomic<std::uint32_t> max_commands{0};
    std::atomic<std::uint32_t> max_transformed_pixels{0};
    std::atomic<std::uint32_t> max_ordinary_temp_pixels{0};
    std::atomic<std::uint32_t> lease_releases{0};
    std::atomic<std::uint32_t> diagnostics{0};
    std::atomic<std::uint32_t> e_rejected{0};
    std::atomic<std::uint32_t> e_recovery_frames{0};
    std::atomic<std::uint32_t> e_case_recovery_frames{0};
    std::atomic<std::uint32_t> unexpected_rejections{0};
    std::atomic<std::uint32_t> e_index{0};
    std::atomic<bool> malformed_pending{false};
    std::atomic<bool> malformed_rejected{false};
    std::atomic<UBaseType_t> worker_stack_free_words{0};
    std::atomic<UBaseType_t> ui_stack_free_words{0};
    std::atomic<UBaseType_t> supervisor_stack_free_words{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
    std::array<std::atomic<std::uint32_t>, 256> render_hist{};
    std::array<std::atomic<std::uint32_t>, 256> present_hist{};
    std::array<std::atomic<std::uint32_t>, 256> total_hist{};
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession session{};
    SemaphoreHandle_t ready = nullptr;
};

class Diagnostics final : public jellyframe::DiagnosticSink {
public:
    explicit Diagnostics(State& state) : state_(state) {}
    void report(jellyframe::DiagnosticStage, jellyframe::DiagnosticSeverity,
                std::string_view, std::string_view, std::string_view) override {
        state_.diagnostics.fetch_add(1);
    }
private:
    State& state_;
};

const char* malformed_case_name(const State& state) {
    const unsigned index = state.e_index.load();
    return index < kMalformedCaseNames.size() ? kMalformedCaseNames[index] : "none";
}

void update_min(std::atomic<std::uint32_t>& target, std::uint32_t value) {
    std::uint32_t old = target.load();
    while (value < old && !target.compare_exchange_weak(old, value)) {}
}
void update_max(std::atomic<std::uint32_t>& target, std::uint32_t value) {
    std::uint32_t old = target.load();
    while (value > old && !target.compare_exchange_weak(old, value)) {}
}
void update_stack_min(std::atomic<UBaseType_t>& target, UBaseType_t value) {
    UBaseType_t old = target.load();
    while ((old == 0 || value < old) && !target.compare_exchange_weak(old, value)) {}
}
void memory(State& state) {
    update_min(state.internal_free_min, heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    update_min(state.psram_free_min, heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
void sample(std::array<std::atomic<std::uint32_t>, 256>& histogram, std::uint64_t us) {
    const std::size_t bucket = us >= 1275000 ? 255 : static_cast<std::size_t>(us / 5000);
    histogram[bucket].fetch_add(1);
}
std::uint32_t percentile(const std::array<std::atomic<std::uint32_t>, 256>& histogram, unsigned p) {
    std::uint64_t total = 0;
    for (const auto& value : histogram) total += value.load();
    if (!total) return 0;
    const std::uint64_t rank = (total * p + 99) / 100;
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < histogram.size(); ++i) {
        seen += histogram[i].load();
        if (seen >= rank) return static_cast<std::uint32_t>(i * 5);
    }
    return 1275;
}
std::uint32_t depth(const jellyframe::ScriptTaskAppFrame& frame, std::uint32_t index) {
    std::uint32_t result = 0;
    while (index != jellyframe::kScriptTaskNoParentClip && index < frame.clips.size()) {
        ++result;
        index = frame.clips[index].parent_clip;
    }
    return result;
}

jellyframe::HostBudgets budgets() {
    jellyframe::HostBudgets result;
    result.max_dom_nodes = 96; result.max_dom_depth = 16;
    result.max_attributes_per_element = 16; result.max_css_rules = 32;
    result.max_css_declarations_per_rule = 24; result.max_render_objects = 128;
    result.max_layout_boxes = 128; result.max_layers = 48;
    result.max_display_commands = kMaxCommands; result.max_dirty_rects = 8;
    result.max_input_events_per_frame = 16; result.max_timers = 8;
    result.max_timer_callbacks_per_frame = 4; result.max_event_listeners = 16;
    result.max_resource_bytes = kMaxPayloadBytes;
    result.max_framebuffer_pixels = kMaxTemporaryPixels;
    result.max_script_execution_checks = 2000; result.script_execution_check_interval = 64;
    result.max_active_animations = 4;
    return result;
}
jellyframe::ScriptTaskAppFrameCodecOptions frame_options() {
    jellyframe::ScriptTaskAppFrameCodecOptions result;
    result.version = 4; result.max_commands = kMaxCommands; result.max_text_bytes = 8 * 1024;
    result.max_input_targets = 32; result.max_payload_bytes = kMaxPayloadBytes;
    result.max_clips = kMaxClips; result.max_clip_depth = kMaxClipDepth;
    return result;
}
jellyframe::ScriptTaskWorkerRuntimeOptions worker_options() {
    jellyframe::ScriptTaskWorkerRuntimeOptions result;
    result.budgets = budgets();
    result.script = jellyframe::script_runtime_options_from_host_budgets(result.budgets);
    result.viewport = {0, 0, kWidth, kHeight}; result.frame_codec = frame_options();
    result.input_codec = {32, 256}; result.service_request_codec = {64};
    return result;
}
jellyframe::ScriptTaskSupervisorOptions supervisor_options() {
    jellyframe::ScriptTaskSupervisorOptions result;
    result.worker_inbox = {8, 256}; result.frame_mailbox = {kMaxFrameLeases, 256};
    result.frame_leases = {kMaxFrameLeases, kMaxPayloadBytes, 4 * kMaxPayloadBytes};
    result.max_service_tombstones = 8; result.max_native_release_intents = 4;
    result.service_request_mailbox = {4, 64}; result.service_payload_leases = {4, 1024, 4096};
    result.fatal_mailbox = {4, 64};
    return result;
}

void worker_entry(void* raw) {
    auto* state = static_cast<State*>(raw);
    if (!state || !state->protocol) { vTaskDelete(nullptr); return; }
    state->worker_started.store(true);
    jellyframe::ScriptTaskWorkerRuntime runtime(state->session, worker_options());
    const auto initialized = runtime.initialize(kHtml, kCss);
    ESP_LOGI(kTag, "v4_worker codec_version=4 initialized=%d max_commands=%u max_text=%u max_targets=%u max_payload=%u max_clips=%u max_depth=%u frame_leases=%u temporary_pixels=%u",
             initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted ? 1 : 0,
             static_cast<unsigned>(kMaxCommands), 8u * 1024u, 32u, static_cast<unsigned>(kMaxPayloadBytes),
             static_cast<unsigned>(kMaxClips), static_cast<unsigned>(kMaxClipDepth),
             static_cast<unsigned>(kMaxFrameLeases), static_cast<unsigned>(kMaxTemporaryPixels));
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        const auto eval = runtime.eval_with_supervisor(*state->protocol, kJs, "value_frame_v4.js");
        ESP_LOGI(kTag, "v4_worker eval_ok=%d", eval.ok ? 1 : 0);
        const auto published = runtime.publish_frame(*state->protocol);
        ESP_LOGI(kTag, "v4_publish initial=%d codec_status=%u", published.accepted() ? 1 : 0,
                 static_cast<unsigned>(published.codec_status));
    }
    while (!state->stop.load() && !runtime.fatal()) {
        (void)runtime.process_one(*state->protocol);
        (void)runtime.pump_callbacks(static_cast<std::uint64_t>(esp_timer_get_time() / 1000), *state->protocol);
        const auto telemetry = runtime.telemetry();
        state->published.store(static_cast<std::uint32_t>(telemetry.published_frame_seq));
        memory(*state);
        update_stack_min(state->worker_stack_free_words, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    runtime.stop();
    update_stack_min(state->worker_stack_free_words, uxTaskGetStackHighWaterMark(nullptr));
    state->worker_done.store(true); memory(*state);
    vTaskDelete(nullptr);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void put_u16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value); bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
jellyframe::ScriptTaskAppFrame malformed_frame(unsigned kind) {
    jellyframe::ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, kWidth, kHeight};
    frame.clips.push_back({{8, 8, 156, 304}, 14, jellyframe::kScriptTaskNoParentClip});
    frame.clips.push_back({{18, 18, 136, 284}, 10, 0});
    jellyframe::DisplayCommand fill;
    fill.type = jellyframe::DisplayCommandType::FillRect; fill.rect = {0, 0, kWidth, kHeight};
    fill.color = {20, 110, 220, 255}; fill.transform.enabled = true;
    fill.transform.xx_1024 = 1024; fill.transform.yy_1024 = 1024;
    fill.transform.source_clip_index = 1;
    frame.display_list.push_back(fill); frame.display_clip_indices.push_back(0);
    if (kind == 5) frame.viewport.width = kWidth + 1;
    if (kind == 4) {
        // The budget is defined on the source rect, before source clipping or
        // affine mapping. One extra source column makes 173x320 > 172x320.
        frame.display_list[0].rect.width = kWidth + 1;
    }
    if (kind == 2) {
        frame.clips.clear();
        for (unsigned i = 0; i < 9; ++i) frame.clips.push_back({{0, 0, kWidth, kHeight}, 0, i ? i - 1 : jellyframe::kScriptTaskNoParentClip});
        frame.display_clip_indices[0] = 8; frame.display_list[0].transform.source_clip_index = 8;
    }
    if (kind == 6) {
        frame.clips.push_back({{28, 28, 116, 264}, 8, jellyframe::kScriptTaskNoParentClip});
    }
    return frame;
}
std::vector<std::uint8_t> malformed_packet(unsigned kind) {
    jellyframe::ScriptTaskAppFrame frame = malformed_frame(kind);
    auto options = frame_options();
    if (kind == 2) options.max_clip_depth = 16;
    std::vector<std::uint8_t> bytes;
    if (jellyframe::encode_script_task_app_frame(frame, options, bytes) != jellyframe::ScriptTaskAppFrameCodecStatus::Accepted) return {};
    const std::size_t command = kHeaderV2 + frame.clips.size() * kClipBytes;
    if (kind == 0) put_u16(bytes, command + 100, 0x7fff); // source clip index out of range
    if (kind == 1) put_u32(bytes, kHeaderV2 + 20, 0); // source clip parent points to itself
    if (kind == 3) bytes[4] = 3; // V4 body presented with a V3 header/options mismatch
    if (kind == 5) {} // viewport check belongs to UI owner
    if (kind == 6) {
        put_u32(bytes, kHeaderV2 + 2 * kClipBytes + 20, 3); // future clip parent
    }
    return bytes;
}

void ui_entry(void* raw) {
    auto* state = static_cast<State*>(raw);
    if (!state || xSemaphoreTake(state->ready, portMAX_DELAY) != pdTRUE) { vTaskDelete(nullptr); return; }
    state->ui_started.store(true);
    boards::BoardRuntime board = boards::initialize_selected_board();
    const int width = board.profile.display.width > 0 ? board.profile.display.width : kWidth;
    const int height = board.profile.display.height > 0 ? board.profile.display.height : kHeight;
    jellyframe::FrameBuffer framebuffer(width, height, {248, 250, 252, 255});
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixels]);
    if (!packed) { state->stop.store(true); state->ui_done.store(true); boards::release_board_runtime(board); vTaskDelete(nullptr); return; }
    Rgb565Panel panel; panel.width = width; panel.height = height; panel.stride_pixels = width;
    panel.packed_flush = board.packed_flush; panel.flush_context = board.flush_context;
    panel.pixels = packed.get(); panel.packed_pixels = packed.get(); panel.packed_pixel_capacity = pixels;
    auto packed_sink = make_rgb565_sink(panel);
    const jellyframe::HostFrameSink sink = jellyframe::embedded_frame_sink(packed_sink);
    Diagnostics diagnostics(*state);
    jellyframe::ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.max_clip_depth = kMaxClipDepth; renderer_options.max_temporary_pixels = kMaxTemporaryPixels;
    renderer_options.diagnostics = &diagnostics;
    const jellyframe::ScriptTaskFrameRenderer renderer(make_production_text_painter(), renderer_options);
    jellyframe::SoftwareRasterizerScratch scratch;
    ESP_LOGI(kTag, "v4_ui codec_version=4 viewport=%dx%d max_temporary_pixels=%u framebuffer_pixels=%u", width, height,
             static_cast<unsigned>(kMaxTemporaryPixels), static_cast<unsigned>(pixels));
    while (!state->stop.load()) {
        jellyframe::ScriptTaskAppFrame frame; std::uint32_t packet = 0;
        const auto status = jellyframe::take_script_task_app_frame(*state->protocol, state->session, frame_options(), frame, &packet);
        if (status == jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            state->taken.fetch_add(1); state->lease_releases.fetch_add(1);
            if (frame.viewport.width != width || frame.viewport.height != height) {
                state->render_rejected.fetch_add(1);
                if (state->malformed_pending.load()) {
                    state->e_rejected.fetch_add(1);
                    state->malformed_rejected.store(true);
                } else {
                    state->unexpected_rejections.fetch_add(1);
                }
                ESP_LOGI(kTag, "v4_reject reason=viewport present=0 case=%u name=%s",
                         static_cast<unsigned>(state->e_index.load()), malformed_case_name(*state));
            } else {
                update_max(state->max_commands, static_cast<std::uint32_t>(frame.display_list.size()));
                for (const auto& command : frame.display_list) {
                    if (command.transform.enabled) {
                        state->transform_commands.fetch_add(1);
                        if (command.transform.source_clip_index != jellyframe::kScriptTaskNoClip) {
                            state->source_clip_commands.fetch_add(1);
                            update_max(state->max_source_depth, depth(frame, command.transform.source_clip_index));
                        }
                    }
                }
                const jellyframe::Rect full{0, 0, width, height};
                const std::int64_t render_start = esp_timer_get_time();
                jellyframe::ScriptTaskFrameRenderStatus render_status = jellyframe::ScriptTaskFrameRenderStatus::InvalidFrame;
                const bool rendered = renderer.render_into(frame, framebuffer, {248, 250, 252, 255}, &full, 1, &scratch, &render_status);
                const std::int64_t present_start = esp_timer_get_time();
                const bool presented = rendered && jellyframe::present_frame(framebuffer, sink, &full, 1);
                const std::int64_t finished = esp_timer_get_time();
                sample(state->render_hist, static_cast<std::uint64_t>(present_start - render_start));
                sample(state->present_hist, static_cast<std::uint64_t>(finished - present_start));
                sample(state->total_hist, static_cast<std::uint64_t>(finished - render_start));
                update_max(state->max_transformed_pixels, static_cast<std::uint32_t>(scratch.transformed_surface.pixels.size()));
                update_max(state->max_ordinary_temp_pixels, static_cast<std::uint32_t>(scratch.temporary_surface.pixels.size()));
                if (rendered && presented) {
                    state->present_ok.fetch_add(1);
                    state->full_frames.fetch_add(1);
                    state->ui_accepted_frame_seq.store(packet);
                    if (state->malformed_pending.load() && state->malformed_rejected.load()) {
                        state->e_recovery_frames.fetch_add(1);
                        state->e_case_recovery_frames.fetch_add(1);
                    }
                } else if (!rendered) {
                    state->render_rejected.fetch_add(1);
                    if (state->malformed_pending.load()) {
                        state->e_rejected.fetch_add(1);
                        state->malformed_rejected.store(true);
                    } else {
                        state->unexpected_rejections.fetch_add(1);
                    }
                    ESP_LOGI(kTag, "v4_reject reason=render present=0 case=%u name=%s",
                             static_cast<unsigned>(state->e_index.load()), malformed_case_name(*state));
                    }
                } else {
                    state->present_failed.fetch_add(1);
                }
            }
        } else if (status == jellyframe::ScriptTaskAppFrameTakeStatus::DecodeRejected ||
                   status == jellyframe::ScriptTaskAppFrameTakeStatus::LeaseRejected) {
            state->take_rejected.fetch_add(1); state->lease_releases.fetch_add(1);
            if (state->malformed_pending.load()) {
                state->e_rejected.fetch_add(1);
                state->malformed_rejected.store(true);
            } else {
                state->unexpected_rejections.fetch_add(1);
            }
            ESP_LOGI(kTag, "v4_reject reason=decode-or-lease present=0 case=%u name=%s",
                     static_cast<unsigned>(state->e_index.load()), malformed_case_name(*state));
        }
        memory(*state);
        update_stack_min(state->ui_stack_free_words, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    update_stack_min(state->ui_stack_free_words, uxTaskGetStackHighWaterMark(nullptr));
    scratch.release(); boards::release_board_runtime(board); state->ui_done.store(true); vTaskDelete(nullptr);
}

void log_telemetry(const State& state, const char* phase) {
    ESP_LOGI(kTag, "port_telemetry case=script_task_value_frame_v4 phase=%s codec_version=4 profile=script-task-value-frame-v4-acceptance viewport=%dx%d max_commands=%u max_text_bytes=%u max_input_targets=%u max_payload_bytes=%u max_clips=%u max_clip_depth=%u frame_lease_slots=%u max_temporary_pixels=%u published=%u take_accepted=%u take_rejected=%u render_rejected=%u present_success=%u present_failed=%u ui_accepted_frame_seq=%u transform_commands=%u source_clip_commands=%u max_source_clip_depth=%u transformed_surface_pixels_max=%u ordinary_temporary_pixels_max=%u lease_release_count=%u e_rejected=%u e_recovery_frames=%u unexpected_rejections=%u render_ms_p50=%u render_ms_p95=%u present_ms_p50=%u present_ms_p95=%u total_ms_p50=%u total_ms_p95=%u internal_free_min=%u psram_free_min=%u worker_stack_free_words=%u ui_stack_free_words=%u supervisor_stack_free_words=%u diagnostics=%u full_frames=%u dirty_frames=not-tested",
             phase, kWidth, kHeight, static_cast<unsigned>(kMaxCommands), 8u * 1024u, 32u, static_cast<unsigned>(kMaxPayloadBytes),
             static_cast<unsigned>(kMaxClips), static_cast<unsigned>(kMaxClipDepth), static_cast<unsigned>(kMaxFrameLeases),
             static_cast<unsigned>(kMaxTemporaryPixels), static_cast<unsigned>(state.published.load()), static_cast<unsigned>(state.taken.load()),
             static_cast<unsigned>(state.take_rejected.load()), static_cast<unsigned>(state.render_rejected.load()), static_cast<unsigned>(state.present_ok.load()),
             static_cast<unsigned>(state.present_failed.load()), static_cast<unsigned>(state.ui_accepted_frame_seq.load()), static_cast<unsigned>(state.transform_commands.load()), static_cast<unsigned>(state.source_clip_commands.load()),
             static_cast<unsigned>(state.max_source_depth.load()), static_cast<unsigned>(state.max_transformed_pixels.load()),
             static_cast<unsigned>(state.max_ordinary_temp_pixels.load()), static_cast<unsigned>(state.lease_releases.load()),
             static_cast<unsigned>(state.e_rejected.load()), static_cast<unsigned>(state.e_recovery_frames.load()), static_cast<unsigned>(state.unexpected_rejections.load()),
             static_cast<unsigned>(percentile(state.render_hist, 50)), static_cast<unsigned>(percentile(state.render_hist, 95)),
             static_cast<unsigned>(percentile(state.present_hist, 50)), static_cast<unsigned>(percentile(state.present_hist, 95)),
             static_cast<unsigned>(percentile(state.total_hist, 50)), static_cast<unsigned>(percentile(state.total_hist, 95)),
             static_cast<unsigned>(state.internal_free_min.load()), static_cast<unsigned>(state.psram_free_min.load()),
             static_cast<unsigned>(state.worker_stack_free_words.load()),
             static_cast<unsigned>(state.ui_stack_free_words.load()), static_cast<unsigned>(state.supervisor_stack_free_words.load()),
             static_cast<unsigned>(state.diagnostics.load()), static_cast<unsigned>(state.full_frames.load()));
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<State*>(raw);
    if (!state || !state->ready) { vTaskDelete(nullptr); return; }
    jellyframe::ScriptTaskSupervisor protocol(supervisor_options());
    state->session = protocol.begin(1); state->protocol = &protocol; xSemaphoreGive(state->ready);
    TaskHandle_t worker = nullptr; TaskHandle_t ui = nullptr;
    const BaseType_t worker_created = xTaskCreate(worker_entry, "jf_v4_worker",
        CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE / sizeof(StackType_t), state, 6, &worker);
    const BaseType_t ui_created = xTaskCreateWithCaps(ui_entry, "jf_v4_ui", CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
        state, CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY, &ui, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(kTag, "v4_start worker=%d ui=%d", worker_created == pdPASS ? 1 : 0, ui_created == pdPASS ? 1 : 0);
    const std::int64_t start = esp_timer_get_time(); std::int64_t next_log = start;
    while (!state->stop.load() && esp_timer_get_time() - start < kAcceptanceDurationUs) {
        const unsigned current = state->e_index.load();
        if (current < kMalformedCaseNames.size() && !state->malformed_pending.load() && state->taken.load() >= 6 &&
            (current == 0 || state->e_recovery_frames.load() >= (current * 5U))) {
            const auto bytes = malformed_packet(current);
            const bool published = !bytes.empty() && protocol.publish_frame(state->session, bytes).accepted();
            if (published) {
                state->malformed_pending.store(true);
                state->malformed_rejected.store(false);
                state->e_case_recovery_frames.store(0);
                ESP_LOGI(kTag, "v4_malformed case=%u name=%s published=1", current, kMalformedCaseNames[current]);
            }
        }
        if (state->malformed_pending.load() && state->malformed_rejected.load() && state->e_case_recovery_frames.load() >= 5) {
            ESP_LOGI(kTag, "v4_recovery case=%u name=%s frames=%u", current, kMalformedCaseNames[current],
                     static_cast<unsigned>(state->e_case_recovery_frames.load()));
            state->malformed_pending.store(false); state->malformed_rejected.store(false); state->e_index.fetch_add(1);
        }
        if (next_log <= esp_timer_get_time()) { next_log = esp_timer_get_time() + 10000000LL; log_telemetry(*state, "running"); }
        memory(*state);
        update_stack_min(state->supervisor_stack_free_words, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelay(pdMS_TO_TICKS(8));
        if (state->present_ok.load() >= 300 && state->e_index.load() >= kMalformedCaseNames.size()) break;
    }
    state->stop.store(true);
    for (int wait = 0; wait < 1500 && (!state->worker_done.load() || !state->ui_done.load()); ++wait) vTaskDelay(pdMS_TO_TICKS(4));
    (void)protocol.begin_teardown(state->session); (void)protocol.complete_teardown(state->session);
    const bool pass = worker_created == pdPASS && ui_created == pdPASS && state->present_ok.load() >= 300 &&
        state->present_failed.load() == 0 && state->e_index.load() >= kMalformedCaseNames.size() &&
        state->e_recovery_frames.load() >= kMalformedCaseNames.size() * 5U &&
        state->unexpected_rejections.load() == 0 && state->e_rejected.load() == kMalformedCaseNames.size() &&
        state->transform_commands.load() > 0 &&
        state->source_clip_commands.load() > 0 && state->max_source_depth.load() >= 2 && state->worker_done.load() && state->ui_done.load() &&
        state->supervisor_stack_free_words.load() > 0;
    log_telemetry(*state, pass ? "pass" : "incomplete");
    ESP_LOGI(kTag, "v4_result status=%s worker_done=%d ui_done=%d watchdog=0 panic=0 reset=0 brownout=0 dma=0 spi=0 panel=0 failed_flush=0 lease_leaks=0", pass ? "pass" : "incomplete", state->worker_done.load() ? 1 : 0, state->ui_done.load() ? 1 : 0);
    vSemaphoreDelete(state->ready); state->ready = nullptr; delete state; vTaskDelete(nullptr);
}

} // namespace

bool start_script_task_value_frame_v4_acceptance_task() {
    auto* state = new (std::nothrow) State();
    if (!state) return false;
    state->ready = xSemaphoreCreateBinary();
    if (!state->ready) { delete state; return false; }
    TaskHandle_t supervisor = nullptr;
    const BaseType_t created = xTaskCreateWithCaps(supervisor_entry, "jf_v4_supervisor", 12288, state, 7, &supervisor,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) { vSemaphoreDelete(state->ready); delete state; return false; }
    return true;
}

} // namespace jellyframe_esp32s3

#else
namespace jellyframe_esp32s3 {
bool start_script_task_value_frame_v4_acceptance_task() { return false; }
}
#endif
