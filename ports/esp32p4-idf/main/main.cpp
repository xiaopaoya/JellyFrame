#include "jellyframe_esp32p4_accel.h"

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "render_core/html_parser.h"
#include "render_core/host.h"

namespace {
constexpr char kTag[] = "JellyFrameP4";
}

extern "C" void app_main(void) {
    // Link and exercise the unchanged Render Core before board-specific work.
    jellyframe::HostDeviceCapabilities capabilities{};
    capabilities.media.supports_image_decode = true;
    capabilities.media.preferred_decoded_image_format = jellyframe::HostPixelFormat::Rgb565;
    jellyframe::HtmlParser parser;
    const auto document = parser.parse("<!doctype html><main><h1>JellyFrame P4</h1></main>");
    ESP_LOGI(kTag, "render_core=%s image_decode=%d free_dma=%u",
             document ? "ok" : "failed", capabilities.media.supports_image_decode,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)));

#if CONFIG_JELLYFRAME_P4_ACCEL_SMOKE
    constexpr int width = 64;
    constexpr int height = 64;
    auto* surface = static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
        64, width * height * sizeof(std::uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    jellyframe_esp32p4::AccelerationStats stats{};
    const bool ppa_ok = surface != nullptr && jellyframe_esp32p4::ppa_fill_rgb565(
        surface, width, height, 0x20, 0x90, 0xf0, &stats);
    ESP_LOGI(kTag, "PPA RGB565 fill=%s calls=%u elapsed_us=%llu",
             ppa_ok ? "ok" : "failed", static_cast<unsigned>(stats.ppa_fill_calls),
             static_cast<unsigned long long>(stats.ppa_fill_us));
    heap_caps_free(surface);

    // Acquiring the codec confirms that the JPEG hardware clock/driver is
    // available. A real JPEG is decoded only when the port image adapter has
    // supplied a bounded resource buffer to decode_jpeg_rgb565().
    jpeg_decoder_handle_t jpeg = nullptr;
    jpeg_decode_engine_cfg_t config{};
    config.timeout_ms = 1000;
    const bool jpeg_ok = jpeg_new_decoder_engine(&config, &jpeg) == ESP_OK;
    ESP_LOGI(kTag, "JPEG decoder=%s", jpeg_ok ? "ready" : "unavailable");
    if (jpeg != nullptr) jpeg_del_decoder_engine(jpeg);
#endif
}
