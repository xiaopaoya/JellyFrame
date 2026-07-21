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

bool valid_packed_dirty_rect(const Rgb565Panel& panel, jellyframe::Rect dirty_rect) {
    return panel.width > 0 &&
        panel.height > 0 &&
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

void accumulate_packed_metrics(Rgb565Panel& panel, const Rgb565PackedFlushMetrics& metrics) {
    panel.panel_convert_us += metrics.convert_us;
    panel.panel_window_setup_us += metrics.window_setup_us;
    panel.panel_scroll_setup_us += metrics.scroll_setup_us;
    panel.panel_dma_submit_us += metrics.dma_submit_us;
    panel.panel_dma_wait_us += metrics.dma_wait_us;
    panel.panel_dma_chunks += metrics.chunks;
    panel.packed_scroll_wrap_count += metrics.scroll_wraps;
}

bool flush_rgb565_rect(jellyframe::Rect dirty_rect, void* context) {
    if (context == nullptr) {
        return false;
    }
    auto* panel = static_cast<Rgb565Panel*>(context);
    if (panel->framebuffer_convert_start_us != 0) {
        const std::uint64_t now_us = esp_timer_get_time();
        panel->framebuffer_convert_us += now_us - panel->framebuffer_convert_start_us;
        // A multi-rect present converts each following rectangle after this
        // callback returns, so this is its next conversion start point.
        panel->framebuffer_convert_start_us = now_us;
    }
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

bool flush_packed_rgb565_rect(const std::uint16_t* pixels, jellyframe::Rect dirty_rect, void* context) {
    if (context == nullptr || pixels == nullptr) {
        return false;
    }
    auto* panel = static_cast<Rgb565Panel*>(context);
    if (!valid_packed_dirty_rect(*panel, dirty_rect)) {
        return false;
    }
    if (panel->framebuffer_convert_start_us != 0) {
        const std::uint64_t now_us = esp_timer_get_time();
        panel->framebuffer_convert_us += now_us - panel->framebuffer_convert_start_us;
        panel->framebuffer_convert_start_us = now_us;
    }
    const std::uint32_t dirty_pixels = dirty_pixel_count(dirty_rect);
    ++panel->flush_count;
    ++panel->packed_flush_count;
    panel->flushed_pixels += dirty_pixels;
    panel->flushed_bytes += dirty_pixels * sizeof(std::uint16_t);
    panel->last_dirty_rect = dirty_rect;
    if (panel->packed_flush == nullptr) {
        return true;
    }

    Rgb565PackedFlushMetrics metrics;
    const bool use_scroll_callback = panel->packed_scroll_active && panel->packed_scroll_flush != nullptr;
    const bool ok = use_scroll_callback
        ? panel->packed_scroll_flush(pixels,
                                     dirty_rect,
                                     panel->packed_scroll_delta_y,
                                     &metrics,
                                     panel->flush_context)
        : panel->packed_flush(pixels, dirty_rect, &metrics, panel->flush_context);
    accumulate_packed_metrics(*panel, metrics);
    if (use_scroll_callback && ok) {
        ++panel->packed_scroll_flush_count;
    }
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
    Rgb565PackedFlushMetrics metrics;
    if (dirty_rect.x == 0 && dirty_rect.width == stride) {
        const bool ok = panel.packed_flush(source, dirty_rect, &metrics, panel.flush_context);
        accumulate_packed_metrics(panel, metrics);
        return ok;
    }

    if (panel.scratch_pixels == nullptr || panel.scratch_pixel_capacity < dirty_pixels) {
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(dirty_rect.width) * sizeof(std::uint16_t);
    const std::uint64_t copy_start = esp_timer_get_time();
    for (int row = 0; row < dirty_rect.height; ++row) {
        const std::uint16_t* source_row = source + static_cast<std::size_t>(row) *
            static_cast<std::size_t>(stride);
        std::uint16_t* target_row = panel.scratch_pixels + static_cast<std::size_t>(row) *
            static_cast<std::size_t>(dirty_rect.width);
        std::memcpy(target_row, source_row, row_bytes);
    }
    panel.scratch_copy_us += static_cast<std::uint64_t>(esp_timer_get_time() - copy_start);
    ++panel.scratch_flush_count;
    const bool ok = panel.packed_flush(panel.scratch_pixels, dirty_rect, &metrics, panel.flush_context);
    accumulate_packed_metrics(panel, metrics);
    return ok;
}

bool flush_rgb565_packed_scroll_strip(const jellyframe::HostFrameBufferView& frame,
                                      Rgb565Panel& panel,
                                      jellyframe::Rect exposed_strip,
                                      int scroll_delta_y) {
    if (panel.packed_scroll_flush == nullptr || scroll_delta_y == 0 ||
        !valid_packed_dirty_rect(panel, exposed_strip) || frame.pixels == nullptr ||
        frame.width != panel.width || frame.height != panel.height) {
        return false;
    }
    const int expected_height = scroll_delta_y < 0 ? -scroll_delta_y : scroll_delta_y;
    if (exposed_strip.width != panel.width || exposed_strip.x != 0 ||
        exposed_strip.height != expected_height) {
        return false;
    }

    panel.packed_scroll_active = true;
    panel.packed_scroll_delta_y = scroll_delta_y;
    jellyframe::EmbeddedPackedRgb565Sink sink = make_packed_rgb565_sink(panel);
    const jellyframe::HostFrameSink frame_sink = jellyframe::embedded_packed_rgb565_sink(sink);
    const bool ok = frame_sink.present != nullptr &&
        frame_sink.present(frame, &exposed_strip, 1, frame_sink.context);
    panel.packed_scroll_active = false;
    panel.packed_scroll_delta_y = 0;
    if (!ok) {
        ++panel.packed_scroll_fallback_count;
    } else {
        panel.packed_scroll_mapped = true;
    }
    return ok;
}

bool reset_rgb565_packed_scroll(Rgb565Panel& panel) {
    panel.packed_scroll_active = false;
    panel.packed_scroll_delta_y = 0;
    const bool ok = panel.reset_scroll == nullptr || panel.reset_scroll(panel.flush_context);
    if (ok) {
        panel.packed_scroll_mapped = false;
    }
    return ok;
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

jellyframe::EmbeddedPackedRgb565Sink make_packed_rgb565_sink(Rgb565Panel& panel) {
    return jellyframe::EmbeddedPackedRgb565Sink{
        jellyframe::EmbeddedPixelFormat::Rgb565,
        panel.packed_pixels,
        panel.packed_pixel_capacity,
        panel.ordered_dither,
        flush_packed_rgb565_rect,
        &panel,
    };
}

} // namespace jellyframe_esp32s3
