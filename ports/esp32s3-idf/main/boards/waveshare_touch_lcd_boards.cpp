#include "boards/waveshare_touch_lcd_boards.h"

#include "jellyframe_esp32s3_input.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3::boards {
namespace {

constexpr const char* kTag = "JellyFrameBoard";

constexpr BoardProfile kGenericProfile{
    BoardId::GenericQemu,
    "generic-qemu",
    DisplayProfile{CONFIG_JELLYFRAME_BENCH_VIEWPORT_WIDTH,
                   CONFIG_JELLYFRAME_BENCH_VIEWPORT_HEIGHT,
                   "qemu/no-panel",
                   "memory",
                   false,
                   ""},
    "No hardware board selected; RGB565 flushes remain probe/no-op paths.",
};

constexpr BoardProfile kWaveshare147Profile{
    BoardId::WaveshareEsp32s3TouchLcd147,
    "waveshare-esp32-s3-touch-lcd-1.47",
    DisplayProfile{172, 320, "JD9853", "SPI", true, "AXS5106L"},
    "Board-local adapter for the Waveshare 1.47-inch 172x320 JD9853 LCD and AXS5106L touch controller.",
};

#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
constexpr i2c_port_t kWs147I2cPort = I2C_NUM_0;
constexpr int kWs147I2cClockHz = 400000;
constexpr int kWs147I2cTimeoutMs = 100;
constexpr int kWs147LcdXGap = 34;
constexpr std::uint8_t kWs147AxsTouchPointsReg = 0x01;
constexpr std::uint8_t kWs147AxsMaxPoints = 2;

struct Ws147DisplayContext {
    spi_host_device_t spi_host = SPI2_HOST;
    esp_lcd_panel_io_handle_t lcd_io = nullptr;
    i2c_master_bus_handle_t i2c_bus = nullptr;
    i2c_master_dev_handle_t touch_dev = nullptr;
    SemaphoreHandle_t lcd_lock = nullptr;
    SemaphoreHandle_t lcd_color_done = nullptr;
    TaskHandle_t touch_task = nullptr;
    std::uint16_t* dma_pixels = nullptr;
    std::size_t dma_pixel_capacity = 0;
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

struct Ws147TouchPoint {
    std::uint16_t raw_x = 0;
    std::uint16_t raw_y = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct Ws147InitCommand {
    std::uint8_t command;
    std::uint8_t data[32];
    std::uint8_t data_bytes;
    std::uint16_t delay_ms;
};

constexpr std::uint16_t ws147_panel_rgb565(std::uint16_t color) {
#if CONFIG_JELLYFRAME_WS147_LCD_SWAP_RGB565_BYTES
    return static_cast<std::uint16_t>((color << 8) | (color >> 8));
#else
    return color;
#endif
}

constexpr std::size_t ws147_dma_pixel_capacity() {
    return static_cast<std::size_t>(CONFIG_JELLYFRAME_WS147_LCD_WIDTH) *
        static_cast<std::size_t>(CONFIG_JELLYFRAME_WS147_LCD_DMA_ROWS);
}

bool ws147_ensure_dma_buffer(Ws147DisplayContext& display) {
    const std::size_t required_pixels = ws147_dma_pixel_capacity();
    if (display.dma_pixels != nullptr && display.dma_pixel_capacity >= required_pixels) {
        return true;
    }
    if (display.dma_pixels != nullptr) {
        heap_caps_free(display.dma_pixels);
        display.dma_pixels = nullptr;
        display.dma_pixel_capacity = 0;
    }
    display.dma_pixels = static_cast<std::uint16_t*>(
        heap_caps_malloc(required_pixels * sizeof(std::uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (display.dma_pixels == nullptr) {
        ESP_LOGE(kTag,
                 "waveshare 1.47 DMA line buffer allocation failed: pixels=%u bytes=%u",
                 static_cast<unsigned>(required_pixels),
                 static_cast<unsigned>(required_pixels * sizeof(std::uint16_t)));
        return false;
    }
    display.dma_pixel_capacity = required_pixels;
    return true;
}

esp_err_t ws147_set_backlight(Ws147DisplayContext& display, bool on) {
    if (CONFIG_JELLYFRAME_WS147_LCD_BL_GPIO < 0) {
        display.backlight_on = on;
        return ESP_OK;
    }
    const gpio_num_t backlight_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_LCD_BL_GPIO);
    const int active_level = CONFIG_JELLYFRAME_WS147_LCD_BL_ON_LEVEL;
    const int inactive_level = active_level == 0 ? 1 : 0;
    ESP_RETURN_ON_ERROR(gpio_set_level(backlight_gpio, on ? active_level : inactive_level),
                        kTag,
                        "waveshare 1.47 backlight level failed");
    display.backlight_on = on;
    return ESP_OK;
}

constexpr Ws147InitCommand kWs147InitCommands[] = {
    {0x11, {}, 0, 120},
    {0xdf, {0x98, 0x53}, 2, 0},
    {0xdf, {0x98, 0x53}, 2, 0},
    {0xb2, {0x23}, 1, 0},
    {0xb7, {0x00, 0x47, 0x00, 0x6f}, 4, 0},
    {0xbb, {0x1c, 0x1a, 0x55, 0x73, 0x63, 0xf0}, 6, 0},
    {0xc0, {0x44, 0xa4}, 2, 0},
    {0xc1, {0x16}, 1, 0},
    {0xc3, {0x7d, 0x07, 0x14, 0x06, 0xcf, 0x71, 0x72, 0x77}, 8, 0},
    {0xc4, {0x00, 0x00, 0xa0, 0x79, 0x0b, 0x0a, 0x16, 0x79, 0x0b, 0x0a, 0x16, 0x82}, 12, 0},
    {0xc8,
     {0x3f, 0x32, 0x29, 0x29, 0x27, 0x2b, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0d, 0x04, 0x00,
      0x3f, 0x32, 0x29, 0x29, 0x27, 0x2b, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0d, 0x04, 0x00},
     32,
     0},
    {0xd0, {0x04, 0x06, 0x6b, 0x0f, 0x00}, 5, 0},
    {0xd7, {0x00, 0x30}, 2, 0},
    {0xe6, {0x14}, 1, 0},
    {0xde, {0x01}, 1, 0},
    {0xb7, {0x03, 0x13, 0xef, 0x35, 0x35}, 5, 0},
    {0xc1, {0x14, 0x15, 0xc0}, 3, 0},
    {0xc2, {0x06, 0x3a}, 2, 0},
    {0xc4, {0x72, 0x12}, 2, 0},
    {0xbe, {0x00}, 1, 0},
    {0xde, {0x02}, 1, 0},
    {0xe5, {0x00, 0x02, 0x00}, 3, 0},
    {0xe5, {0x01, 0x02, 0x00}, 3, 0},
    {0xde, {0x00}, 1, 0},
    {LCD_CMD_TEOFF, {0x00}, 1, 0},
    {LCD_CMD_COLMOD, {0x55}, 1, 0},
    {0xb0, {0x00, 0xf0}, 2, 0},
    {LCD_CMD_CASET, {0x00, 0x22, 0x00, 0xcd}, 4, 0},
    {LCD_CMD_RASET, {0x00, 0x00, 0x01, 0x3f}, 4, 0},
    {0xde, {0x02}, 1, 0},
    {0xe5, {0x00, 0x02, 0x00}, 3, 0},
    {0xde, {0x00}, 1, 0},
    {LCD_CMD_MADCTL, {0x00}, 1, 0},
    {LCD_CMD_INVON, {}, 0, 0},
    {LCD_CMD_DISPON, {}, 0, 0},
};

void ws147_lock_lcd(Ws147DisplayContext& display) {
    if (display.lcd_lock != nullptr) {
        xSemaphoreTake(display.lcd_lock, portMAX_DELAY);
    }
}

void ws147_unlock_lcd(Ws147DisplayContext& display) {
    if (display.lcd_lock != nullptr) {
        xSemaphoreGive(display.lcd_lock);
    }
}

esp_err_t ws147_lcd_tx_param(esp_lcd_panel_io_handle_t io,
                             std::uint8_t command,
                             const std::uint8_t* data,
                             std::uint8_t data_bytes) {
    return esp_lcd_panel_io_tx_param(io, command, data, data_bytes);
}

bool IRAM_ATTR ws147_lcd_color_done_callback(esp_lcd_panel_io_handle_t,
                                             esp_lcd_panel_io_event_data_t*,
                                             void* user_ctx) {
    auto* display = static_cast<Ws147DisplayContext*>(user_ctx);
    if (display == nullptr || display->lcd_color_done == nullptr) {
        return false;
    }
    BaseType_t high_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(display->lcd_color_done, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

bool ws147_wait_lcd_color_done(Ws147DisplayContext& display) {
    if (display.lcd_color_done == nullptr) {
        return true;
    }
    return xSemaphoreTake(display.lcd_color_done, pdMS_TO_TICKS(1000)) == pdTRUE;
}

esp_err_t ws147_reset_lcd() {
    const gpio_num_t reset_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_LCD_RST_GPIO);
    ESP_RETURN_ON_ERROR(gpio_reset_pin(reset_gpio), kTag, "waveshare 1.47 lcd reset pin reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(reset_gpio, GPIO_MODE_OUTPUT), kTag, "waveshare 1.47 lcd reset pin direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), kTag, "waveshare 1.47 lcd reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), kTag, "waveshare 1.47 lcd reset high failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t ws147_init_lcd(Ws147DisplayContext& display) {
    if (display.lcd_lock == nullptr) {
        display.lcd_lock = xSemaphoreCreateMutex();
        if (display.lcd_lock == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (display.lcd_color_done == nullptr) {
        display.lcd_color_done = xSemaphoreCreateBinary();
        if (display.lcd_color_done == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!ws147_ensure_dma_buffer(display)) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num = CONFIG_JELLYFRAME_WS147_LCD_SPI_SCLK_GPIO;
    bus_config.mosi_io_num = CONFIG_JELLYFRAME_WS147_LCD_SPI_MOSI_GPIO;
    bus_config.miso_io_num = -1;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = CONFIG_JELLYFRAME_WS147_LCD_WIDTH *
        CONFIG_JELLYFRAME_WS147_LCD_DMA_ROWS * static_cast<int>(sizeof(std::uint16_t));

    esp_err_t err = spi_bus_initialize(display.spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "waveshare 1.47 SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = CONFIG_JELLYFRAME_WS147_LCD_CS_GPIO;
    io_config.dc_gpio_num = CONFIG_JELLYFRAME_WS147_LCD_DC_GPIO;
    io_config.spi_mode = 3;
    io_config.pclk_hz = CONFIG_JELLYFRAME_WS147_LCD_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.on_color_trans_done = ws147_lcd_color_done_callback;
    io_config.user_ctx = &display;

    err = esp_lcd_new_panel_io_spi(display.spi_host, &io_config, &display.lcd_io);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "waveshare 1.47 panel IO init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(ws147_reset_lcd(), kTag, "waveshare 1.47 lcd reset failed");

    ws147_lock_lcd(display);
    for (const Ws147InitCommand& command : kWs147InitCommands) {
        err = ws147_lcd_tx_param(display.lcd_io,
                                 command.command,
                                 command.data_bytes > 0 ? command.data : nullptr,
                                 command.data_bytes);
        if (err != ESP_OK) {
            ws147_unlock_lcd(display);
            ESP_LOGE(kTag, "waveshare 1.47 lcd init command failed: %s", esp_err_to_name(err));
            return err;
        }
        if (command.delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
        }
    }
    ws147_unlock_lcd(display);

    if (CONFIG_JELLYFRAME_WS147_LCD_BL_GPIO >= 0) {
        const gpio_num_t backlight_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_LCD_BL_GPIO);
        ESP_RETURN_ON_ERROR(gpio_reset_pin(backlight_gpio), kTag, "waveshare 1.47 backlight reset failed");
        ESP_RETURN_ON_ERROR(gpio_set_direction(backlight_gpio, GPIO_MODE_OUTPUT), kTag, "waveshare 1.47 backlight direction failed");
        ESP_RETURN_ON_ERROR(ws147_set_backlight(display, false), kTag, "waveshare 1.47 backlight off failed");
    }

    return ESP_OK;
}

esp_err_t ws147_set_window(esp_lcd_panel_io_handle_t io, int x_start, int y_start, int x_end, int y_end) {
    x_start += kWs147LcdXGap;
    x_end += kWs147LcdXGap;
    const std::uint8_t column_data[4] = {
        static_cast<std::uint8_t>((x_start >> 8) & 0xff),
        static_cast<std::uint8_t>(x_start & 0xff),
        static_cast<std::uint8_t>(((x_end - 1) >> 8) & 0xff),
        static_cast<std::uint8_t>((x_end - 1) & 0xff),
    };
    const std::uint8_t row_data[4] = {
        static_cast<std::uint8_t>((y_start >> 8) & 0xff),
        static_cast<std::uint8_t>(y_start & 0xff),
        static_cast<std::uint8_t>(((y_end - 1) >> 8) & 0xff),
        static_cast<std::uint8_t>((y_end - 1) & 0xff),
    };
    ESP_RETURN_ON_ERROR(ws147_lcd_tx_param(io, LCD_CMD_CASET, column_data, sizeof(column_data)),
                        kTag,
                        "waveshare 1.47 CASET failed");
    return ws147_lcd_tx_param(io, LCD_CMD_RASET, row_data, sizeof(row_data));
}

bool ws147_packed_flush(const std::uint16_t* pixels, jellyframe::Rect dirty_rect, void* context) {
    auto* display = static_cast<Ws147DisplayContext*>(context);
    if (display == nullptr || display->lcd_io == nullptr || pixels == nullptr ||
        dirty_rect.width <= 0 || dirty_rect.height <= 0) {
        return false;
    }

    constexpr int rows_per_chunk = CONFIG_JELLYFRAME_WS147_LCD_DMA_ROWS;
    const std::size_t max_chunk_pixels = display->dma_pixel_capacity;
    std::uint16_t* color_buffer = display->dma_pixels;
    if (color_buffer == nullptr || max_chunk_pixels == 0) {
        return false;
    }

    ws147_lock_lcd(*display);
    for (int y = 0; y < dirty_rect.height; y += rows_per_chunk) {
        const int rows = std::min(rows_per_chunk, dirty_rect.height - y);
        const std::size_t pixels_in_chunk =
            static_cast<std::size_t>(dirty_rect.width) * static_cast<std::size_t>(rows);
        if (pixels_in_chunk > max_chunk_pixels) {
            ws147_unlock_lcd(*display);
            return false;
        }
        const std::uint16_t* source =
            pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(dirty_rect.width);
        for (std::size_t i = 0; i < pixels_in_chunk; ++i) {
            color_buffer[i] = ws147_panel_rgb565(source[i]);
        }
        if (ws147_set_window(display->lcd_io,
                             dirty_rect.x,
                             dirty_rect.y + y,
                             dirty_rect.x + dirty_rect.width,
                             dirty_rect.y + y + rows) != ESP_OK ||
            esp_lcd_panel_io_tx_color(display->lcd_io,
                                      LCD_CMD_RAMWR,
                                      color_buffer,
                                      pixels_in_chunk * sizeof(std::uint16_t)) != ESP_OK ||
            !ws147_wait_lcd_color_done(*display)) {
            ws147_unlock_lcd(*display);
            return false;
        }
    }
    ws147_unlock_lcd(*display);
    if (!display->backlight_on) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ws147_set_backlight(*display, true));
    }
    return true;
}

Ws147TouchPoint ws147_map_touch_point(std::uint16_t raw_x, std::uint16_t raw_y) {
    Ws147TouchPoint point{};
    point.raw_x = raw_x;
    point.raw_y = raw_y;

    constexpr int width = CONFIG_JELLYFRAME_WS147_LCD_WIDTH;
    constexpr int height = CONFIG_JELLYFRAME_WS147_LCD_HEIGHT;
    const int clamped_x = std::min(width - 1, std::max(0, static_cast<int>(raw_x)));
    const int clamped_y = std::min(height - 1, std::max(0, static_cast<int>(raw_y)));

    point.x = static_cast<std::uint16_t>(width - 1 - clamped_x);
    point.y = static_cast<std::uint16_t>(clamped_y);
    return point;
}

esp_err_t ws147_read_touch_points(Ws147DisplayContext& display,
                                  Ws147TouchPoint* points,
                                  std::uint8_t max_points,
                                  std::uint8_t& point_count) {
    point_count = 0;
    if (display.touch_dev == nullptr || points == nullptr || max_points == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t reg = kWs147AxsTouchPointsReg;
    std::uint8_t data[14]{};
    esp_err_t err = i2c_master_transmit(display.touch_dev, &reg, sizeof(reg), kWs147I2cTimeoutMs);
    if (err == ESP_OK) {
        err = i2c_master_receive(display.touch_dev, data, sizeof(data), kWs147I2cTimeoutMs);
    }
    if (err != ESP_OK) {
        return err;
    }

    const std::uint8_t reported_points = static_cast<std::uint8_t>(data[1] & 0x0f);
    point_count = std::min<std::uint8_t>(reported_points, std::min<std::uint8_t>(max_points, kWs147AxsMaxPoints));
    for (std::uint8_t i = 0; i < point_count; ++i) {
        const std::uint8_t base = static_cast<std::uint8_t>(2 + i * 6);
        const std::uint16_t raw_x =
            static_cast<std::uint16_t>(((data[base] & 0x0f) << 8) | data[base + 1]);
        const std::uint16_t raw_y =
            static_cast<std::uint16_t>(((data[base + 2] & 0x0f) << 8) | data[base + 3]);
        points[i] = ws147_map_touch_point(raw_x, raw_y);
    }
    return ESP_OK;
}

void ws147_enqueue_touch(Ws147DisplayContext& display,
                         BoardInputKind kind,
                         const Ws147TouchPoint& point) {
    if (display.input_queue == nullptr) {
        return;
    }
    BoardInputEvent event;
    event.kind = kind;
    event.x = point.x;
    event.y = point.y;
    if (!display.input_queue->enqueue(event)) {
        ESP_LOGW(kTag, "waveshare 1.47 touch input queue full; dropped=%u",
                 static_cast<unsigned>(display.input_queue->dropped_count()));
    }
}

void ws147_enqueue_touch_release(Ws147DisplayContext& display) {
    if (display.input_queue == nullptr) {
        return;
    }
    BoardInputEvent event;
    event.kind = BoardInputKind::PointerUp;
    event.x = display.touch_start_x;
    event.y = display.touch_start_y;
    if (!display.input_queue->enqueue(event)) {
        ESP_LOGW(kTag, "waveshare 1.47 touch input queue full; dropped=%u",
                 static_cast<unsigned>(display.input_queue->dropped_count()));
    }
}

void ws147_touch_poll_task(void* arg) {
    auto* display = static_cast<Ws147DisplayContext*>(arg);
    Ws147TouchPoint points[kWs147AxsMaxPoints]{};
    while (display != nullptr && !display->touch_task_stop) {
        const bool was_pending = display->touch_pending;
        display->touch_pending = false;
        ++display->touch_poll_count;
        std::uint8_t point_count = 0;
        const esp_err_t err = ws147_read_touch_points(*display, points, kWs147AxsMaxPoints, point_count);
        if (err == ESP_OK && point_count > 0) {
            const bool was_down = display->touch_down;
            display->touch_down = true;
            display->touch_miss_count = 0;
            ++display->touch_point_count;
            display->last_touch_x = points[0].x;
            display->last_touch_y = points[0].y;
            if (!was_down) {
                display->touch_start_x = points[0].x;
                display->touch_start_y = points[0].y;
                ESP_LOGI(kTag,
                         "waveshare 1.47 touch down points=%u raw=%u,%u mapped=%u,%u",
                         static_cast<unsigned>(point_count),
                         static_cast<unsigned>(points[0].raw_x),
                         static_cast<unsigned>(points[0].raw_y),
                         static_cast<unsigned>(points[0].x),
                         static_cast<unsigned>(points[0].y));
            }
            ws147_enqueue_touch(*display,
                                was_down ? BoardInputKind::PointerMove : BoardInputKind::PointerDown,
                                points[0]);
        } else if (err == ESP_OK && display->touch_down) {
            ++display->touch_miss_count;
            if (display->touch_miss_count < 3) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            display->touch_down = false;
            display->touch_miss_count = 0;
            ESP_LOGI(kTag,
                     "waveshare 1.47 touch released start=%u,%u last=%u,%u",
                     static_cast<unsigned>(display->touch_start_x),
                     static_cast<unsigned>(display->touch_start_y),
                     static_cast<unsigned>(display->last_touch_x),
                     static_cast<unsigned>(display->last_touch_y));
            ws147_enqueue_touch_release(*display);
        } else if (err != ESP_OK) {
            ESP_LOGW(kTag, "waveshare 1.47 touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        const std::uint64_t now_us = esp_timer_get_time();
        if (display->last_touch_summary_us == 0 || now_us - display->last_touch_summary_us >= 5000000ULL) {
            display->last_touch_summary_us = now_us;
            ESP_LOGI(kTag,
                     "waveshare 1.47 touch summary polls=%u irq=%u hits=%u int_level=%d pending=%d down=%d",
                     static_cast<unsigned>(display->touch_poll_count),
                     static_cast<unsigned>(display->touch_irq_count),
                     static_cast<unsigned>(display->touch_point_count),
                     gpio_get_level(static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO)),
                     was_pending ? 1 : 0,
                     display->touch_down ? 1 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (display != nullptr) {
        display->touch_task = nullptr;
    }
    vTaskDelete(nullptr);
}

void IRAM_ATTR ws147_touch_isr_handler(void* arg) {
    auto* display = static_cast<Ws147DisplayContext*>(arg);
    if (display != nullptr) {
        display->touch_pending = true;
        display->touch_irq_count = display->touch_irq_count + 1;
    }
}

esp_err_t ws147_init_i2c_and_probe_touch(Ws147DisplayContext& display) {
    const gpio_num_t touch_reset_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_RST_GPIO);
    const gpio_num_t touch_int_gpio = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO);
    ESP_RETURN_ON_ERROR(gpio_reset_pin(touch_int_gpio), kTag, "waveshare 1.47 touch int pin reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(touch_int_gpio, GPIO_MODE_INPUT), kTag, "waveshare 1.47 touch int direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(touch_int_gpio, GPIO_PULLUP_ONLY), kTag, "waveshare 1.47 touch int pullup failed");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(touch_int_gpio, GPIO_INTR_NEGEDGE), kTag, "waveshare 1.47 touch int edge failed");

    ESP_RETURN_ON_ERROR(gpio_reset_pin(touch_reset_gpio), kTag, "waveshare 1.47 touch reset pin reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(touch_reset_gpio, GPIO_MODE_OUTPUT), kTag, "waveshare 1.47 touch reset pin direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(touch_reset_gpio, 0), kTag, "waveshare 1.47 touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(gpio_set_level(touch_reset_gpio, 1), kTag, "waveshare 1.47 touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(300));

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = kWs147I2cPort;
    bus_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_SDA_GPIO);
    bus_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_SCL_GPIO);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &display.i2c_bus), kTag, "waveshare 1.47 I2C init failed");

    const esp_err_t probe_err =
        i2c_master_probe(display.i2c_bus, CONFIG_JELLYFRAME_WS147_TOUCH_ADDR, kWs147I2cTimeoutMs);
    ESP_LOGI(kTag,
             "waveshare 1.47 touch probe addr=0x%02x result=%s int_gpio=%d int_level=%d",
             CONFIG_JELLYFRAME_WS147_TOUCH_ADDR,
             esp_err_to_name(probe_err),
             CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO,
             gpio_get_level(touch_int_gpio));
    if (probe_err != ESP_OK) {
        return probe_err;
    }

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "waveshare 1.47 touch ISR service install failed: %s", esp_err_to_name(isr_err));
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(touch_int_gpio, ws147_touch_isr_handler, &display));
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_intr_enable(touch_int_gpio));
    }

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = CONFIG_JELLYFRAME_WS147_TOUCH_ADDR;
    device_config.scl_speed_hz = kWs147I2cClockHz;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(display.i2c_bus, &device_config, &display.touch_dev),
                        kTag,
                        "waveshare 1.47 touch device add failed");

    display.touch_task_stop = false;
    BaseType_t task_ok = xTaskCreate(ws147_touch_poll_task,
                                     "ws147_touch",
                                     4096,
                                     &display,
                                     4,
                                     &display.touch_task);
    if (task_ok != pdPASS) {
        ESP_LOGW(kTag, "waveshare 1.47 touch task creation failed");
        display.touch_task = nullptr;
    }
    return ESP_OK;
}

BoardRuntime initialize_waveshare_147() {
    static Ws147DisplayContext display;

    esp_err_t err = ws147_init_lcd(display);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "waveshare 1.47 LCD init failed: %s", esp_err_to_name(err));
        return BoardRuntime{kWaveshare147Profile, false, "JD9853 init failed", nullptr, &display};
    }

    err = ws147_init_i2c_and_probe_touch(display);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "waveshare 1.47 display initialized; touch probe failed: %s", esp_err_to_name(err));
        return BoardRuntime{kWaveshare147Profile, true, "hardware display initialized; touch probe failed", ws147_packed_flush, &display};
    }

    ESP_LOGI(kTag, "waveshare 1.47 hardware display initialized and AXS5106L detected");
    return BoardRuntime{kWaveshare147Profile, true, "hardware display initialized; touch detected", ws147_packed_flush, &display};
}

void release_waveshare_147(Ws147DisplayContext& display) {
    display.touch_task_stop = true;
    for (int wait_ms = 0; display.touch_task != nullptr && wait_ms < 100; wait_ms += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (display.touch_dev != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(display.touch_dev));
        display.touch_dev = nullptr;
    }
    if (display.i2c_bus != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(display.i2c_bus));
        display.i2c_bus = nullptr;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(static_cast<gpio_num_t>(CONFIG_JELLYFRAME_WS147_TOUCH_INT_GPIO)));
    if (display.lcd_io != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_io_del(display.lcd_io));
        display.lcd_io = nullptr;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(display.spi_host));
    if (display.lcd_color_done != nullptr) {
        vSemaphoreDelete(display.lcd_color_done);
        display.lcd_color_done = nullptr;
    }
    if (display.lcd_lock != nullptr) {
        vSemaphoreDelete(display.lcd_lock);
        display.lcd_lock = nullptr;
    }
    if (display.dma_pixels != nullptr) {
        heap_caps_free(display.dma_pixels);
        display.dma_pixels = nullptr;
        display.dma_pixel_capacity = 0;
    }
    display.input_queue = nullptr;
    display.touch_down = false;
}
#endif

} // namespace

const BoardProfile& selected_board_profile() {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
    return kWaveshare147Profile;
#else
    return kGenericProfile;
#endif
}

bool selected_board_probe_only_enabled() {
    return false;
}

void run_selected_board_probe_only() {
    ESP_LOGW(kTag, "selected board has no probe-only implementation enabled");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

BoardRuntime initialize_selected_board() {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
    return initialize_waveshare_147();
#else
    const BoardProfile& profile = selected_board_profile();
    return BoardRuntime{profile, false, "hardware board support disabled", nullptr, nullptr};
#endif
}

void release_board_runtime(BoardRuntime& runtime) {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
    if (runtime.profile.id == BoardId::WaveshareEsp32s3TouchLcd147 && runtime.flush_context != nullptr) {
        release_waveshare_147(*static_cast<Ws147DisplayContext*>(runtime.flush_context));
    }
#endif
    runtime.hardware_display_ready = false;
    runtime.hardware_status = "released";
    runtime.packed_flush = nullptr;
    runtime.flush_context = nullptr;
}

void attach_input_queue(BoardRuntime& runtime, BoardInputQueue* queue) {
#if CONFIG_JELLYFRAME_ESP32S3_BOARD_ENABLE_HARDWARE && \
    CONFIG_JELLYFRAME_ESP32S3_BOARD_WAVESHARE_TOUCH_LCD_147
    if (runtime.profile.id == BoardId::WaveshareEsp32s3TouchLcd147 && runtime.flush_context != nullptr) {
        auto* display = static_cast<Ws147DisplayContext*>(runtime.flush_context);
        display->input_queue = queue;
        if (queue != nullptr) {
            ESP_LOGI(kTag, "waveshare 1.47 touch events attached to JellyFrame input queue");
        }
    }
#else
    (void)runtime;
    (void)queue;
#endif
}

} // namespace jellyframe_esp32s3::boards
