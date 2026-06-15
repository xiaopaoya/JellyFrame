#include "core/embedded_framebuffer.h"
#include "core/software_renderer.h"

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

bool record_flush(Rect rect, void* context) {
    auto* probe = static_cast<FlushProbe*>(context);
    ++probe->count;
    probe->last = rect;
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

    check(present_to_embedded_framebuffer(frame_buffer_view(source), &dirty, 1, sink),
          "rgb565 present succeeds");
    check(probe.count == 1 && probe.last.x == 1 && probe.last.width == 2, "flush receives dirty rect");

    const std::size_t row = embedded_framebuffer_min_stride_bytes(4, EmbeddedPixelFormat::Rgb565);
    check(target_bytes[0] == 0xcc, "outside dirty rect remains untouched");
    check(target_bytes[row + 2] == 0x00 && target_bytes[row + 3] == 0xf8, "red packs to rgb565");
    check(target_bytes[row + 4] == 0xe0 && target_bytes[row + 5] == 0x07, "green packs to rgb565");
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

} // namespace

int main() {
    try {
        stride_and_size_are_bounded();
        rgb565_present_respects_dirty_rect();
        mono_present_packs_bits();
        host_frame_sink_wrapper_presents();
        invalid_target_fails_cleanly();
    } catch (const std::exception& error) {
        std::cerr << "embedded framebuffer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "embedded framebuffer tests passed\n";
    return 0;
}
