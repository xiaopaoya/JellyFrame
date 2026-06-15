#include "jellyframe_esp32s3_hal.h"

#include "esp_timer.h"

#include <cstddef>
#include <cstring>

namespace jellyframe_esp32s3 {
namespace {

std::uint64_t now_ms(void*) {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

bool valid_dirty_rect(const Rgb565Panel& panel, jellyframe::Rect dirty_rect) {
    const int stride = panel.stride_pixels > 0 ? panel.stride_pixels : panel.width;
    return panel.pixels != nullptr &&
        panel.width > 0 &&
        panel.height > 0 &&
        stride >= panel.width &&
        dirty_rect.x >= 0 &&
        dirty_rect.y >= 0 &&
        dirty_rect.width > 0 &&
        dirty_rect.height > 0 &&
        dirty_rect.x <= panel.width - dirty_rect.width &&
        dirty_rect.y <= panel.height - dirty_rect.height;
}

std::uint32_t dirty_pixel_count(jellyframe::Rect dirty_rect) {
    if (dirty_rect.width <= 0 || dirty_rect.height <= 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(dirty_rect.width) *
        static_cast<std::uint32_t>(dirty_rect.height);
}

bool flush_rgb565_rect(jellyframe::Rect dirty_rect, void* context) {
    if (context == nullptr) {
        return false;
    }
    auto* panel = static_cast<Rgb565Panel*>(context);
    const std::uint32_t dirty_pixels = dirty_pixel_count(dirty_rect);
    ++panel->flush_count;
    panel->flushed_pixels += dirty_pixels;
    panel->flushed_bytes += dirty_pixels * sizeof(std::uint16_t);
    panel->last_dirty_rect = dirty_rect;
    if (panel->flush == nullptr) {
        const bool ok = flush_rgb565_packed_rect(*panel, dirty_rect);
        if (!ok) {
            ++panel->failed_flush_count;
        }
        return ok;
    }
    const int stride = panel->stride_pixels > 0 ? panel->stride_pixels : panel->width;
    const bool ok = panel->flush(panel->pixels,
                                 panel->width,
                                 panel->height,
                                 stride,
                                 dirty_rect,
                                 panel->flush_context);
    if (!ok) {
        ++panel->failed_flush_count;
    }
    return ok;
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

bool flush_rgb565_packed_rect(Rgb565Panel& panel, jellyframe::Rect dirty_rect) {
    if (panel.packed_flush == nullptr) {
        return true;
    }
    if (!valid_dirty_rect(panel, dirty_rect)) {
        return false;
    }

    const int stride = panel.stride_pixels > 0 ? panel.stride_pixels : panel.width;
    const std::size_t dirty_pixels = static_cast<std::size_t>(dirty_rect.width) *
        static_cast<std::size_t>(dirty_rect.height);
    const std::uint16_t* source = panel.pixels +
        static_cast<std::size_t>(dirty_rect.y) * static_cast<std::size_t>(stride) +
        static_cast<std::size_t>(dirty_rect.x);

    ++panel.packed_flush_count;
    if (dirty_rect.x == 0 && dirty_rect.width == stride) {
        return panel.packed_flush(source, dirty_rect, panel.flush_context);
    }

    if (panel.scratch_pixels == nullptr || panel.scratch_pixel_capacity < dirty_pixels) {
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(dirty_rect.width) * sizeof(std::uint16_t);
    for (int row = 0; row < dirty_rect.height; ++row) {
        const std::uint16_t* source_row = source + static_cast<std::size_t>(row) *
            static_cast<std::size_t>(stride);
        std::uint16_t* target_row = panel.scratch_pixels + static_cast<std::size_t>(row) *
            static_cast<std::size_t>(dirty_rect.width);
        std::memcpy(target_row, source_row, row_bytes);
    }
    ++panel.scratch_flush_count;
    return panel.packed_flush(panel.scratch_pixels, dirty_rect, panel.flush_context);
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
