#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V2_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V2_ACCEPTANCE

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"

#include "app_runtime/script_task_contract.h"
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

constexpr const char* kTag = "JellyFrameValueV2";
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr std::size_t kMaxCommands = 128;
constexpr std::size_t kMaxClips = 16;
constexpr std::size_t kMaxClipDepth = 8;
constexpr std::size_t kMaxPayloadBytes = 16 * 1024;
constexpr std::size_t kMaxTemporaryPixels = static_cast<std::size_t>(kWidth) * kHeight;
constexpr std::uint64_t kSoakUs = 10ULL * 60ULL * 1000ULL * 1000ULL;

#if CONFIG_JELLYFRAME_ESP32S3_SCRIPT_VALUE_FRAME_V2_DIRTY_ACCEPTANCE
constexpr bool kDirtyAcceptance = true;
constexpr std::string_view kHtml =
    "<!doctype html><html><body><main id='outer'><section id='inner'>"
    "<div id='fill'><div id='corner'>corner</div><h1>V2 CLIP</h1><p id='label'>dirty text</p>"
    "<div class='badge'>value-only</div></div></section></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;background:#ffffff;color:#ffffff;}"
    "#outer{margin:10px;width:152px;height:300px;overflow:hidden;border-radius:12px;background:#ffffff;}"
    "#inner{margin:8px;width:136px;height:284px;overflow:hidden;border-radius:18px;"
    "background:#0f172a;box-shadow:0 3px 8px #64748b;}"
    "#fill{width:160px;height:340px;padding:14px;background:linear-gradient(180deg,#2563eb,#0ea5e9);}"
    "#corner{width:44px;height:26px;border-radius:8px;background:#f59e0b;color:#172554;}"
    "h1{margin:0;font-size:22px;color:#ffffff;}p{font-size:16px;color:#e0f2fe;}"
    ".badge{margin-top:120px;padding:8px;border-radius:10px;background:#172554;color:#ffffff;}";
constexpr std::string_view kJs =
    "let tick=0;const corner=document.getElementById('corner');const label=document.getElementById('label');"
    "setInterval(function(){tick=tick+1;if((tick%2)===0){label.textContent='text '+(tick%100);}"
    "else{corner.style.background=((tick%4)===1)?'#f59e0b':'#ec4899';}},50);";
#else
constexpr bool kDirtyAcceptance = false;
constexpr std::string_view kHtml =
    "<!doctype html><html><body><main id='outer'><section id='inner'>"
    "<div id='fill'><h1>V2 CLIP</h1><p id='label'>rounded overflow</p>"
    "<div class='badge'>value-only</div></div></section></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;background:#ffffff;color:#ffffff;}"
    "#outer{margin:10px;width:152px;height:300px;overflow:hidden;border-radius:12px;background:#ffffff;}"
    "#inner{margin:8px;width:136px;height:284px;overflow:hidden;border-radius:18px;"
    "background:#0f172a;box-shadow:0 3px 8px #64748b;}"
    "#fill{width:160px;height:340px;padding:14px;background:linear-gradient(180deg,#2563eb,#0ea5e9);}"
    "h1{margin:0;font-size:22px;color:#ffffff;}p{font-size:16px;color:#e0f2fe;}"
    ".badge{margin-top:180px;padding:8px;border-radius:10px;background:#172554;color:#ffffff;}";
constexpr std::string_view kJs =
    "let tick=0;const inner=document.getElementById('inner');const label=document.getElementById('label');"
    "setInterval(function(){tick=tick+1;inner.style.transform='translateY(-'+(tick%18)+'px)';"
    "label.textContent='frame '+tick;},50);";
#endif

struct V2State {
    V2State() {
        for (auto& value : render_present_histogram) value.store(0);
        for (auto& value : render_histogram) value.store(0);
        for (auto& value : present_histogram) value.store(0);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> malformed_injected{false};
    std::atomic<bool> malformed_rejected{false};
    std::atomic<bool> recovered_after_malformed{false};
    std::atomic<std::uint32_t> published{0};
    std::atomic<std::uint32_t> take_accepted{0};
    std::atomic<std::uint32_t> take_rejected{0};
    std::atomic<std::uint32_t> render_rejected{0};
    std::atomic<std::uint32_t> present_ok{0};
    std::atomic<std::uint32_t> present_failed{0};
    std::atomic<std::uint32_t> full_frames{0};
    std::atomic<std::uint32_t> dirty_frames{0};
    std::atomic<std::uint32_t> dirty_text_frames{0};
    std::atomic<std::uint32_t> dirty_corner_frames{0};
    std::atomic<std::uint32_t> last_published_seq{0};
    std::atomic<std::uint32_t> last_accepted_seq{0};
    std::atomic<std::uint32_t> max_clip_count{0};
    std::atomic<std::uint32_t> max_clip_depth{0};
    std::atomic<std::uint32_t> max_commands{0};
    std::atomic<std::uint32_t> max_clip_runs{0};
    std::atomic<std::uint32_t> total_clip_runs{0};
    std::atomic<std::uint32_t> max_temporary_pixels{0};
    std::atomic<std::uint32_t> diagnostics{0};
    std::atomic<std::uint64_t> rounded_clip_mask_pixels{0};
    std::atomic<std::uint32_t> rounded_clip_rectangular_fast_paths{0};
    std::atomic<std::uint64_t> rounded_clip_opaque_direct_pixels{0};
    std::atomic<std::uint64_t> rounded_clip_blended_pixels{0};
    std::atomic<std::uint64_t> rounded_clip_full_coverage_pixels{0};
    std::atomic<std::uint64_t> rounded_clip_coverage_sampled_pixels{0};
    std::atomic<std::uint32_t> rounded_clip_budget_rejections{0};
    std::atomic<std::uint32_t> rounded_clip_allocation_rejections{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
    std::array<std::atomic<std::uint32_t>, 256> render_present_histogram;
    std::array<std::atomic<std::uint32_t>, 256> render_histogram;
    std::array<std::atomic<std::uint32_t>, 256> present_histogram;
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession session{};
    SemaphoreHandle_t ready = nullptr;
};

class CountingDiagnostics final : public jellyframe::DiagnosticSink {
public:
    explicit CountingDiagnostics(V2State& state) : state_(state) {}

    void report(jellyframe::DiagnosticStage,
                jellyframe::DiagnosticSeverity,
                std::string_view,
                std::string_view,
                std::string_view) override {
        state_.diagnostics.fetch_add(1);
    }

private:
    V2State& state_;
};

void update_minimum(std::atomic<std::uint32_t>& target, std::uint32_t value) {
    std::uint32_t observed = target.load();
    while (value < observed && !target.compare_exchange_weak(observed, value)) {}
}

void update_memory(V2State& state) {
    update_minimum(state.internal_free_min, static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    update_minimum(state.psram_free_min, static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void update_maximum(std::atomic<std::uint32_t>& target, std::uint32_t value) {
    std::uint32_t observed = target.load();
    while (value > observed && !target.compare_exchange_weak(observed, value)) {}
}

void add_sample(std::array<std::atomic<std::uint32_t>, 256>& histogram, std::uint64_t microseconds) {
    const std::size_t bucket = static_cast<std::size_t>(
        microseconds >= 1275000 ? 255 : microseconds / 5000);
    histogram[bucket].fetch_add(1);
}

std::uint32_t percentile_ms(const std::array<std::atomic<std::uint32_t>, 256>& histogram,
                            std::uint32_t percentile) {
    std::uint64_t total = 0;
    for (const auto& value : histogram) total += value.load();
    if (total == 0) return 0;
    const std::uint64_t rank = (total * percentile + 99U) / 100U;
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < histogram.size(); ++index) {
        cumulative += histogram[index].load();
        if (cumulative >= rank) return static_cast<std::uint32_t>(index * 5U);
    }
    return 255;
}

std::uint32_t clip_run_count(const jellyframe::ScriptTaskAppFrame& frame) {
    if (frame.display_list.empty()) return 0;
    std::uint32_t runs = 0;
    std::uint32_t previous = jellyframe::kScriptTaskNoClip;
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        const std::uint32_t clip = frame.display_clip_indices.empty()
            ? jellyframe::kScriptTaskNoClip
            : frame.display_clip_indices[index];
        if (index == 0 || clip != previous) ++runs;
        previous = clip;
    }
    return runs;
}

jellyframe::HostBudgets budgets() {
    jellyframe::HostBudgets result;
    result.max_dom_nodes = 96;
    result.max_dom_depth = 16;
    result.max_attributes_per_element = 16;
    result.max_css_rules = 32;
    result.max_css_declarations_per_rule = 24;
    result.max_render_objects = 128;
    result.max_layout_boxes = 128;
    result.max_layers = 48;
    result.max_display_commands = kMaxCommands;
    result.max_dirty_rects = 8;
    result.max_input_events_per_frame = 16;
    result.max_timers = 8;
    result.max_timer_callbacks_per_frame = 4;
    result.max_event_listeners = 16;
    result.max_resource_bytes = kMaxPayloadBytes;
    result.max_framebuffer_pixels = kMaxTemporaryPixels;
    result.max_script_execution_checks = 2000;
    result.script_execution_check_interval = 64;
    result.max_active_animations = 4;
    return result;
}

jellyframe::ScriptTaskAppFrameCodecOptions frame_options() {
    jellyframe::ScriptTaskAppFrameCodecOptions result;
    result.version = 2;
    result.max_commands = kMaxCommands;
    result.max_text_bytes = 8 * 1024;
    result.max_input_targets = 32;
    result.max_payload_bytes = kMaxPayloadBytes;
    result.max_clips = kMaxClips;
    result.max_clip_depth = kMaxClipDepth;
    return result;
}

jellyframe::ScriptTaskWorkerRuntimeOptions worker_options() {
    jellyframe::ScriptTaskWorkerRuntimeOptions result;
    result.budgets = budgets();
    result.script = jellyframe::jerryscript_runtime_options_from_host_budgets(result.budgets);
    result.viewport = {0, 0, kWidth, kHeight};
    result.frame_codec = frame_options();
    result.input_codec = {32, 256};
    result.service_request_codec = {64};
    return result;
}

jellyframe::ScriptTaskSupervisorOptions supervisor_options() {
    jellyframe::ScriptTaskSupervisorOptions result;
    result.input_mailbox = {8, 256};
    result.worker_mailbox = {8, 256};
    result.frame_leases = {4, kMaxPayloadBytes, 4 * kMaxPayloadBytes};
    result.max_service_tombstones = 8;
    result.max_native_release_intents = 4;
    result.service_request_mailbox = {4, 64};
    result.service_payload_leases = {4, 1024, 4096};
    result.fatal_mailbox = {4, 64};
    return result;
}

void worker_entry(void* raw) {
    auto* state = static_cast<V2State*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    jellyframe::ScriptTaskWorkerRuntime runtime(state->session, worker_options());
    const auto initialized = runtime.initialize(kHtml, kCss);
    ESP_LOGI(kTag, "v2_worker initialized=%d codec_version=2 max_clips=%u max_depth=%u payload_limit=%u",
             initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted ? 1 : 0,
             static_cast<unsigned>(kMaxClips), static_cast<unsigned>(kMaxClipDepth),
             static_cast<unsigned>(kMaxPayloadBytes));
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        const auto eval = runtime.eval_with_supervisor(*state->protocol, kJs, "value_frame_v2.js");
        ESP_LOGI(kTag, "v2_worker eval_ok=%d", eval.ok ? 1 : 0);
        const auto published = runtime.publish_frame(*state->protocol);
        ESP_LOGI(kTag, "v2_publish initial accepted=%d codec_status=%u",
                 published.accepted() ? 1 : 0, static_cast<unsigned>(published.codec_status));
    }
    while (!state->stop.load() && !runtime.fatal()) {
        (void)runtime.process_one(*state->protocol);
        (void)runtime.pump_callbacks(static_cast<std::uint64_t>(esp_timer_get_time() / 1000),
                                     *state->protocol);
        const auto telemetry = runtime.telemetry();
        state->published.store(static_cast<std::uint32_t>(telemetry.published_frame_seq));
        state->last_published_seq.store(telemetry.published_frame_seq);
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    if (runtime.fatal()) {
        (void)runtime.publish_fatal(*state->protocol);
        ESP_LOGE(kTag, "v2_worker fatal=1 reason=%u", static_cast<unsigned>(runtime.fatal_record().reason));
    }
    runtime.stop();
    state->worker_done.store(true);
    update_memory(*state);
    vTaskDelete(nullptr);
}

bool publish_malformed_clip_packet(jellyframe::ScriptTaskSupervisor& protocol,
                                   const jellyframe::ScriptAppSession& session) {
    jellyframe::ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, kWidth, kHeight};
    frame.clips.push_back({{8, 8, 156, 304}, 12, jellyframe::kScriptTaskNoParentClip});
    jellyframe::DisplayCommand fill;
    fill.type = jellyframe::DisplayCommandType::FillRect;
    fill.rect = {0, 0, kWidth, kHeight};
    fill.color = {20, 120, 240, 255};
    frame.display_list.push_back(fill);
    frame.display_clip_indices.push_back(0);
    std::vector<std::uint8_t> encoded;
    if (jellyframe::encode_script_task_app_frame(frame, frame_options(), encoded) !=
        jellyframe::ScriptTaskAppFrameCodecStatus::Accepted) {
        return false;
    }
    constexpr std::size_t kHeaderBytesV2 = 36;
    constexpr std::size_t kClipBytes = 28;
    constexpr std::size_t kClipIndexOffset = 6;
    const std::size_t command_offset = kHeaderBytesV2 + kClipBytes;
    if (encoded.size() < command_offset + kClipIndexOffset + 2) return false;
    encoded[command_offset + kClipIndexOffset] = 7;
    encoded[command_offset + kClipIndexOffset + 1] = 0;
    return protocol.publish_frame(session, encoded).accepted();
}

std::uint32_t clip_depth(const jellyframe::ScriptTaskAppFrame& frame, std::uint32_t index) {
    std::uint32_t depth = 0;
    while (index != jellyframe::kScriptTaskNoParentClip && index < frame.clips.size()) {
        ++depth;
        index = frame.clips[index].parent_clip;
    }
    return depth;
}

void ui_entry(void* raw) {
    auto* state = static_cast<V2State*>(raw);
    if (state == nullptr || xSemaphoreTake(state->ready, portMAX_DELAY) != pdTRUE) {
        vTaskDelete(nullptr);
        return;
    }
    boards::BoardRuntime board = boards::initialize_selected_board();
    const int width = board.profile.display.width > 0 ? board.profile.display.width : kWidth;
    const int height = board.profile.display.height > 0 ? board.profile.display.height : kHeight;
    jellyframe::FrameBuffer framebuffer(width, height, {255, 255, 255, 255});
    const std::size_t pixels = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixels]);
    Rgb565Panel panel;
    panel.width = width;
    panel.height = height;
    panel.stride_pixels = width;
    panel.packed_flush = board.packed_flush;
    panel.flush_context = board.flush_context;
    panel.pixels = packed.get();
    panel.packed_pixels = packed.get();
    panel.packed_pixel_capacity = pixels;
#if CONFIG_JELLYFRAME_ESP32S3_SCRIPT_VALUE_FRAME_V2_DIRTY_ACCEPTANCE
    auto packed_sink = make_packed_rgb565_sink(panel);
    const jellyframe::HostFrameSink sink = jellyframe::embedded_packed_rgb565_sink(packed_sink);
#else
    auto packed_sink = make_rgb565_sink(panel);
    const jellyframe::HostFrameSink sink = jellyframe::embedded_frame_sink(packed_sink);
#endif
    CountingDiagnostics diagnostics(*state);
    jellyframe::ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.max_clip_depth = kMaxClipDepth;
    renderer_options.max_temporary_pixels = kMaxTemporaryPixels;
    renderer_options.diagnostics = &diagnostics;
    jellyframe::SoftwareRasterizerStatistics rasterizer_statistics;
    renderer_options.rasterizer_statistics = &rasterizer_statistics;
    const jellyframe::ScriptTaskFrameRenderer renderer(make_production_text_painter(), renderer_options);
    jellyframe::SoftwareRasterizerScratch scratch;
    ESP_LOGI(kTag, "v2_ui ready viewport=%dx%d temporary_pixels_limit=%u hardware_display=%d",
             width, height, static_cast<unsigned>(kMaxTemporaryPixels), board.hardware_display_ready ? 1 : 0);
    while (!state->stop.load()) {
        jellyframe::ScriptTaskAppFrame frame;
        std::uint32_t packet_sequence = 0;
        const auto take_status = jellyframe::take_script_task_app_frame(
            *state->protocol, state->session, frame_options(), frame, &packet_sequence);
        if (take_status == jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            if (frame.viewport.width != width || frame.viewport.height != height) {
                state->render_rejected.fetch_add(1);
                ESP_LOGW(kTag, "v2_render reject=viewport frame=%dx%d target=%dx%d",
                         frame.viewport.width, frame.viewport.height, width, height);
            } else {
                update_maximum(state->max_clip_count, static_cast<std::uint32_t>(frame.clips.size()));
                update_maximum(state->max_commands, static_cast<std::uint32_t>(frame.display_list.size()));
                const std::uint32_t clip_runs = clip_run_count(frame);
                update_maximum(state->max_clip_runs, clip_runs);
                state->total_clip_runs.fetch_add(clip_runs);
                for (std::uint32_t index = 0; index < frame.clips.size(); ++index) {
                    update_maximum(state->max_clip_depth, clip_depth(frame, index));
                }
                const jellyframe::Rect full{0, 0, width, height};
                const bool first_frame = state->present_ok.load() == 0;
                const bool corner_dirty = kDirtyAcceptance && !first_frame &&
                    (state->take_accepted.load() % 2U == 0U);
                const jellyframe::Rect dirty = corner_dirty
                    ? jellyframe::Rect{18, 18, 68, 68}
                    : jellyframe::Rect{24, 72, 126, 48};
                const jellyframe::Rect* repaint = (!kDirtyAcceptance || first_frame) ? &full : &dirty;
                const std::size_t repaint_count = 1;
                const std::int64_t render_start = esp_timer_get_time();
                jellyframe::ScriptTaskFrameRenderStatus render_status =
                    jellyframe::ScriptTaskFrameRenderStatus::InvalidFrame;
                const bool rendered = renderer.render_into(frame, framebuffer, {255, 255, 255, 255},
                                                           repaint, repaint_count, &scratch, &render_status);
                const std::int64_t present_start = esp_timer_get_time();
                bool presented = false;
                if (rendered) {
                    presented = jellyframe::present_frame(framebuffer, sink, repaint, repaint_count);
                }
                const std::int64_t finished = esp_timer_get_time();
                add_sample(state->render_histogram, static_cast<std::uint64_t>(present_start - render_start));
                add_sample(state->render_present_histogram, static_cast<std::uint64_t>(finished - render_start));
                add_sample(state->present_histogram, static_cast<std::uint64_t>(finished - present_start));
                update_maximum(state->max_temporary_pixels, static_cast<std::uint32_t>(
                    scratch.temporary_surface.pixels.size()));
                state->rounded_clip_mask_pixels.store(rasterizer_statistics.rounded_clip_mask_pixels);
                state->rounded_clip_rectangular_fast_paths.store(
                    static_cast<std::uint32_t>(rasterizer_statistics.rounded_clip_rectangular_fast_paths));
                state->rounded_clip_opaque_direct_pixels.store(
                    rasterizer_statistics.rounded_clip_opaque_direct_pixels);
                state->rounded_clip_blended_pixels.store(
                    rasterizer_statistics.rounded_clip_blended_pixels);
                state->rounded_clip_full_coverage_pixels.store(
                    rasterizer_statistics.rounded_clip_full_coverage_pixels);
                state->rounded_clip_coverage_sampled_pixels.store(
                    rasterizer_statistics.rounded_clip_coverage_sampled_pixels);
                state->rounded_clip_budget_rejections.store(
                    static_cast<std::uint32_t>(rasterizer_statistics.rounded_clip_budget_rejections));
                state->rounded_clip_allocation_rejections.store(
                    static_cast<std::uint32_t>(rasterizer_statistics.rounded_clip_allocation_rejections));
                if (!rendered) {
                    state->render_rejected.fetch_add(1);
                    ESP_LOGW(kTag, "v2_render reject=status-%u packet_seq=%" PRIu32,
                             static_cast<unsigned>(render_status), packet_sequence);
                } else if (!presented) {
                    state->present_failed.fetch_add(1);
                    ESP_LOGE(kTag, "v2_present ok=0 packet_seq=%" PRIu32, packet_sequence);
                } else {
                    state->take_accepted.fetch_add(1);
                    state->present_ok.fetch_add(1);
                    if (kDirtyAcceptance && !first_frame) {
                        state->dirty_frames.fetch_add(1);
                        if (corner_dirty) state->dirty_corner_frames.fetch_add(1);
                        else state->dirty_text_frames.fetch_add(1);
                    } else {
                        state->full_frames.fetch_add(1);
                    }
                    state->last_accepted_seq.store(packet_sequence);
                    if (state->malformed_rejected.load()) state->recovered_after_malformed.store(true);
                }
            }
        } else if (take_status != jellyframe::ScriptTaskAppFrameTakeStatus::NoFrame) {
            state->take_rejected.fetch_add(1);
            if (take_status == jellyframe::ScriptTaskAppFrameTakeStatus::DecodeRejected &&
                state->malformed_injected.load()) {
                state->malformed_rejected.store(true);
                ESP_LOGI(kTag, "v2_rejection expected=malformed-clip-index present_skipped=1");
            } else {
                ESP_LOGW(kTag, "v2_take reject=status-%u", static_cast<unsigned>(take_status));
            }
        }
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    scratch.release();
    boards::release_board_runtime(board);
    state->ui_done.store(true);
    vTaskDelete(nullptr);
}

void log_telemetry(const V2State& state, const char* status) {
    ESP_LOGI(kTag,
             "script_value_frame_v2_telemetry status=%s codec_version=2 max_clips=%u max_clip_depth=%u "
             "payload_limit=%u lease_limit=%u temporary_pixels_limit=%u published_frame_seq=%" PRIu32 " "
             "ui_accepted_frame_seq=%" PRIu32 " published=%u take_accepted=%u take_rejected=%u "
             "render_rejected=%u present_ok=%u present_failed=%u malformed_injected=%d malformed_rejected=%d "
             "recovered_after_malformed=%d clip_count_max=%u clip_depth_max=%u command_count_max=%u "
             "temporary_pixels_max=%u diagnostics=%u dirty_acceptance=%d dirty_frames=%u full_frames=%u "
             "dirty_text_frames=%u dirty_corner_frames=%u "
             "rounded_clip_mask_pixels=%" PRIu64 " rounded_clip_rectangular_fast_paths=%u "
             "rounded_clip_opaque_direct_pixels=%" PRIu64 " rounded_clip_blended_pixels=%" PRIu64 " "
             "rounded_clip_full_coverage_pixels=%" PRIu64 " rounded_clip_coverage_sampled_pixels=%" PRIu64 " "
             "rounded_clip_budget_rejections=%u rounded_clip_allocation_rejections=%u "
             "render_present_ms_p50=%u render_present_ms_p95=%u render_ms_p50=%u render_ms_p95=%u "
             "present_ms_p50=%u present_ms_p95=%u clip_runs_max=%u clip_runs_total=%u "
             "worker_done=%d ui_done=%d "
             "internal_free_min=%u psram_free_min=%u",
             status, static_cast<unsigned>(kMaxClips), static_cast<unsigned>(kMaxClipDepth),
             static_cast<unsigned>(kMaxPayloadBytes), static_cast<unsigned>(kMaxPayloadBytes),
             static_cast<unsigned>(kMaxTemporaryPixels), state.last_published_seq.load(),
             state.last_accepted_seq.load(), static_cast<unsigned>(state.published.load()),
             static_cast<unsigned>(state.take_accepted.load()), static_cast<unsigned>(state.take_rejected.load()),
             static_cast<unsigned>(state.render_rejected.load()), static_cast<unsigned>(state.present_ok.load()),
             static_cast<unsigned>(state.present_failed.load()), state.malformed_injected.load() ? 1 : 0,
             state.malformed_rejected.load() ? 1 : 0, state.recovered_after_malformed.load() ? 1 : 0,
             static_cast<unsigned>(state.max_clip_count.load()), static_cast<unsigned>(state.max_clip_depth.load()),
             static_cast<unsigned>(state.max_commands.load()), static_cast<unsigned>(state.max_temporary_pixels.load()),
             static_cast<unsigned>(state.diagnostics.load()), kDirtyAcceptance ? 1 : 0,
             static_cast<unsigned>(state.dirty_frames.load()), static_cast<unsigned>(state.full_frames.load()),
             static_cast<unsigned>(state.dirty_text_frames.load()),
             static_cast<unsigned>(state.dirty_corner_frames.load()),
             state.rounded_clip_mask_pixels.load(),
             static_cast<unsigned>(state.rounded_clip_rectangular_fast_paths.load()),
             state.rounded_clip_opaque_direct_pixels.load(),
             state.rounded_clip_blended_pixels.load(),
             state.rounded_clip_full_coverage_pixels.load(),
             state.rounded_clip_coverage_sampled_pixels.load(),
             static_cast<unsigned>(state.rounded_clip_budget_rejections.load()),
             static_cast<unsigned>(state.rounded_clip_allocation_rejections.load()),
             static_cast<unsigned>(percentile_ms(state.render_present_histogram, 50)),
             static_cast<unsigned>(percentile_ms(state.render_present_histogram, 95)),
             static_cast<unsigned>(percentile_ms(state.render_histogram, 50)),
             static_cast<unsigned>(percentile_ms(state.render_histogram, 95)),
             static_cast<unsigned>(percentile_ms(state.present_histogram, 50)),
             static_cast<unsigned>(percentile_ms(state.present_histogram, 95)),
             static_cast<unsigned>(state.max_clip_runs.load()),
             static_cast<unsigned>(state.total_clip_runs.load()),
             state.worker_done.load() ? 1 : 0, state.ui_done.load() ? 1 : 0,
             static_cast<unsigned>(state.internal_free_min.load()),
             static_cast<unsigned>(state.psram_free_min.load()));
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<V2State*>(raw);
    if (state == nullptr || state->ready == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    jellyframe::ScriptTaskSupervisor protocol(supervisor_options());
    state->session = protocol.begin(1);
    state->protocol = &protocol;
    xSemaphoreGive(state->ready);
    TaskHandle_t worker = nullptr;
    TaskHandle_t ui = nullptr;
    const BaseType_t worker_created = xTaskCreate(
        worker_entry, "jf_v2_worker",
        CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE / sizeof(StackType_t), state, 6, &worker);
    const BaseType_t ui_created = xTaskCreateWithCaps(
        ui_entry, "jf_v2_ui", CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE, state,
        CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY, &ui, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(kTag, "script_value_frame_v2_start worker=%d ui=%d soak_s=600",
             worker_created == pdPASS ? 1 : 0, ui_created == pdPASS ? 1 : 0);
    const std::int64_t start = esp_timer_get_time();
    std::int64_t next_log = start;
    while (!state->stop.load() && esp_timer_get_time() - start < static_cast<std::int64_t>(kSoakUs)) {
        if (!state->malformed_injected.load() && state->take_accepted.load() >= 4) {
            const bool injected = publish_malformed_clip_packet(protocol, state->session);
            state->malformed_injected.store(injected);
            ESP_LOGI(kTag, "v2_malformed inject=%d kind=clip-index-out-of-range", injected ? 1 : 0);
        }
        if (next_log <= esp_timer_get_time()) {
            next_log = esp_timer_get_time() + 10000000;
            log_telemetry(*state, "running");
        }
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    state->stop.store(true);
    // Runtime teardown frees the worker's private DOM/JerryScript objects and
    // may take longer than a single panel present on the target device.
    for (int wait = 0; wait < 1250 && (!state->worker_done.load() || !state->ui_done.load()); ++wait) {
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    (void)protocol.begin_teardown(state->session);
    (void)protocol.complete_teardown(state->session);
    const bool pass = worker_created == pdPASS && ui_created == pdPASS &&
        state->take_accepted.load() >= 300 && state->take_rejected.load() == 1 &&
        state->render_rejected.load() == 0 && state->present_failed.load() == 0 &&
        state->malformed_rejected.load() && state->recovered_after_malformed.load() &&
        state->max_clip_count.load() >= 2 && state->max_clip_depth.load() >= 2 &&
        state->diagnostics.load() == 0 && state->worker_done.load() && state->ui_done.load();
    log_telemetry(*state, pass ? "pass" : "incomplete");
    vSemaphoreDelete(state->ready);
    state->ready = nullptr;
    delete state;
    vTaskDelete(nullptr);
}

} // namespace

bool start_script_task_value_frame_v2_acceptance_task() {
    auto* state = new (std::nothrow) V2State();
    if (state == nullptr) return false;
    state->ready = xSemaphoreCreateBinary();
    if (state->ready == nullptr) {
        delete state;
        return false;
    }
    TaskHandle_t task = nullptr;
    const BaseType_t created = xTaskCreate(supervisor_entry, "jf_v2_supervisor",
                                           12288 / sizeof(StackType_t), state, 7, &task);
    if (created != pdPASS) {
        vSemaphoreDelete(state->ready);
        delete state;
        return false;
    }
    return true;
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_script_task_value_frame_v2_acceptance_task() { return false; }
} // namespace jellyframe_esp32s3

#endif
