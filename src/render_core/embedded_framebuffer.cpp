#include "render_core/embedded_framebuffer.h"

#include <algorithm>

namespace jellyframe {
namespace {

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(left.x + left.width, right.x + right.width);
    const int y2 = std::min(left.y + left.height, right.y + right.height);
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

std::uint8_t opaque_channel(std::uint8_t channel, std::uint8_t alpha) {
    return static_cast<std::uint8_t>((static_cast<unsigned>(channel) * alpha + 127U) / 255U);
}

Color opaque_color(Color color) {
    if (color.a == 255) {
        return color;
    }
    return Color{opaque_channel(color.r, color.a),
                 opaque_channel(color.g, color.a),
                 opaque_channel(color.b, color.a),
                 255};
}

int bayer4_threshold(int x, int y) {
    static constexpr std::uint8_t kBayer4[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    return static_cast<int>(kBayer4[y & 3][x & 3]);
}

std::uint8_t quantize_channel(std::uint8_t channel, int bits, int x, int y, bool ordered_dither) {
    const int max_value = (1 << bits) - 1;
    const int bias = ordered_dither ? bayer4_threshold(x, y) * 16 : 127;
    return static_cast<std::uint8_t>(std::min(max_value, (static_cast<int>(channel) * max_value + bias) / 255));
}

std::uint16_t pack_565(Color color, bool bgr, int x, int y, bool ordered_dither) {
    color = opaque_color(color);
    const std::uint16_t r = quantize_channel(color.r, 5, x, y, ordered_dither);
    const std::uint16_t g = quantize_channel(color.g, 6, x, y, ordered_dither);
    const std::uint16_t b = quantize_channel(color.b, 5, x, y, ordered_dither);
    if (bgr) {
        return static_cast<std::uint16_t>((b << 11) | (g << 5) | r);
    }
    return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}

std::uint8_t luminance(Color color) {
    color = opaque_color(color);
    return static_cast<std::uint8_t>((77U * color.r + 150U * color.g + 29U * color.b) >> 8);
}

std::size_t actual_stride(const EmbeddedFrameBufferTarget& target) {
    return target.stride_bytes != 0
        ? target.stride_bytes
        : embedded_framebuffer_min_stride_bytes(target.width, target.format);
}

bool valid_target(const EmbeddedFrameBufferTarget& target) {
    if (target.width <= 0 || target.height <= 0 || target.pixels == nullptr) {
        return false;
    }
    const std::size_t min_stride = embedded_framebuffer_min_stride_bytes(target.width, target.format);
    const std::size_t stride = actual_stride(target);
    if (min_stride == 0 || stride < min_stride) {
        return false;
    }
    const std::size_t required = stride * static_cast<std::size_t>(target.height - 1) + min_stride;
    return target.byte_size >= required;
}

void write_pixel(EmbeddedFrameBufferTarget& target, int x, int y, Color color) {
    const std::size_t stride = actual_stride(target);
    std::uint8_t* row = target.pixels + stride * static_cast<std::size_t>(y);
    color = opaque_color(color);

    switch (target.format) {
    case EmbeddedPixelFormat::Rgba8888: {
        std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4U;
        pixel[0] = color.r;
        pixel[1] = color.g;
        pixel[2] = color.b;
        pixel[3] = color.a;
        break;
    }
    case EmbeddedPixelFormat::Bgra8888: {
        std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4U;
        pixel[0] = color.b;
        pixel[1] = color.g;
        pixel[2] = color.r;
        pixel[3] = color.a;
        break;
    }
    case EmbeddedPixelFormat::Rgb565:
    case EmbeddedPixelFormat::Bgr565: {
        const bool bgr = target.format == EmbeddedPixelFormat::Bgr565;
        const std::uint16_t packed = pack_565(color, bgr, x, y, target.ordered_dither);
        std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 2U;
        pixel[0] = static_cast<std::uint8_t>(packed & 0xffU);
        pixel[1] = static_cast<std::uint8_t>(packed >> 8);
        break;
    }
    case EmbeddedPixelFormat::Rgb332: {
        row[x] = static_cast<std::uint8_t>((color.r & 0xe0U) | ((color.g >> 3) & 0x1cU) | (color.b >> 6));
        break;
    }
    case EmbeddedPixelFormat::Gray8:
        row[x] = luminance(color);
        break;
    case EmbeddedPixelFormat::Mono1Msb:
    case EmbeddedPixelFormat::Mono1Lsb: {
        const std::size_t byte_index = static_cast<std::size_t>(x) >> 3;
        const int bit_index = x & 7;
        const std::uint8_t mask = target.format == EmbeddedPixelFormat::Mono1Msb
            ? static_cast<std::uint8_t>(0x80U >> bit_index)
            : static_cast<std::uint8_t>(1U << bit_index);
        if (luminance(color) >= 128) {
            row[byte_index] = static_cast<std::uint8_t>(row[byte_index] | mask);
        } else {
            row[byte_index] = static_cast<std::uint8_t>(row[byte_index] & ~mask);
        }
        break;
    }
    }
}

bool present_callback(const HostFrameBufferView& frame,
                      const Rect* dirty_rects,
                      std::size_t dirty_rect_count,
                      void* context) {
    if (context == nullptr) {
        return false;
    }
    auto* sink = static_cast<EmbeddedFrameBufferSink*>(context);
    return present_to_embedded_framebuffer(frame, dirty_rects, dirty_rect_count, *sink);
}

} // namespace

std::size_t embedded_framebuffer_min_stride_bytes(int width, EmbeddedPixelFormat format) {
    if (width <= 0) {
        return 0;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    switch (format) {
    case EmbeddedPixelFormat::Rgba8888:
    case EmbeddedPixelFormat::Bgra8888:
        return w * 4U;
    case EmbeddedPixelFormat::Rgb565:
    case EmbeddedPixelFormat::Bgr565:
        return w * 2U;
    case EmbeddedPixelFormat::Rgb332:
    case EmbeddedPixelFormat::Gray8:
        return w;
    case EmbeddedPixelFormat::Mono1Msb:
    case EmbeddedPixelFormat::Mono1Lsb:
        return (w + 7U) / 8U;
    }
    return 0;
}

std::size_t embedded_framebuffer_min_size(int width,
                                          int height,
                                          EmbeddedPixelFormat format,
                                          std::size_t stride_bytes) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::size_t min_stride = embedded_framebuffer_min_stride_bytes(width, format);
    const std::size_t stride = stride_bytes == 0 ? min_stride : stride_bytes;
    if (stride < min_stride) {
        return 0;
    }
    return stride * static_cast<std::size_t>(height - 1) + min_stride;
}

bool present_to_embedded_framebuffer(const HostFrameBufferView& frame,
                                     const Rect* dirty_rects,
                                     std::size_t dirty_rect_count,
                                     EmbeddedFrameBufferSink& sink) {
    if (frame.width <= 0 || frame.height <= 0 || frame.pixels == nullptr ||
        frame.stride_pixels < frame.width || !valid_target(sink.target) ||
        sink.target.width != frame.width || sink.target.height != frame.height) {
        return false;
    }

    const Rect full{0, 0, frame.width, frame.height};
    const bool full_present = dirty_rects == nullptr || dirty_rect_count == 0;
    const std::size_t count = full_present ? 1U : dirty_rect_count;

    for (std::size_t index = 0; index < count; ++index) {
        const Rect source_rect = full_present ? full : dirty_rects[index];
        const Rect dirty = intersect_rect(source_rect, full);
        if (empty_rect(dirty)) {
            continue;
        }
        for (int y = dirty.y; y < dirty.y + dirty.height; ++y) {
            const Color* source_row = frame.pixels + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(frame.stride_pixels);
            for (int x = dirty.x; x < dirty.x + dirty.width; ++x) {
                write_pixel(sink.target, x, y, source_row[x]);
            }
        }
        if (sink.flush != nullptr && !sink.flush(dirty, sink.flush_context)) {
            return false;
        }
    }
    return true;
}

HostFrameSink embedded_frame_sink(EmbeddedFrameBufferSink& sink) {
    return HostFrameSink{present_callback, &sink};
}

} // namespace jellyframe
