#include "jellyframe_esp32s3_hal.h"

#include "esp_timer.h"

namespace jellyframe_esp32s3 {
namespace {

std::uint64_t now_ms(void*) {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

bool flush_rgb565_rect(jellyframe::Rect dirty_rect, void* context) {
    if (context == nullptr) {
        return false;
    }
    auto* panel = static_cast<Rgb565Panel*>(context);
    ++panel->flush_count;
    panel->flushed_pixels += static_cast<std::uint32_t>(dirty_rect.width * dirty_rect.height);
    if (panel->flush == nullptr) {
        return true;
    }
    const int stride = panel->stride_pixels > 0 ? panel->stride_pixels : panel->width;
    return panel->flush(panel->pixels,
                        panel->width,
                        panel->height,
                        stride,
                        dirty_rect,
                        panel->flush_context);
}

} // namespace

jellyframe::HostClock make_clock() {
    return jellyframe::HostClock{now_ms, nullptr};
}

std::size_t rgb565_buffer_pixels(int width, int height, int stride_pixels) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const int stride = stride_pixels > 0 ? stride_pixels : width;
    if (stride < width) {
        return 0;
    }
    return static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
}

jellyframe::EmbeddedFrameBufferSink make_rgb565_sink(Rgb565Panel& panel) {
    const int stride = panel.stride_pixels > 0 ? panel.stride_pixels : panel.width;
    const std::size_t pixels = rgb565_buffer_pixels(panel.width, panel.height, stride);
    return jellyframe::EmbeddedFrameBufferSink{
        jellyframe::EmbeddedFrameBufferTarget{
            panel.width,
            panel.height,
            jellyframe::EmbeddedPixelFormat::Rgb565,
            reinterpret_cast<std::uint8_t*>(panel.pixels),
            pixels * sizeof(std::uint16_t),
            static_cast<std::size_t>(stride) * sizeof(std::uint16_t),
        },
        flush_rgb565_rect,
        &panel,
    };
}

} // namespace jellyframe_esp32s3
