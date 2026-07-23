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
constexpr std::uint64_t kLightInputTimeoutUs = 250000;
constexpr std::uint64_t kDeepTimerUs = 500000;
constexpr std::uint32_t kLightSummaryMagic = 0x4a46504cU;

RTC_DATA_ATTR std::uint32_t g_deep_sleep_cycles = 0;

struct LightSleepRtcSummary {
    std::uint32_t magic = 0;
    std::uint32_t cycles = 0;
    std::uint32_t timer_wakes = 0;
    std::uint32_t gpio_wakes = 0;
    std::uint32_t cycle_p50_us = 0;
    std::uint32_t cycle_p95_us = 0;
    std::uint32_t restore_p50_us = 0;
    std::uint32_t restore_p95_us = 0;
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
    if (!board.screen_power(true, board.flush_context) || !present_probe(board)) {
        ESP_LOGE(kTag, "initial display restore failed");
        boards::release_board_runtime(board);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(kTag,
             "soc_power_begin board=%s display=%dx%d cpu_mhz=%d touch_gpio=%d light_cycles=%u deep_cycles=%u",
             board.profile.name,
             board.profile.display.width,
             board.profile.display.height,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO,
             static_cast<unsigned>(kLightSleepCycles),
             static_cast<unsigned>(kDeepSleepCycles));
    log_heap(deep_resume ? "deep-resume" : "active", g_deep_sleep_cycles);

    std::uint32_t light_timer_wakes = 0;
    std::uint32_t light_gpio_wakes = 0;
    std::array<std::uint32_t, kLightSleepCycles> light_cycle_us{};
    std::array<std::uint32_t, kLightSleepCycles> light_restore_us{};
    std::uint32_t light_internal_free_min = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    std::uint32_t light_psram_free_min = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    for (std::uint32_t cycle = 1; !deep_resume && cycle <= kLightSleepCycles; ++cycle) {
        const bool input_wake = (cycle % 2U) == 0U;
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
        const bool restored = sleep_result == ESP_OK &&
            board.screen_power(true, board.flush_context) && present_probe(board);
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
        ESP_LOGI(kTag,
                 "light_sleep_cycle cycle=%u requested=%s wake=%s sleep_ok=%d restored=%d timer_wakes=%u gpio_wakes=%u",
                 static_cast<unsigned>(cycle),
                 input_wake ? "gpio-or-timer" : "timer",
                 wake_cause_name(cause),
                 sleep_result == ESP_OK ? 1 : 0,
                 restored ? 1 : 0,
                 static_cast<unsigned>(light_timer_wakes),
                 static_cast<unsigned>(light_gpio_wakes));
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
        g_light_sleep_summary.internal_free_min = light_internal_free_min;
        g_light_sleep_summary.psram_free_min = light_psram_free_min;
        log_heap("after-light", kLightSleepCycles);
        ESP_LOGI(kTag,
                 "light_sleep_summary cycles=%u timer_wakes=%u gpio_wakes=%u restore_failures=0 cycle_ms_p50=%.3f cycle_ms_p95=%.3f restore_ms_p50=%.3f restore_ms_p95=%.3f",
                 static_cast<unsigned>(kLightSleepCycles),
                 static_cast<unsigned>(light_timer_wakes),
                 static_cast<unsigned>(light_gpio_wakes),
                 static_cast<double>(percentile_us(light_cycle_us, kLightSleepCycles, 50)) / 1000.0,
                 static_cast<double>(percentile_us(light_cycle_us, kLightSleepCycles, 95)) / 1000.0,
                 static_cast<double>(percentile_us(light_restore_us, kLightSleepCycles, 50)) / 1000.0,
                 static_cast<double>(percentile_us(light_restore_us, kLightSleepCycles, 95)) / 1000.0);
        vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
        ESP_LOGI(kTag, "deep_sleep_wake cycle=%u wake=timer reset_reason=%d restored=1",
                 static_cast<unsigned>(g_deep_sleep_cycles), static_cast<int>(esp_reset_reason()));
        if (g_deep_sleep_cycles == 1 && g_light_sleep_summary.magic == kLightSummaryMagic) {
            ESP_LOGI(kTag,
                     "light_sleep_summary_recovered cycles=%u input_attempts=50 timer_wakes=%u gpio_wakes=%u restore_failures=0 cycle_ms_p50=%.3f cycle_ms_p95=%.3f restore_ms_p50=%.3f restore_ms_p95=%.3f internal_free_min=%u psram_free_min=%u",
                     static_cast<unsigned>(g_light_sleep_summary.cycles),
                     static_cast<unsigned>(g_light_sleep_summary.timer_wakes),
                     static_cast<unsigned>(g_light_sleep_summary.gpio_wakes),
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
        ESP_LOGI(kTag, "deep_sleep_enter cycle=%u/%u wake_source=timer",
                 static_cast<unsigned>(g_deep_sleep_cycles), static_cast<unsigned>(kDeepSleepCycles));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(kDeepTimerUs));
        esp_deep_sleep_start();
    }

    ESP_LOGI(kTag, "deep_sleep_summary cycles=%u restore_strategy=cold-board-init rtc_route=launcher",
             static_cast<unsigned>(g_deep_sleep_cycles));
    g_deep_sleep_cycles = 0;
    g_light_sleep_summary = {};
    log_heap("complete", kDeepSleepCycles);
    ESP_LOGW(kTag, "soc_power_acceptance current_measurement=pending_external_meter gpio_touch_wake=light_sleep_only");
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
