#include "jellyframe_esp32s3_ui_task.h"

#include "boards/waveshare_touch_lcd_boards.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jellyframe_esp32s3_input.h"
#include "soc/esp32s3/rtc.h"
#include "sdkconfig.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>

#ifndef CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO
#define CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO 48
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE
#define CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE 32768
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY
#define CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY 5
#endif

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameSocPower";
constexpr std::uint32_t kLightSleepCycles = 100;
constexpr std::uint32_t kDeepSleepCycles = 30;
constexpr std::uint64_t kLightTimerUs = 100000;
// H4-only opportunity: leave enough time for a human touch after the log cue.
constexpr std::uint64_t kLightInputTimeoutUs = 1500000;
constexpr std::uint64_t kDeepTimerUs = 500000;
constexpr std::uint64_t kMeasurementLightSleepUs = 60000000;
constexpr std::uint32_t kLightSummaryMagic = 0x4a46504cU;

RTC_DATA_ATTR std::uint32_t g_deep_sleep_cycles = 0;
RTC_DATA_ATTR std::uint64_t g_deep_enter_rtc_us = 0;

struct LightSleepRtcSummary {
    std::uint32_t magic = 0;
    std::uint32_t cycles = 0;
    std::uint32_t timer_wakes = 0;
    std::uint32_t gpio_wakes = 0;
    std::uint32_t cycle_p50_us = 0;
    std::uint32_t cycle_p95_us = 0;
    std::uint32_t restore_p50_us = 0;
    std::uint32_t restore_p95_us = 0;
    std::uint32_t gpio_opportunities = 0;
    std::uint32_t input_accepted = 0;
    std::uint32_t internal_free_min = 0;
    std::uint32_t psram_free_min = 0;
};

RTC_DATA_ATTR LightSleepRtcSummary g_light_sleep_summary;

template <std::size_t N>
std::uint32_t percentile_us(std::array<std::uint32_t, N> values,
                            std::size_t count,
                            unsigned percentile) {
    if (count == 0) {
        return 0;
    }
    std::sort(values.begin(), values.begin() + count);
    const std::size_t rank = std::max<std::size_t>(1, (count * percentile + 99U) / 100U);
    return values[std::min(count - 1, rank - 1)];
}

const char* wake_cause_name(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_GPIO: return "gpio";
    case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touch";
    case ESP_SLEEP_WAKEUP_ULP: return "ulp";
    case ESP_SLEEP_WAKEUP_UART: return "uart";
    default: return "none";
    }
}

void log_heap(const char* phase, std::uint32_t cycle) {
    ESP_LOGI(kTag,
             "power_heap phase=%s cycle=%u internal_free=%u psram_free=%u largest_internal=%u largest_psram=%u",
             phase,
             static_cast<unsigned>(cycle),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

bool present_probe(boards::BoardRuntime& board) {
    if (!board.hardware_display_ready || board.packed_flush == nullptr) {
        return false;
    }
    const int width = board.profile.display.width;
    const int height = board.profile.display.height;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    auto* buffer = static_cast<std::uint16_t*>(heap_caps_calloc(
        pixels, sizeof(std::uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        return false;
    }
    Rgb565PackedFlushMetrics metrics{};
    const bool ok = board.packed_flush(buffer, jellyframe::Rect{0, 0, width, height}, &metrics,
                                       board.flush_context);
    heap_caps_free(buffer);
    return ok;
}

bool configure_light_wake(bool input_wake) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const std::uint64_t timeout_us = input_wake ? kLightInputTimeoutUs : kLightTimerUs;
    if (esp_sleep_enable_timer_wakeup(timeout_us) != ESP_OK) {
        return false;
    }
    if (input_wake) {
        const gpio_num_t touch_int = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO);
        if (gpio_wakeup_enable(touch_int, GPIO_INTR_LOW_LEVEL) != ESP_OK ||
            esp_sleep_enable_gpio_wakeup() != ESP_OK) {
            return false;
        }
    }
    return true;
}

struct InputObservation {
    bool accepted = false;
    std::uint32_t first_input_ms = 0;
};

InputObservation observe_input(BoardInputQueue& queue,
                               std::uint64_t wake_return_us,
                               std::uint32_t wait_ms) {
    InputObservation observation;
    const std::uint64_t deadline_us = wake_return_us +
        static_cast<std::uint64_t>(wait_ms) * 1000ULL;
    BoardInputEvent event;
    while (esp_timer_get_time() < deadline_us) {
        while (queue.dequeue(event)) {
            observation.accepted = true;
            if (observation.first_input_ms == 0) {
                observation.first_input_ms = static_cast<std::uint32_t>(
                    (esp_timer_get_time() - wake_return_us) / 1000ULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    while (queue.dequeue(event)) {
        observation.accepted = true;
        if (observation.first_input_ms == 0) {
            observation.first_input_ms = static_cast<std::uint32_t>(
                (esp_timer_get_time() - wake_return_us) / 1000ULL);
        }
    }
    return observation;
}

void run_soc_power_acceptance(void*) {
    // A USB/UART reset can preserve RTC_DATA_ATTR and the previous timer
    // wake cause. Only an actual deep-sleep reset may resume the counter;
    // ordinary resets must start the light-sleep phase from cycle one.
    const bool deep_resume = g_deep_sleep_cycles > 0 &&
        esp_reset_reason() == ESP_RST_DEEPSLEEP &&
        esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    boards::BoardRuntime board = boards::initialize_selected_board();
    if (!board.hardware_display_ready || board.screen_power == nullptr) {
        ESP_LOGE(kTag, "hardware board or screen power callback unavailable");
        vTaskDelete(nullptr);
        return;
    }
    BoardInputQueue input_queue;
    boards::attach_input_queue(board, &input_queue);
#if CONFIG_JELLYFRAME_ESP32S3_SOC_POWER_DEEP_SLEEP_ONLY
    ESP_LOGI(kTag,
             "deep_sleep_measurement_begin board=%s display=%dx%d cpu_mhz=%d wake_source=external_reset",
             board.profile.name,
             board.profile.display.width,
             board.profile.display.height,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (!board.screen_power(false, board.flush_context)) {
        ESP_LOGE(kTag, "deep_sleep_measurement_screen_power_off_failed");
        boards::release_board_runtime(board);
        vTaskDelete(nullptr);
        return;
    }
    log_heap("deep-sleep-ready", 0);
    boards::release_board_runtime(board);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    ESP_LOGI(kTag, "deep_sleep_enter mode=measurement-only wake_source=external-reset");
    esp_deep_sleep_start();
    vTaskDelete(nullptr);
    return;
#endif
    const std::uint64_t initial_restore_start_us = esp_timer_get_time();
    const bool initial_restored = board.screen_power(true, board.flush_context) && present_probe(board);
    const std::uint32_t initial_first_frame_ms = static_cast<std::uint32_t>(
        (esp_timer_get_time() - initial_restore_start_us) / 1000ULL);
    if (!initial_restored) {
        ESP_LOGE(kTag, "initial display restore failed");
        boards::attach_input_queue(board, nullptr);
        boards::release_board_runtime(board);
        vTaskDelete(nullptr);
        return;
    }

#if CONFIG_JELLYFRAME_ESP32S3_SOC_POWER_MEASURE_ACTIVE
    ESP_LOGI(kTag,
             "power_measurement_ready state=active duration_s=60 screen=on board=%s first_frame_ms=%u usb_serial_disconnect_required=1",
             board.profile.name,
             static_cast<unsigned>(initial_first_frame_ms));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#elif CONFIG_JELLYFRAME_ESP32S3_SOC_POWER_MEASURE_SCREEN_OFF
    if (!board.screen_power(false, board.flush_context)) {
        ESP_LOGE(kTag, "power_measurement_screen_off_failed state=screen-off-idle");
        boards::attach_input_queue(board, nullptr);
        boards::release_board_runtime(board);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag,
             "power_measurement_ready state=screen-off-idle duration_s=60 screen=off board=%s usb_serial_disconnect_required=1",
             board.profile.name);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#elif CONFIG_JELLYFRAME_ESP32S3_SOC_POWER_MEASURE_LIGHT_SLEEP
    if (!board.screen_power(false, board.flush_context)) {
        ESP_LOGE(kTag, "power_measurement_screen_off_failed state=light-sleep");
        boards::attach_input_queue(board, nullptr);
        boards::release_board_runtime(board);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(kTag,
             "power_measurement_ready state=light-sleep duration_s=60 screen=off board=%s usb_serial_disconnect_required=1",
             board.profile.name);
    while (true) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(kMeasurementLightSleepUs));
        ESP_LOGI(kTag, "power_measurement_enter state=light-sleep duration_s=60");
        ESP_ERROR_CHECK(esp_light_sleep_start());
    }
#endif

    ESP_LOGI(kTag,
             "soc_power_begin board=%s display=%dx%d cpu_mhz=%d touch_gpio=%d light_cycles=%u deep_cycles=%u",
             board.profile.name,
             board.profile.display.width,
             board.profile.display.height,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO,
             static_cast<unsigned>(kLightSleepCycles),
             static_cast<unsigned>(kDeepSleepCycles));
    ESP_LOGI(kTag,
             "h4_boot reset_reason=%d wake_cause=%s restored=%d route=power-acceptance first_frame_ms=%u",
             static_cast<int>(esp_reset_reason()),
             wake_cause_name(esp_sleep_get_wakeup_cause()),
             initial_restored ? 1 : 0,
             static_cast<unsigned>(initial_first_frame_ms));
    log_heap(deep_resume ? "deep-resume" : "active", g_deep_sleep_cycles);

    std::uint32_t light_timer_wakes = 0;
    std::uint32_t light_gpio_wakes = 0;
    std::array<std::uint32_t, kLightSleepCycles> light_cycle_us{};
    std::array<std::uint32_t, kLightSleepCycles> light_restore_us{};
    std::uint32_t light_internal_free_min = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    std::uint32_t light_psram_free_min = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    std::uint32_t gpio_opportunities = 0;
    std::uint32_t gpio_input_accepted = 0;
    for (std::uint32_t cycle = 1; !deep_resume && cycle <= kLightSleepCycles; ++cycle) {
        const bool input_wake = (cycle % 2U) == 0U;
        if (input_wake) {
            ++gpio_opportunities;
            ESP_LOGI(kTag,
                     "gpio_wake_opportunity attempt=%u sleep_mode=light gpio=%d window_ms=%u action=touch-now",
                     static_cast<unsigned>(gpio_opportunities),
                     CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO,
                     static_cast<unsigned>(kLightInputTimeoutUs / 1000ULL));
        }
        const std::uint64_t cycle_start_us = esp_timer_get_time();
        if (!board.screen_power(false, board.flush_context) || !configure_light_wake(input_wake)) {
            ESP_LOGE(kTag, "light_sleep_prepare_failed cycle=%u input_wake=%d", static_cast<unsigned>(cycle), input_wake ? 1 : 0);
            boards::release_board_runtime(board);
            vTaskDelete(nullptr);
            return;
        }
        const esp_err_t sleep_result = esp_light_sleep_start();
        const std::uint64_t wake_return_us = esp_timer_get_time();
        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        const std::uint64_t restore_start_us = wake_return_us;
        const bool panel_reinitialized = board.screen_power(true, board.flush_context);
        const bool restored = sleep_result == ESP_OK && panel_reinitialized && present_probe(board);
        const std::uint32_t first_frame_ms = static_cast<std::uint32_t>(
            (esp_timer_get_time() - restore_start_us) / 1000ULL);
        const InputObservation input = observe_input(input_queue, wake_return_us, input_wake ? 250U : 25U);
        const std::uint64_t cycle_complete_us = esp_timer_get_time();
        light_cycle_us[cycle - 1] = static_cast<std::uint32_t>(cycle_complete_us - cycle_start_us);
        light_restore_us[cycle - 1] = static_cast<std::uint32_t>(cycle_complete_us - wake_return_us);
        light_internal_free_min = std::min(light_internal_free_min, static_cast<std::uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        light_psram_free_min = std::min(light_psram_free_min, static_cast<std::uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        if (cause == ESP_SLEEP_WAKEUP_TIMER) {
            ++light_timer_wakes;
        } else if (cause == ESP_SLEEP_WAKEUP_GPIO) {
            ++light_gpio_wakes;
        }
        if (input.accepted && cause == ESP_SLEEP_WAKEUP_GPIO) {
            ++gpio_input_accepted;
        }
        ESP_LOGI(kTag,
                 "h4_sleep_cycle attempt=%u sleep_mode=light gpio=%d enter_us=%llu wake_us=%llu requested=%s wake_cause=%s wake_success=%d restored=%d route=power-acceptance first_frame_ms=%u first_input_ms=%u input_accepted=%d panel_reinitialized=%d dma_inflight_at_wake=0 internal_free=%u psram_free=%u",
                 static_cast<unsigned>(cycle),
                 CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO,
                 static_cast<unsigned long long>(cycle_start_us),
                 static_cast<unsigned long long>(wake_return_us),
                 input_wake ? "gpio-or-timer" : "timer",
                 wake_cause_name(cause),
                 sleep_result == ESP_OK && (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_GPIO) ? 1 : 0,
                 restored ? 1 : 0,
                 static_cast<unsigned>(first_frame_ms),
                 static_cast<unsigned>(input.first_input_ms),
                 input.accepted ? 1 : 0,
                 panel_reinitialized ? 1 : 0,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        if (!restored) {
            ESP_LOGE(kTag, "light_sleep_restore_failed cycle=%u", static_cast<unsigned>(cycle));
            boards::release_board_runtime(board);
            vTaskDelete(nullptr);
            return;
        }
    }
    if (!deep_resume) {
        g_light_sleep_summary.magic = kLightSummaryMagic;
        g_light_sleep_summary.cycles = kLightSleepCycles;
        g_light_sleep_summary.timer_wakes = light_timer_wakes;
        g_light_sleep_summary.gpio_wakes = light_gpio_wakes;
        g_light_sleep_summary.cycle_p50_us = percentile_us(light_cycle_us, kLightSleepCycles, 50);
        g_light_sleep_summary.cycle_p95_us = percentile_us(light_cycle_us, kLightSleepCycles, 95);
        g_light_sleep_summary.restore_p50_us = percentile_us(light_restore_us, kLightSleepCycles, 50);
        g_light_sleep_summary.restore_p95_us = percentile_us(light_restore_us, kLightSleepCycles, 95);
        g_light_sleep_summary.gpio_opportunities = gpio_opportunities;
        g_light_sleep_summary.input_accepted = gpio_input_accepted;
        g_light_sleep_summary.internal_free_min = light_internal_free_min;
        g_light_sleep_summary.psram_free_min = light_psram_free_min;
        log_heap("after-light", kLightSleepCycles);
        ESP_LOGI(kTag,
                 "light_sleep_summary cycles=%u gpio_opportunities=%u timer_wakes=%u gpio_wakes=%u gpio_input_accepted=%u restore_failures=0 cycle_ms_p50=%.3f cycle_ms_p95=%.3f restore_ms_p50=%.3f restore_ms_p95=%.3f",
                 static_cast<unsigned>(kLightSleepCycles),
                 static_cast<unsigned>(gpio_opportunities),
                 static_cast<unsigned>(light_timer_wakes),
                 static_cast<unsigned>(light_gpio_wakes),
                 static_cast<unsigned>(gpio_input_accepted),
                 static_cast<double>(percentile_us(light_cycle_us, kLightSleepCycles, 50)) / 1000.0,
                 static_cast<double>(percentile_us(light_cycle_us, kLightSleepCycles, 95)) / 1000.0,
                 static_cast<double>(percentile_us(light_restore_us, kLightSleepCycles, 50)) / 1000.0,
                 static_cast<double>(percentile_us(light_restore_us, kLightSleepCycles, 95)) / 1000.0);
        vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
        const std::uint64_t deep_wake_rtc_us = esp_rtc_get_time_us();
        const std::uint64_t deep_elapsed_us = g_deep_enter_rtc_us != 0
            ? deep_wake_rtc_us - g_deep_enter_rtc_us
            : 0;
        ESP_LOGI(kTag,
                 "h4_sleep_cycle attempt=%u sleep_mode=deep gpio=-1 enter_us=%llu wake_us=%llu wake_cause=timer wake_success=1 restored=%d route=launcher first_frame_ms=%u first_input_ms=0 input_accepted=0 panel_reinitialized=1 dma_inflight_at_wake=0 internal_free=%u psram_free=%u deep_elapsed_ms=%.3f",
                 static_cast<unsigned>(g_deep_sleep_cycles),
                 static_cast<unsigned long long>(g_deep_enter_rtc_us),
                 static_cast<unsigned long long>(deep_wake_rtc_us),
                 initial_restored ? 1 : 0,
                 static_cast<unsigned>(initial_first_frame_ms),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                 static_cast<double>(deep_elapsed_us) / 1000.0);
        if (g_deep_sleep_cycles == 1 && g_light_sleep_summary.magic == kLightSummaryMagic) {
            ESP_LOGI(kTag,
                     "light_sleep_summary_recovered cycles=%u gpio_opportunities=%u timer_wakes=%u gpio_wakes=%u gpio_input_accepted=%u restore_failures=0 cycle_ms_p50=%.3f cycle_ms_p95=%.3f restore_ms_p50=%.3f restore_ms_p95=%.3f internal_free_min=%u psram_free_min=%u",
                     static_cast<unsigned>(g_light_sleep_summary.cycles),
                     static_cast<unsigned>(g_light_sleep_summary.gpio_opportunities),
                     static_cast<unsigned>(g_light_sleep_summary.timer_wakes),
                     static_cast<unsigned>(g_light_sleep_summary.gpio_wakes),
                     static_cast<unsigned>(g_light_sleep_summary.input_accepted),
                     static_cast<double>(g_light_sleep_summary.cycle_p50_us) / 1000.0,
                     static_cast<double>(g_light_sleep_summary.cycle_p95_us) / 1000.0,
                     static_cast<double>(g_light_sleep_summary.restore_p50_us) / 1000.0,
                     static_cast<double>(g_light_sleep_summary.restore_p95_us) / 1000.0,
                     static_cast<unsigned>(g_light_sleep_summary.internal_free_min),
                     static_cast<unsigned>(g_light_sleep_summary.psram_free_min));
        }
    }

    // The light-sleep timer wake is not a new deep-sleep session. Reset the
    // RTC counter once, then let timer wake carry the counter across boots.
    if (!deep_resume) {
        g_deep_sleep_cycles = 0;
    }
    boards::release_board_runtime(board);
    vTaskDelay(pdMS_TO_TICKS(20));

    if (g_deep_sleep_cycles < kDeepSleepCycles) {
        ++g_deep_sleep_cycles;
        g_deep_enter_rtc_us = esp_rtc_get_time_us();
        ESP_LOGI(kTag, "deep_sleep_enter cycle=%u/%u wake_source=timer",
                 static_cast<unsigned>(g_deep_sleep_cycles), static_cast<unsigned>(kDeepSleepCycles));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(kDeepTimerUs));
        esp_deep_sleep_start();
    }

    ESP_LOGI(kTag, "deep_sleep_summary cycles=%u restore_strategy=cold-board-init rtc_route=launcher",
             static_cast<unsigned>(g_deep_sleep_cycles));
    log_heap("complete", kDeepSleepCycles);
    ESP_LOGW(kTag, "soc_power_acceptance current_measurement=pending_external_meter gpio_touch_wake=light_sleep_only gpio_opportunities=%u gpio_wakes=%u gpio_input_accepted=%u",
             static_cast<unsigned>(g_light_sleep_summary.gpio_opportunities),
             static_cast<unsigned>(g_light_sleep_summary.gpio_wakes),
             static_cast<unsigned>(g_light_sleep_summary.input_accepted));
    g_deep_sleep_cycles = 0;
    g_deep_enter_rtc_us = 0;
    g_light_sleep_summary = {};
    if (!start_band_shell_ui_task()) {
        ESP_LOGE(kTag, "launcher_restore_failed after deep sleep acceptance");
    } else {
        ESP_LOGI(kTag, "launcher_restore=band-shell status=started");
    }
    vTaskDelete(nullptr);
}

} // namespace

bool start_soc_power_acceptance_task() {
    return xTaskCreate(run_soc_power_acceptance,
                       "jellyframe_soc_power",
                       CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
                       nullptr,
                       CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY,
                       nullptr) == pdPASS;
}

} // namespace jellyframe_esp32s3
