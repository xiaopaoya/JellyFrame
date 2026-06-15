#pragma once

#include "core/embedded_framebuffer.h"
#include "core/host.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3 {

using Rgb565PanelFlushCallback = bool (*)(const std::uint16_t* pixels,
                                          int width,
                                          int height,
                                          int stride_pixels,
                                          jellyframe::Rect dirty_rect,
                                          void* context);

struct Rgb565Panel {
    std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    Rgb565PanelFlushCallback flush = nullptr;
    void* flush_context = nullptr;
    std::uint32_t flush_count = 0;
    std::uint32_t flushed_pixels = 0;
};

jellyframe::HostClock make_clock();

jellyframe::EmbeddedFrameBufferSink make_rgb565_sink(Rgb565Panel& panel);

std::size_t rgb565_buffer_pixels(int width, int height, int stride_pixels = 0);

} // namespace jellyframe_esp32s3
