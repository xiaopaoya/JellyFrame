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

// Receives a tightly packed, host-native RGB565/BGR565 word buffer for one
// dirty rectangle. The callback must consume it before returning.
using EmbeddedPackedRgb565FlushCallback = bool (*)(const std::uint16_t* pixels,
                                                    Rect dirty_rect,
                                                    void* context);

struct EmbeddedFrameBufferSink {
    EmbeddedFrameBufferTarget target;
    EmbeddedFlushCallback flush = nullptr;
    void* flush_context = nullptr;
};

struct EmbeddedPackedRgb565Sink {
    EmbeddedPixelFormat format = EmbeddedPixelFormat::Rgb565;
    std::uint16_t* pixels = nullptr;
    std::size_t pixel_capacity = 0;
    bool ordered_dither = false;
    EmbeddedPackedRgb565FlushCallback flush = nullptr;
    void* flush_context = nullptr;
};

struct EmbeddedFrameBufferPresentStats {
    bool full_present = false;
    std::size_t source_rects = 0;
    std::size_t clipped_rects = 0;
    std::size_t empty_rects = 0;
    std::size_t flushes = 0;
    std::uint64_t converted_pixels = 0;
    std::uint64_t packed_bytes = 0;
};

std::size_t embedded_framebuffer_min_stride_bytes(int width, EmbeddedPixelFormat format);
std::size_t embedded_framebuffer_min_size(int width,
                                          int height,
                                          EmbeddedPixelFormat format,
                                          std::size_t stride_bytes = 0);
std::size_t embedded_framebuffer_packed_rect_bytes(int width, int height, EmbeddedPixelFormat format);

bool present_to_embedded_framebuffer(const HostFrameBufferView& frame,
                                     const Rect* dirty_rects,
                                     std::size_t dirty_rect_count,
                                     EmbeddedFrameBufferSink& sink,
                                     EmbeddedFrameBufferPresentStats* stats = nullptr);

// Converts each clipped dirty rectangle directly into sink.pixels. This avoids
// a persistent full-size RGB565 target when the host can synchronously flush a
// compact rectangle buffer.
bool present_to_packed_rgb565(const HostFrameBufferView& frame,
                              const Rect* dirty_rects,
                              std::size_t dirty_rect_count,
                              EmbeddedPackedRgb565Sink& sink,
                              EmbeddedFrameBufferPresentStats* stats = nullptr);

EmbeddedFrameBufferPresentStats estimate_embedded_framebuffer_present_stats(int width,
                                                                            int height,
                                                                            EmbeddedPixelFormat format,
                                                                            const Rect* dirty_rects = nullptr,
                                                                            std::size_t dirty_rect_count = 0);

HostFrameSink embedded_frame_sink(EmbeddedFrameBufferSink& sink);
HostFrameSink embedded_packed_rgb565_sink(EmbeddedPackedRgb565Sink& sink);

} // namespace jellyframe
