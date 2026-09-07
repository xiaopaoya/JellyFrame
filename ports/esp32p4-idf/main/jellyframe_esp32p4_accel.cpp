#include "jellyframe_esp32p4_accel.h"

#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <algorithm>
#include <limits>

namespace jellyframe_esp32p4 {
namespace {

ppa_client_handle_t fill_client = nullptr;

bool ensure_fill_client() {
    if (fill_client != nullptr) return true;
    ppa_client_config_t config{};
    config.oper_type = PPA_OPERATION_FILL;
    config.max_pending_trans_num = 1;
    config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
    return ppa_register_client(&config, &fill_client) == ESP_OK;
}

int align16(int value) {
    return (value + 15) & ~15;
}

} // namespace

bool ppa_fill_rgb565(std::uint16_t* pixels,
                     int width,
                     int height,
                     std::uint8_t red,
                     std::uint8_t green,
                     std::uint8_t blue,
                     AccelerationStats* stats) {
    if (pixels == nullptr || width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
            static_cast<std::size_t>(height) / sizeof(std::uint16_t) ||
        !ensure_fill_client()) {
        return false;
    }

    ppa_fill_oper_config_t operation{};
    operation.out.buffer = pixels;
    operation.out.buffer_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(std::uint16_t);
    operation.out.pic_w = static_cast<std::uint32_t>(width);
    operation.out.pic_h = static_cast<std::uint32_t>(height);
    operation.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
    operation.fill_block_w = static_cast<std::uint32_t>(width);
    operation.fill_block_h = static_cast<std::uint32_t>(height);
    operation.fill_argb_color.a = 255;
    operation.fill_argb_color.r = red;
    operation.fill_argb_color.g = green;
    operation.fill_argb_color.b = blue;
    operation.mode = PPA_TRANS_MODE_BLOCKING;

    const std::int64_t start = esp_timer_get_time();
    const bool ok = ppa_do_fill(fill_client, &operation) == ESP_OK;
    if (stats != nullptr) {
        stats->ppa_ready = ok;
        stats->ppa_fill_calls += ok ? 1 : 0;
        stats->ppa_fill_us += static_cast<std::uint64_t>(esp_timer_get_time() - start);
    }
    return ok;
}

bool decode_jpeg_rgb565(const std::uint8_t* jpeg,
                         std::size_t jpeg_size,
                         JpegRgb565Surface& output,
                         AccelerationStats* stats) {
    release_jpeg_rgb565_surface(output);
    if (jpeg == nullptr || jpeg_size == 0 || jpeg_size > std::numeric_limits<std::uint32_t>::max()) return false;

    jpeg_decode_picture_info_t info{};
    if (jpeg_decoder_get_info(jpeg, static_cast<std::uint32_t>(jpeg_size), &info) != ESP_OK ||
        info.width == 0 || info.height == 0) return false;
    const int stride = align16(static_cast<int>(info.width));
    const int padded_height = align16(static_cast<int>(info.height));
    const std::size_t requested = static_cast<std::size_t>(stride) * static_cast<std::size_t>(padded_height) * 2U;
    jpeg_decode_memory_alloc_cfg_t memory_config{};
    memory_config.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    std::size_t allocated = 0;
    auto* buffer = static_cast<std::uint8_t*>(jpeg_alloc_decoder_mem(requested, &memory_config, &allocated));
    if (buffer == nullptr) return false;

    jpeg_decoder_handle_t decoder = nullptr;
    jpeg_decode_engine_cfg_t engine_config{};
    engine_config.timeout_ms = 1000;
    const bool acquired = jpeg_new_decoder_engine(&engine_config, &decoder) == ESP_OK;
    std::uint32_t decoded = 0;
    jpeg_decode_cfg_t decode_config{};
    decode_config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    decode_config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
    decode_config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    const std::int64_t start = esp_timer_get_time();
    const bool ok = acquired && jpeg_decoder_process(decoder, &decode_config, jpeg,
        static_cast<std::uint32_t>(jpeg_size), buffer, static_cast<std::uint32_t>(allocated), &decoded) == ESP_OK;
    if (decoder != nullptr) jpeg_del_decoder_engine(decoder);
    if (!ok) {
        heap_caps_free(buffer);
        return false;
    }
    output = JpegRgb565Surface{buffer, decoded, static_cast<int>(info.width), static_cast<int>(info.height), stride};
    if (stats != nullptr) {
        stats->jpeg_ready = true;
        ++stats->jpeg_decode_calls;
        stats->jpeg_decode_us += static_cast<std::uint64_t>(esp_timer_get_time() - start);
    }
    return true;
}

void release_jpeg_rgb565_surface(JpegRgb565Surface& surface) {
    if (surface.bytes != nullptr) heap_caps_free(surface.bytes);
    surface = {};
}

} // namespace jellyframe_esp32p4
