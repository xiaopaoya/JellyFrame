#pragma once

#include "render_core/embedded_framebuffer.h"
#include "render_core/host.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3 {

using Rgb565PanelFlushCallback = bool (*)(const std::uint16_t* pixels,
                                          int width,
                                          int height,
                                          int stride_pixels,
                                          jellyframe::Rect dirty_rect,
                                          void* context);

struct Rgb565PackedFlushMetrics {
    std::uint32_t convert_us = 0;
    std::uint32_t window_setup_us = 0;
    std::uint32_t scroll_setup_us = 0;
    std::uint32_t dma_submit_us = 0;
    std::uint32_t dma_wait_us = 0;
    std::uint32_t chunks = 0;
    std::uint32_t scroll_wraps = 0;
};

using Rgb565PackedRectFlushCallback = bool (*)(const std::uint16_t* pixels,
                                               jellyframe::Rect dirty_rect,
                                               Rgb565PackedFlushMetrics* metrics,
                                               void* context);

using Rgb565PackedScrollFlushCallback = bool (*)(const std::uint16_t* pixels,
                                                 jellyframe::Rect exposed_strip,
                                                 int scroll_delta_y,
                                                 Rgb565PackedFlushMetrics* metrics,
                                                 void* context);

using Rgb565PanelScrollResetCallback = bool (*)(void* context);

struct Rgb565Panel {
    std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    Rgb565PanelFlushCallback flush = nullptr;
    Rgb565PackedRectFlushCallback packed_flush = nullptr;
    Rgb565PackedScrollFlushCallback packed_scroll_flush = nullptr;
    Rgb565PanelScrollResetCallback reset_scroll = nullptr;
    void* flush_context = nullptr;
    std::uint16_t* scratch_pixels = nullptr;
    std::size_t scratch_pixel_capacity = 0;
    std::uint16_t* packed_pixels = nullptr;
    std::size_t packed_pixel_capacity = 0;
    std::uint32_t flush_count = 0;
    std::uint32_t packed_flush_count = 0;
    std::uint32_t packed_scroll_flush_count = 0;
    std::uint32_t packed_scroll_fallback_count = 0;
    std::uint32_t packed_scroll_wrap_count = 0;
    std::uint32_t scratch_flush_count = 0;
    std::uint32_t failed_flush_count = 0;
    std::uint32_t flushed_pixels = 0;
    std::uint32_t flushed_bytes = 0;
    // Set by the UI task immediately before present_frame(); consumed by the
    // flush callback after the Core has converted the next dirty rectangle.
    std::uint64_t framebuffer_convert_start_us = 0;
    std::uint64_t framebuffer_convert_us = 0;
    std::uint64_t scratch_copy_us = 0;
    std::uint64_t panel_convert_us = 0;
    std::uint64_t panel_window_setup_us = 0;
    std::uint64_t panel_scroll_setup_us = 0;
    std::uint64_t panel_dma_submit_us = 0;
    std::uint64_t panel_dma_wait_us = 0;
    std::uint32_t panel_dma_chunks = 0;
    bool packed_scroll_active = false;
    bool packed_scroll_mapped = false;
    int packed_scroll_delta_y = 0;
    jellyframe::Rect last_dirty_rect{};
};

jellyframe::HostClock make_clock();

jellyframe::EmbeddedFrameBufferSink make_rgb565_sink(Rgb565Panel& panel);
jellyframe::EmbeddedPackedRgb565Sink make_packed_rgb565_sink(Rgb565Panel& panel);

std::size_t rgb565_buffer_pixels(int width, int height, int stride_pixels = 0);

bool flush_rgb565_packed_rect(Rgb565Panel& panel, jellyframe::Rect dirty_rect);
bool flush_rgb565_packed_scroll_strip(const jellyframe::HostFrameBufferView& frame,
                                      Rgb565Panel& panel,
                                      jellyframe::Rect exposed_strip,
                                      int scroll_delta_y);
bool reset_rgb565_packed_scroll(Rgb565Panel& panel);

} // namespace jellyframe_esp32s3
