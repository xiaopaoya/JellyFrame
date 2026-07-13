#include "boards/waveshare_touch_lcd_boards.h"

#include "jellyframe_esp32s3_input.h"

#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION
#define CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION 0
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_169
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3::boards {
namespace {

constexpr const char* kTag = "JellyFrameBoard";

constexpr BoardProfile kWaveshare169Profile{
    BoardId::WaveshareEsp32s3TouchLcd169,
    "waveshare-esp32-s3-touch-lcd-1.69",
    DisplayProfile{240, 280, "ST7789V2", "SPI", true, "CST816T"},
    "Board-local adapter for the Waveshare 1.69-inch 240x280 ST7789V2 LCD and CST816T touch controller.",
};

#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_169
constexpr i2c_port_t kWs169I2cPort = I2C_NUM_0;
constexpr int kWs169I2cClockHz = 400000;
constexpr int kWs169I2cTimeoutMs = 100;
constexpr int kWs169LcdYGap = 20;
constexpr int kWs169GramHeight = 320;
constexpr std::uint8_t kWs169CmdVerticalScrollDefinition = 0x33;
constexpr std::uint8_t kWs169CmdVerticalScrollStart = 0x37;
constexpr std::uint8_t kCst816TouchCountOffset = 2;
constexpr std::uint8_t kCst816MaxTouches = 1;

struct Ws169DisplayContext {
    spi_host_device_t spi_host = SPI2_HOST;
    esp_lcd_panel_io_handle_t lcd_io = nullptr;
    esp_lcd_panel_handle_t lcd_panel = nullptr;
    i2c_master_bus_handle_t i2c_bus = nullptr;
    i2c_master_dev_handle_t touch_dev = nullptr;
    SemaphoreHandle_t lcd_lock = nullptr;
    SemaphoreHandle_t lcd_color_done = nullptr;
    TaskHandle_t touch_task = nullptr;
    std::uint16_t* dma_pixels = nullptr;
    std::size_t dma_pixel_capacity = 0;
    bool panel_scroll_configured = false;
    std::uint16_t panel_scroll_start = 0;
    BoardInputQueue* input_queue = nullptr;
    bool touch_task_stop = false;
    bool touch_down = false;
    std::uint8_t touch_miss_count = 0;
    volatile bool touch_pending = false;
    volatile std::uint32_t touch_irq_count = 0;
    std::uint32_t touch_poll_count = 0;
    std::uint32_t touch_point_count = 0;
    std::uint64_t last_touch_summary_us = 0;
    std::uint16_t touch_start_x = 0;
    std::uint16_t touch_start_y = 0;
    std::uint16_t last_touch_x = 0;
    std::uint16_t last_touch_y = 0;
    bool backlight_on = false;
};

struct Ws169TouchPoint {
    std::uint16_t raw_x = 0;
    std::uint16_t raw_y = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

constexpr std::size_t ws169_dma_pixel_capacity() {
    return static_cast<std::size_t>(CONFIG_JELLYFRAME_WS169_LCD_WIDTH) *
        static_cast<std::size_t>(CONFIG_JELLYFRAME_WS169_LCD_DMA_ROWS);
}

bool ws169_ensure_dma_buffer(Ws169DisplayContext& display) {
    const std::size_t required_pixels = ws169_dma_pixel_capacity();
    if (display.dma_pixels != nullptr && display.dma_pixel_capacity >= required_pixels) {
        return true;
    }
    if (display.dma_pixels != nullptr) {
        heap_caps_free(display.dma_pixels);
    }
    display.dma_pixels = static_cast<std::uint16_t*>(
        heap_caps_malloc(required_pixels * sizeof(std::uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (display.dma_pixels == nullptr) {
        display.dma_pixel_capacity = 0;
        ESP_LOGE(kTag, "waveshare 1.69 DMA strip allocation failed: pixels=%u", static_cast<unsigned>(required_pixels));
        return false;
    }
    display.dma_pixel_capacity = required_pixels;
    return true;
}

void ws169_lock_lcd(Ws169DisplayContext& display) {
    if (display.lcd_lock != nullptr) {
        xSemaphoreTake(display.lcd_lock, portMAX_DELAY);
    }
}

void ws169_unlock_lcd(Ws169DisplayContext& display) {
    if (display.lcd_lock != nullptr) {
        xSemaphoreGive(display.lcd_lock);
    }
}

bool IRAM_ATTR ws169_lcd_color_done_callback(esp_lcd_panel_io_handle_t,
                                             esp_lcd_panel_io_event_data_t*,
                                             void* user_context) {
    auto* display = static_cast<Ws169DisplayContext*>(user_context);
    if (display == nullptr || display->lcd_color_done == nullptr) {
        return false;
    }
    BaseType_t high_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(display->lcd_color_done, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

bool ws169_wait_lcd_color_done(Ws169DisplayContext& display) {
    return display.lcd_color_done == nullptr ||
        xSemaphoreTake(display.lcd_color_done, pdMS_TO_TICKS(1000)) == pdTRUE;
}

esp_err_t ws169_set_backlight(Ws169DisplayContext& display, bool on) {
    const gpio_num_t backlight_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_LCD_BL_GPIO);
    ESP_RETURN_ON_ERROR(gpio_set_level(backlight_gpio, on ? 1 : 0), kTag, "waveshare 1.69 backlight level failed");
    display.backlight_on = on;
    return ESP_OK;
}

esp_err_t ws169_init_lcd(Ws169DisplayContext& display) {
    if (display.lcd_lock == nullptr) {
        display.lcd_lock = xSemaphoreCreateMutex();
    }
    if (display.lcd_color_done == nullptr) {
        display.lcd_color_done = xSemaphoreCreateBinary();
    }
    if (display.lcd_lock == nullptr || display.lcd_color_done == nullptr || !ws169_ensure_dma_buffer(display)) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_num_t backlight_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_LCD_BL_GPIO);
    ESP_RETURN_ON_ERROR(gpio_reset_pin(backlight_gpio), kTag, "waveshare 1.69 backlight reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(backlight_gpio, GPIO_MODE_OUTPUT), kTag, "waveshare 1.69 backlight direction failed");
    ESP_RETURN_ON_ERROR(ws169_set_backlight(display, false), kTag, "waveshare 1.69 backlight off failed");

    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num = CONFIG_JELLYFRAME_WS169_LCD_SPI_SCLK_GPIO;
    bus_config.mosi_io_num = CONFIG_JELLYFRAME_WS169_LCD_SPI_MOSI_GPIO;
    bus_config.miso_io_num = -1;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = static_cast<int>(ws169_dma_pixel_capacity() * sizeof(std::uint16_t));
    esp_err_t result = spi_bus_initialize(display.spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = CONFIG_JELLYFRAME_WS169_LCD_CS_GPIO;
    io_config.dc_gpio_num = CONFIG_JELLYFRAME_WS169_LCD_DC_GPIO;
    io_config.spi_mode = 0;
    io_config.pclk_hz = CONFIG_JELLYFRAME_WS169_LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.on_color_trans_done = ws169_lcd_color_done_callback;
    io_config.user_ctx = &display;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(display.spi_host, &io_config, &display.lcd_io),
                        kTag,
                        "waveshare 1.69 panel IO init failed");

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = CONFIG_JELLYFRAME_WS169_LCD_RST_GPIO;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(display.lcd_io, &panel_config, &display.lcd_panel),
                        kTag,
                        "waveshare 1.69 ST7789 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display.lcd_panel), kTag, "waveshare 1.69 panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display.lcd_panel), kTag, "waveshare 1.69 panel initialize failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(display.lcd_panel, 0, kWs169LcdYGap),
                        kTag,
                        "waveshare 1.69 panel gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display.lcd_panel, true),
                        kTag,
                        "waveshare 1.69 panel invert failed");
    return esp_lcd_panel_disp_on_off(display.lcd_panel, true);
}

bool ws169_packed_flush(const std::uint16_t* pixels,
                        jellyframe::Rect dirty_rect,
                        Rgb565PackedFlushMetrics* metrics,
                        void* context) {
    auto* display = static_cast<Ws169DisplayContext*>(context);
    if (display == nullptr || display->lcd_panel == nullptr || pixels == nullptr ||
        dirty_rect.width <= 0 || dirty_rect.height <= 0) {
        return false;
    }
    if (metrics != nullptr) {
        *metrics = {};
    }
    ws169_lock_lcd(*display);
    for (int y = 0; y < dirty_rect.height; y += CONFIG_JELLYFRAME_WS169_LCD_DMA_ROWS) {
        const int rows = std::min(CONFIG_JELLYFRAME_WS169_LCD_DMA_ROWS, dirty_rect.height - y);
        const std::size_t chunk_pixels = static_cast<std::size_t>(dirty_rect.width) * static_cast<std::size_t>(rows);
        if (chunk_pixels > display->dma_pixel_capacity) {
            ws169_unlock_lcd(*display);
            return false;
        }
        const std::uint16_t* source = pixels + static_cast<std::size_t>(y) * dirty_rect.width;
        const std::uint64_t convert_start = esp_timer_get_time();
        for (std::size_t index = 0; index < chunk_pixels; ++index) {
            const std::uint16_t color = source[index];
            display->dma_pixels[index] = static_cast<std::uint16_t>((color << 8) | (color >> 8));
        }
        const std::uint64_t window_start = esp_timer_get_time();
        const esp_err_t submit_result = esp_lcd_panel_draw_bitmap(display->lcd_panel,
                                                                    dirty_rect.x,
                                                                    dirty_rect.y + y,
                                                                    dirty_rect.x + dirty_rect.width,
                                                                    dirty_rect.y + y + rows,
                                                                    display->dma_pixels);
        const std::uint64_t after_submit = esp_timer_get_time();
        const bool complete = submit_result == ESP_OK && ws169_wait_lcd_color_done(*display);
        const std::uint64_t after_wait = esp_timer_get_time();
        if (metrics != nullptr) {
            metrics->convert_us += static_cast<std::uint32_t>(window_start - convert_start);
            metrics->window_setup_us += static_cast<std::uint32_t>(after_submit - window_start);
            metrics->dma_submit_us += static_cast<std::uint32_t>(after_submit - window_start);
            metrics->dma_wait_us += static_cast<std::uint32_t>(after_wait - after_submit);
            ++metrics->chunks;
        }
        if (!complete) {
            ws169_unlock_lcd(*display);
            return false;
        }
    }
    ws169_unlock_lcd(*display);
    if (!display->backlight_on) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ws169_set_backlight(*display, true));
    }
    return true;
}

void ws169_accumulate_metrics(Rgb565PackedFlushMetrics& target,
                              const Rgb565PackedFlushMetrics& source) {
    target.convert_us += source.convert_us;
    target.window_setup_us += source.window_setup_us;
    target.scroll_setup_us += source.scroll_setup_us;
    target.dma_submit_us += source.dma_submit_us;
    target.dma_wait_us += source.dma_wait_us;
    target.chunks += source.chunks;
    target.scroll_wraps += source.scroll_wraps;
}

bool ws169_configure_panel_scroll(Ws169DisplayContext& display,
                                  Rgb565PackedFlushMetrics* metrics) {
    if (display.panel_scroll_configured) {
        return true;
    }
    constexpr int height = CONFIG_JELLYFRAME_WS169_LCD_HEIGHT;
    constexpr int bottom_fixed_rows = kWs169GramHeight - kWs169LcdYGap - height;
    static_assert(bottom_fixed_rows >= 0, "ST7789 GRAM must contain the visible viewport");
    const std::uint8_t definition[6] = {
        0, static_cast<std::uint8_t>(kWs169LcdYGap),
        static_cast<std::uint8_t>((height >> 8) & 0xff),
        static_cast<std::uint8_t>(height & 0xff),
        static_cast<std::uint8_t>((bottom_fixed_rows >> 8) & 0xff),
        static_cast<std::uint8_t>(bottom_fixed_rows & 0xff),
    };
    const std::uint64_t start = esp_timer_get_time();
    ws169_lock_lcd(display);
    const esp_err_t result = esp_lcd_panel_io_tx_param(display.lcd_io,
                                                        kWs169CmdVerticalScrollDefinition,
                                                        definition,
                                                        sizeof(definition));
    ws169_unlock_lcd(display);
    if (metrics != nullptr) {
        metrics->scroll_setup_us += static_cast<std::uint32_t>(esp_timer_get_time() - start);
    }
    if (result != ESP_OK) {
        return false;
    }
    display.panel_scroll_configured = true;
    display.panel_scroll_start = 0;
    return true;
}

bool ws169_set_panel_scroll_start(Ws169DisplayContext& display,
                                  std::uint16_t logical_start,
                                  Rgb565PackedFlushMetrics* metrics) {
    constexpr int height = CONFIG_JELLYFRAME_WS169_LCD_HEIGHT;
    if (logical_start >= height) {
        return false;
    }
    const std::uint16_t gram_start = static_cast<std::uint16_t>(kWs169LcdYGap + logical_start);
    const std::uint8_t data[2] = {
        static_cast<std::uint8_t>((gram_start >> 8) & 0xff),
        static_cast<std::uint8_t>(gram_start & 0xff),
    };
    const std::uint64_t start = esp_timer_get_time();
    ws169_lock_lcd(display);
    const esp_err_t result = esp_lcd_panel_io_tx_param(display.lcd_io,
                                                        kWs169CmdVerticalScrollStart,
                                                        data,
                                                        sizeof(data));
    ws169_unlock_lcd(display);
    if (metrics != nullptr) {
        metrics->scroll_setup_us += static_cast<std::uint32_t>(esp_timer_get_time() - start);
    }
    if (result != ESP_OK) {
        return false;
    }
    display.panel_scroll_start = logical_start;
    return true;
}

bool ws169_packed_scroll_flush(const std::uint16_t* pixels,
                               jellyframe::Rect exposed_strip,
                               int scroll_delta_y,
                               Rgb565PackedFlushMetrics* metrics,
                               void* context) {
    auto* display = static_cast<Ws169DisplayContext*>(context);
    constexpr int width = CONFIG_JELLYFRAME_WS169_LCD_WIDTH;
    constexpr int height = CONFIG_JELLYFRAME_WS169_LCD_HEIGHT;
    const int rows = scroll_delta_y < 0 ? -scroll_delta_y : scroll_delta_y;
    if (display == nullptr || pixels == nullptr || metrics == nullptr || display->lcd_io == nullptr ||
        rows <= 0 || rows >= height || exposed_strip.x != 0 || exposed_strip.width != width ||
        exposed_strip.height != rows || exposed_strip.y < 0 || exposed_strip.y > height - rows ||
        ((scroll_delta_y > 0) != (exposed_strip.y + rows == height)) ||
        ((scroll_delta_y < 0) != (exposed_strip.y == 0))) {
        return false;
    }

    *metrics = {};
    if (!ws169_configure_panel_scroll(*display, metrics)) {
        return false;
    }
    const int old_start = display->panel_scroll_start;
    const int next_start = (old_start + scroll_delta_y + height) % height;
    const int physical_start = scroll_delta_y > 0 ? old_start : next_start;
    if (!ws169_set_panel_scroll_start(*display, static_cast<std::uint16_t>(next_start), metrics)) {
        return false;
    }
    const int first_rows = std::min(rows, height - physical_start);
    Rgb565PackedFlushMetrics part_metrics;
    if (!ws169_packed_flush(pixels, jellyframe::Rect{0, physical_start, width, first_rows},
                            &part_metrics, display)) {
        return false;
    }
    ws169_accumulate_metrics(*metrics, part_metrics);
    if (first_rows != rows) {
        part_metrics = {};
        if (!ws169_packed_flush(pixels + static_cast<std::size_t>(first_rows) * width,
                                jellyframe::Rect{0, 0, width, rows - first_rows},
                                &part_metrics, display)) {
            return false;
        }
        ws169_accumulate_metrics(*metrics, part_metrics);
        ++metrics->scroll_wraps;
    }
    return true;
}

bool ws169_reset_panel_scroll(void* context) {
    auto* display = static_cast<Ws169DisplayContext*>(context);
    if (display == nullptr || display->lcd_io == nullptr) {
        return false;
    }
    if (!display->panel_scroll_configured && display->panel_scroll_start == 0) {
        return true;
    }
    if (!ws169_set_panel_scroll_start(*display, 0, nullptr)) {
        return false;
    }
    display->panel_scroll_configured = false;
    return true;
}

Ws169TouchPoint ws169_map_touch_point(std::uint16_t raw_x, std::uint16_t raw_y) {
    Ws169TouchPoint point{};
    point.raw_x = raw_x;
    point.raw_y = raw_y;
    point.x = static_cast<std::uint16_t>(std::clamp(static_cast<int>(raw_x), 0, CONFIG_JELLYFRAME_WS169_LCD_WIDTH - 1));
    point.y = static_cast<std::uint16_t>(std::clamp(static_cast<int>(raw_y), 0, CONFIG_JELLYFRAME_WS169_LCD_HEIGHT - 1));
    return point;
}

esp_err_t ws169_read_touch(Ws169DisplayContext& display, Ws169TouchPoint& point, bool& pressed) {
    pressed = false;
    std::uint8_t register_address = 0x00;
    std::uint8_t data[7]{};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(display.touch_dev, &register_address, sizeof(register_address), kWs169I2cTimeoutMs),
                        kTag,
                        "waveshare 1.69 touch register select failed");
    ESP_RETURN_ON_ERROR(i2c_master_receive(display.touch_dev, data, sizeof(data), kWs169I2cTimeoutMs),
                        kTag,
                        "waveshare 1.69 touch read failed");
    const std::uint8_t touches = static_cast<std::uint8_t>(data[kCst816TouchCountOffset] & 0x0f);
    if (touches == 0 || touches == 0x0f) {
        return ESP_OK;
    }
    const std::uint16_t raw_x = static_cast<std::uint16_t>(((data[3] & 0x0f) << 8) | data[4]);
    const std::uint16_t raw_y = static_cast<std::uint16_t>(((data[5] & 0x0f) << 8) | data[6]);
    point = ws169_map_touch_point(raw_x, raw_y);
    pressed = true;
    return ESP_OK;
}

void ws169_enqueue_touch(Ws169DisplayContext& display, BoardInputKind kind, const Ws169TouchPoint& point) {
    if (display.input_queue == nullptr) {
        return;
    }
    BoardInputEvent event;
    event.kind = kind;
    event.x = point.x;
    event.y = point.y;
    if (!display.input_queue->enqueue(event)) {
        ESP_LOGW(kTag, "waveshare 1.69 touch input queue full; dropped=%u",
                 static_cast<unsigned>(display.input_queue->dropped_count()));
    }
}

void ws169_touch_poll_task(void* argument) {
    auto* display = static_cast<Ws169DisplayContext*>(argument);
    while (display != nullptr && !display->touch_task_stop) {
        display->touch_pending = false;
        ++display->touch_poll_count;
        Ws169TouchPoint point{};
        bool pressed = false;
        const esp_err_t result = ws169_read_touch(*display, point, pressed);
        if (result == ESP_OK && pressed) {
            const bool was_down = display->touch_down;
            display->touch_down = true;
            display->touch_miss_count = 0;
            ++display->touch_point_count;
            display->last_touch_x = point.x;
            display->last_touch_y = point.y;
            if (!was_down) {
                display->touch_start_x = point.x;
                display->touch_start_y = point.y;
                ESP_LOGI(kTag, "waveshare 1.69 touch down raw=%u,%u mapped=%u,%u",
                         point.raw_x, point.raw_y, point.x, point.y);
            }
            ws169_enqueue_touch(*display, was_down ? BoardInputKind::PointerMove : BoardInputKind::PointerDown, point);
        } else if (result == ESP_OK && display->touch_down) {
            ++display->touch_miss_count;
            if (display->touch_miss_count >= 3) {
                display->touch_down = false;
                display->touch_miss_count = 0;
                ws169_enqueue_touch(*display, BoardInputKind::PointerUp,
                                    Ws169TouchPoint{0, 0, display->last_touch_x, display->last_touch_y});
            }
        } else if (result != ESP_OK) {
            ESP_LOGW(kTag, "waveshare 1.69 touch read failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        const std::uint64_t now_us = esp_timer_get_time();
        if (display->last_touch_summary_us == 0 || now_us - display->last_touch_summary_us >= 5000000ULL) {
            display->last_touch_summary_us = now_us;
            ESP_LOGI(kTag, "waveshare 1.69 touch summary polls=%u irq=%u hits=%u down=%d",
                     static_cast<unsigned>(display->touch_poll_count),
                     static_cast<unsigned>(display->touch_irq_count),
                     static_cast<unsigned>(display->touch_point_count),
                     display->touch_down ? 1 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (display != nullptr) {
        display->touch_task = nullptr;
    }
    vTaskDelete(nullptr);
}

void IRAM_ATTR ws169_touch_isr_handler(void* argument) {
    auto* display = static_cast<Ws169DisplayContext*>(argument);
    if (display != nullptr) {
        display->touch_pending = true;
        ++display->touch_irq_count;
    }
}

esp_err_t ws169_init_touch(Ws169DisplayContext& display) {
    const gpio_num_t reset_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_TOUCH_RST_GPIO);
    const gpio_num_t interrupt_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_TOUCH_INT_GPIO);
    ESP_RETURN_ON_ERROR(gpio_reset_pin(interrupt_gpio), kTag, "waveshare 1.69 touch interrupt reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(interrupt_gpio, GPIO_MODE_INPUT), kTag, "waveshare 1.69 touch interrupt direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(interrupt_gpio, GPIO_PULLUP_ONLY), kTag, "waveshare 1.69 touch interrupt pullup failed");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(interrupt_gpio, GPIO_INTR_NEGEDGE), kTag, "waveshare 1.69 touch interrupt type failed");
    ESP_RETURN_ON_ERROR(gpio_reset_pin(reset_gpio), kTag, "waveshare 1.69 touch reset pin failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(reset_gpio, GPIO_MODE_OUTPUT), kTag, "waveshare 1.69 touch reset direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), kTag, "waveshare 1.69 touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), kTag, "waveshare 1.69 touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = kWs169I2cPort;
    bus_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_TOUCH_SDA_GPIO);
    bus_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_TOUCH_SCL_GPIO);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &display.i2c_bus), kTag, "waveshare 1.69 I2C init failed");
    const esp_err_t probe_result = i2c_master_probe(display.i2c_bus, CONFIG_JELLYFRAME_WS169_TOUCH_ADDR, kWs169I2cTimeoutMs);
    ESP_LOGI(kTag, "waveshare 1.69 CST816T probe addr=0x%02x result=%s",
             CONFIG_JELLYFRAME_WS169_TOUCH_ADDR, esp_err_to_name(probe_result));
    ESP_RETURN_ON_ERROR(probe_result, kTag, "waveshare 1.69 CST816T probe failed");

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = CONFIG_JELLYFRAME_WS169_TOUCH_ADDR;
    device_config.scl_speed_hz = kWs169I2cClockHz;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(display.i2c_bus, &device_config, &display.touch_dev),
                        kTag,
                        "waveshare 1.69 CST816T device add failed");
    const esp_err_t isr_result = gpio_install_isr_service(0);
    if (isr_result == ESP_OK || isr_result == ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(interrupt_gpio, ws169_touch_isr_handler, &display));
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_intr_enable(interrupt_gpio));
    }
    display.touch_task_stop = false;
    if (xTaskCreate(ws169_touch_poll_task, "ws169_touch", 4096, &display, 4, &display.touch_task) != pdPASS) {
        display.touch_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ws169_release(Ws169DisplayContext& display) {
    display.touch_task_stop = true;
    for (int wait_ms = 0; display.touch_task != nullptr && wait_ms < 100; wait_ms += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS169_TOUCH_INT_GPIO)));
    if (display.touch_dev != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(display.touch_dev));
    }
    if (display.i2c_bus != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(display.i2c_bus));
    }
    if (display.lcd_panel != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_del(display.lcd_panel));
    }
    if (display.lcd_io != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_io_del(display.lcd_io));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(display.spi_host));
    if (display.lcd_color_done != nullptr) {
        vSemaphoreDelete(display.lcd_color_done);
    }
    if (display.lcd_lock != nullptr) {
        vSemaphoreDelete(display.lcd_lock);
    }
    if (display.dma_pixels != nullptr) {
        heap_caps_free(display.dma_pixels);
    }
    display = {};
}
#endif

} // namespace

const BoardProfile& waveshare_169_profile() {
    return kWaveshare169Profile;
}

BoardRuntime initialize_waveshare_169() {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_169
    static Ws169DisplayContext display;
    const esp_err_t lcd_result = ws169_init_lcd(display);
    if (lcd_result != ESP_OK) {
        ESP_LOGE(kTag, "waveshare 1.69 ST7789V2 init failed: %s", esp_err_to_name(lcd_result));
        return BoardRuntime{kWaveshare169Profile, false, "ST7789V2 init failed", nullptr, nullptr, nullptr, &display};
    }
    const esp_err_t touch_result = ws169_init_touch(display);
    if (touch_result != ESP_OK) {
        ESP_LOGW(kTag, "waveshare 1.69 display initialized; CST816T unavailable: %s", esp_err_to_name(touch_result));
        return BoardRuntime{kWaveshare169Profile, true, "hardware display initialized; touch probe failed", ws169_packed_flush, nullptr, nullptr, &display};
    }
    ESP_LOGI(kTag, "waveshare 1.69 hardware display initialized and CST816T detected");
    return BoardRuntime{kWaveshare169Profile, true, "hardware display initialized; touch detected", ws169_packed_flush,
#if CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION
                        ws169_packed_scroll_flush, ws169_reset_panel_scroll,
#else
                        nullptr, nullptr,
#endif
                        &display};
#else
    return BoardRuntime{kWaveshare169Profile, false, "hardware board support disabled", nullptr, nullptr};
#endif
}

void release_waveshare_169(BoardRuntime& runtime) {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_169
    if (runtime.flush_context != nullptr) {
        ws169_release(*static_cast<Ws169DisplayContext*>(runtime.flush_context));
    }
#else
    (void)runtime;
#endif
}

void attach_waveshare_169_input_queue(BoardRuntime& runtime, BoardInputQueue* queue) {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_169
    if (runtime.profile.id == BoardId::WaveshareEsp32s3TouchLcd169 && runtime.flush_context != nullptr) {
        auto* display = static_cast<Ws169DisplayContext*>(runtime.flush_context);
        display->input_queue = queue;
        ESP_LOGI(kTag, "waveshare 1.69 touch events attached to JellyFrame input queue");
    }
#else
    (void)runtime;
    (void)queue;
#endif
}

} // namespace jellyframe_esp32s3::boards
