#pragma once

#include "render_core/geometry.h"
#include "render_core/host.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe {

enum class EmbeddedPixelFormat {
    Rgba8888,
    Bgra8888,
    Rgb565,
    Bgr565,
    Rgb332,
    Gray8,
    Mono1Msb,
    Mono1Lsb,
};

struct EmbeddedFrameBufferTarget {
    int width = 0;
    int height = 0;
    EmbeddedPixelFormat format = EmbeddedPixelFormat::Rgb565;
    std::uint8_t* pixels = nullptr;
    std::size_t byte_size = 0;
    std::size_t stride_bytes = 0;
    bool ordered_dither = false;
};

using EmbeddedFlushCallback = bool (*)(Rect dirty_rect, void* context);

struct EmbeddedFrameBufferSink {
    EmbeddedFrameBufferTarget target;
    EmbeddedFlushCallback flush = nullptr;
    void* flush_context = nullptr;
};

std::size_t embedded_framebuffer_min_stride_bytes(int width, EmbeddedPixelFormat format);
std::size_t embedded_framebuffer_min_size(int width,
                                          int height,
                                          EmbeddedPixelFormat format,
                                          std::size_t stride_bytes = 0);

bool present_to_embedded_framebuffer(const HostFrameBufferView& frame,
                                     const Rect* dirty_rects,
                                     std::size_t dirty_rect_count,
                                     EmbeddedFrameBufferSink& sink);

HostFrameSink embedded_frame_sink(EmbeddedFrameBufferSink& sink);

} // namespace jellyframe
