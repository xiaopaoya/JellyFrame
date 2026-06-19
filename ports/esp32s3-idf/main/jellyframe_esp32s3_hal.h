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

using Rgb565PackedRectFlushCallback = bool (*)(const std::uint16_t* pixels,
                                               jellyframe::Rect dirty_rect,
                                               void* context);

struct Rgb565Panel {
    std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    Rgb565PanelFlushCallback flush = nullptr;
    Rgb565PackedRectFlushCallback packed_flush = nullptr;
    void* flush_context = nullptr;
    std::uint16_t* scratch_pixels = nullptr;
    std::size_t scratch_pixel_capacity = 0;
    std::uint32_t flush_count = 0;
    std::uint32_t packed_flush_count = 0;
    std::uint32_t scratch_flush_count = 0;
    std::uint32_t failed_flush_count = 0;
    std::uint32_t flushed_pixels = 0;
    std::uint32_t flushed_bytes = 0;
    jellyframe::Rect last_dirty_rect{};
};

jellyframe::HostClock make_clock();

jellyframe::EmbeddedFrameBufferSink make_rgb565_sink(Rgb565Panel& panel);

std::size_t rgb565_buffer_pixels(int width, int height, int stride_pixels = 0);

bool flush_rgb565_packed_rect(Rgb565Panel& panel, jellyframe::Rect dirty_rect);

} // namespace jellyframe_esp32s3
