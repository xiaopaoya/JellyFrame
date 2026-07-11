#include "render_core/embedded_framebuffer.h"
#include "render_core/software_renderer.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FlushProbe {
    int count = 0;
    Rect last;
};

struct PackedFlushProbe {
    int count = 0;
    Rect last;
    std::uint16_t first = 0;
    std::uint16_t second = 0;
};

bool record_flush(Rect rect, void* context) {
    auto* probe = static_cast<FlushProbe*>(context);
    ++probe->count;
    probe->last = rect;
    return true;
}

bool record_packed_flush(const std::uint16_t* pixels, Rect rect, void* context) {
    auto* probe = static_cast<PackedFlushProbe*>(context);
    ++probe->count;
    probe->last = rect;
    probe->first = pixels[0];
    probe->second = rect.width * rect.height > 1 ? pixels[1] : 0;
    return true;
}

void stride_and_size_are_bounded() {
    check(embedded_framebuffer_min_stride_bytes(10, EmbeddedPixelFormat::Rgb565) == 20,
          "rgb565 stride");
    check(embedded_framebuffer_min_stride_bytes(9, EmbeddedPixelFormat::Mono1Msb) == 2,
          "mono stride rounds up");
    check(embedded_framebuffer_min_size(10, 3, EmbeddedPixelFormat::Rgb565) == 60,
          "tight size");
    check(embedded_framebuffer_min_size(10, 3, EmbeddedPixelFormat::Rgb565, 24) == 68,
          "padded size");
    check(embedded_framebuffer_packed_rect_bytes(5, 2, EmbeddedPixelFormat::Rgb565) == 20,
          "rgb565 packed rect bytes");
    check(embedded_framebuffer_packed_rect_bytes(9, 2, EmbeddedPixelFormat::Mono1Msb) == 4,
          "mono packed rect bytes round per row");
}

void rgb565_present_respects_dirty_rect() {
    FrameBuffer source(4, 3, Color{0, 0, 0, 255});
    source.pixel(1, 1) = Color{255, 0, 0, 255};
    source.pixel(2, 1) = Color{0, 255, 0, 255};

    std::vector<std::uint8_t> target_bytes(embedded_framebuffer_min_size(4, 3, EmbeddedPixelFormat::Rgb565),
                                           0xcc);
    FlushProbe probe;
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{4,
                                  3,
                                  EmbeddedPixelFormat::Rgb565,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0},
        record_flush,
        &probe};
    const Rect dirty{1, 1, 2, 1};
    EmbeddedFrameBufferPresentStats stats;

    check(present_to_embedded_framebuffer(frame_buffer_view(source), &dirty, 1, sink, &stats),
          "rgb565 present succeeds");
    check(probe.count == 1 && probe.last.x == 1 && probe.last.width == 2, "flush receives dirty rect");
    check(!stats.full_present && stats.source_rects == 1 && stats.clipped_rects == 1 &&
              stats.empty_rects == 0 && stats.flushes == 1,
          "present stats count dirty rect and flush");
    check(stats.converted_pixels == 2 && stats.packed_bytes == 4, "present stats count rgb565 bytes");

    const std::size_t row = embedded_framebuffer_min_stride_bytes(4, EmbeddedPixelFormat::Rgb565);
    check(target_bytes[0] == 0xcc, "outside dirty rect remains untouched");
    check(target_bytes[row + 2] == 0x00 && target_bytes[row + 3] == 0xf8, "red packs to rgb565");
    check(target_bytes[row + 4] == 0xe0 && target_bytes[row + 5] == 0x07, "green packs to rgb565");
}

void rgb565_ordered_dither_varies_quantization() {
    FrameBuffer source(4, 1, Color{127, 127, 127, 255});
    std::vector<std::uint8_t> target_bytes(embedded_framebuffer_min_size(4, 1, EmbeddedPixelFormat::Rgb565),
                                           0);
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{4,
                                  1,
                                  EmbeddedPixelFormat::Rgb565,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0,
                                  true},
        nullptr,
        nullptr};

    check(present_to_embedded_framebuffer(frame_buffer_view(source), nullptr, 0, sink),
          "rgb565 dither present succeeds");
    bool differs = false;
    for (std::size_t index = 2; index < target_bytes.size(); index += 2) {
        differs = differs || target_bytes[index] != target_bytes[0] || target_bytes[index + 1] != target_bytes[1];
    }
    check(differs, "ordered dither varies packed rgb565 values");
}

void rgb565_opaque_fast_path_matches_reference_quantization() {
    constexpr int width = 256;
    FrameBuffer source(width, 1, Color{0, 0, 0, 255});
    for (int x = 0; x < width; ++x) {
        source.pixel(x, 0) = Color{static_cast<std::uint8_t>(x),
                                   static_cast<std::uint8_t>(255 - x),
                                   static_cast<std::uint8_t>((x * 73) & 0xff),
                                   255};
    }

    std::vector<std::uint8_t> target_bytes(
        embedded_framebuffer_min_size(width, 1, EmbeddedPixelFormat::Rgb565), 0);
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{width,
                                  1,
                                  EmbeddedPixelFormat::Rgb565,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0},
        nullptr,
        nullptr};

    check(present_to_embedded_framebuffer(frame_buffer_view(source), nullptr, 0, sink),
          "rgb565 opaque fast path succeeds");
    for (int x = 0; x < width; ++x) {
        const Color color = source.pixel(x, 0);
        const std::uint16_t r = static_cast<std::uint16_t>((color.r * 31U + 127U) / 255U);
        const std::uint16_t g = static_cast<std::uint16_t>((color.g * 63U + 127U) / 255U);
        const std::uint16_t b = static_cast<std::uint16_t>((color.b * 31U + 127U) / 255U);
        const std::uint16_t expected = static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
        const std::size_t offset = static_cast<std::size_t>(x) * 2U;
        const std::uint16_t actual = static_cast<std::uint16_t>(target_bytes[offset]) |
            static_cast<std::uint16_t>(target_bytes[offset + 1]) << 8;
        check(actual == expected, "rgb565 opaque fast path preserves quantization");
    }
}

void packed_rgb565_present_uses_compact_dirty_buffer() {
    FrameBuffer source(4, 3, Color{0, 0, 0, 255});
    source.pixel(1, 1) = Color{255, 0, 0, 255};
    source.pixel(2, 1) = Color{0, 255, 0, 255};
    std::uint16_t packed_pixels[2]{};
    PackedFlushProbe probe;
    EmbeddedPackedRgb565Sink sink{
        EmbeddedPixelFormat::Rgb565,
        packed_pixels,
        2,
        false,
        record_packed_flush,
        &probe};
    EmbeddedFrameBufferPresentStats stats;
    const Rect dirty{1, 1, 2, 1};

    check(present_to_packed_rgb565(frame_buffer_view(source), &dirty, 1, sink, &stats),
          "packed rgb565 present succeeds");
    check(probe.count == 1 && probe.last.x == 1 && probe.last.width == 2,
          "packed rgb565 flush receives dirty rect");
    check(probe.first == 0xf800 && probe.second == 0x07e0,
          "packed rgb565 buffer is tightly packed in source order");
    check(stats.converted_pixels == 2 && stats.packed_bytes == 4 && stats.flushes == 1,
          "packed rgb565 present stats match dirty rect");

    sink.pixel_capacity = 1;
    check(!present_to_packed_rgb565(frame_buffer_view(source), &dirty, 1, sink),
          "packed rgb565 rejects insufficient compact buffer");

}

void mono_present_packs_bits() {
    FrameBuffer source(8, 1, Color{0, 0, 0, 255});
    for (int x = 0; x < 8; x += 2) {
        source.pixel(x, 0) = Color{255, 255, 255, 255};
    }

    std::vector<std::uint8_t> target_bytes(1, 0);
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{8,
                                  1,
                                  EmbeddedPixelFormat::Mono1Msb,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0},
        nullptr,
        nullptr};

    check(present_to_embedded_framebuffer(frame_buffer_view(source), nullptr, 0, sink),
          "mono present succeeds");
    check(target_bytes[0] == 0xaa, "mono msb packs even white pixels");
}

void host_frame_sink_wrapper_presents() {
    FrameBuffer source(2, 1, Color{0, 0, 255, 255});
    std::vector<std::uint8_t> target_bytes(embedded_framebuffer_min_size(2, 1, EmbeddedPixelFormat::Bgr565),
                                           0);
    EmbeddedFrameBufferSink embedded{
        EmbeddedFrameBufferTarget{2,
                                  1,
                                  EmbeddedPixelFormat::Bgr565,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0},
        nullptr,
        nullptr};
    const HostFrameSink sink = embedded_frame_sink(embedded);

    check(present_frame(source, sink), "host frame sink wrapper succeeds");
    check(target_bytes[0] == 0x00 && target_bytes[1] == 0xf8, "bgr565 maps blue to high bits");
}

void invalid_target_fails_cleanly() {
    FrameBuffer source(2, 2, Color{255, 255, 255, 255});
    std::vector<std::uint8_t> too_small(1, 0);
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{2,
                                  2,
                                  EmbeddedPixelFormat::Rgb565,
                                  too_small.data(),
                                  too_small.size(),
                                  0},
        nullptr,
        nullptr};

    check(!present_to_embedded_framebuffer(frame_buffer_view(source), nullptr, 0, sink),
          "too-small target fails");
}

void clipped_and_empty_rects_are_reported() {
    FrameBuffer source(3, 3, Color{16, 32, 48, 255});
    std::vector<std::uint8_t> target_bytes(embedded_framebuffer_min_size(3, 3, EmbeddedPixelFormat::Rgb332),
                                           0);
    EmbeddedFrameBufferSink sink{
        EmbeddedFrameBufferTarget{3,
                                  3,
                                  EmbeddedPixelFormat::Rgb332,
                                  target_bytes.data(),
                                  target_bytes.size(),
                                  0},
        nullptr,
        nullptr};
    const Rect dirty[] = {
        Rect{-1, -1, 2, 2},
        Rect{5, 5, 1, 1},
    };
    EmbeddedFrameBufferPresentStats stats;

    check(present_to_embedded_framebuffer(frame_buffer_view(source), dirty, 2, sink, &stats),
          "clipped present succeeds");
    check(stats.source_rects == 2 && stats.clipped_rects == 1 && stats.empty_rects == 1,
          "present stats report clipped and empty rects");
    check(stats.converted_pixels == 1 && stats.packed_bytes == 1, "present stats count clipped area");
}

void present_stats_estimate_matches_dirty_rect_accounting() {
    const Rect dirty[] = {
        Rect{1, 1, 2, 3},
        Rect{8, 8, 2, 2},
    };
    const EmbeddedFrameBufferPresentStats stats =
        estimate_embedded_framebuffer_present_stats(4, 4, EmbeddedPixelFormat::Rgb565, dirty, 2);
    check(!stats.full_present && stats.source_rects == 2 && stats.clipped_rects == 1 &&
              stats.empty_rects == 1 && stats.flushes == 1,
          "estimated stats count dirty/clipped/empty rects");
    check(stats.converted_pixels == 6 && stats.packed_bytes == 12,
          "estimated stats count converted pixels and packed bytes");

    const EmbeddedFrameBufferPresentStats full =
        estimate_embedded_framebuffer_present_stats(4, 4, EmbeddedPixelFormat::Rgb332);
    check(full.full_present && full.source_rects == 1 && full.clipped_rects == 1 &&
              full.converted_pixels == 16 && full.packed_bytes == 16,
          "estimated stats count full frame present");
}

} // namespace

int main() {
    try {
        stride_and_size_are_bounded();
        rgb565_present_respects_dirty_rect();
        rgb565_ordered_dither_varies_quantization();
        rgb565_opaque_fast_path_matches_reference_quantization();
        packed_rgb565_present_uses_compact_dirty_buffer();
        mono_present_packs_bits();
        host_frame_sink_wrapper_presents();
        invalid_target_fails_cleanly();
        clipped_and_empty_rects_are_reported();
        present_stats_estimate_matches_dirty_rect_accounting();
    } catch (const std::exception& error) {
        std::cerr << "embedded framebuffer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "embedded framebuffer tests passed\n";
    return 0;
}
