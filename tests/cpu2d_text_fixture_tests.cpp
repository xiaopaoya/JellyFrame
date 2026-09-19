#include "../src/render_core/benchmarks/cpu2d_text_fixture.h"

#include <stdexcept>

using namespace jellyframe;

namespace {
bool matches_oracle(const FrameBuffer& frame, Color ink) {
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const Color expected = benchmark::clock_text_ink(x, y) ? ink : Color{0, 0, 0, 255};
            const Color actual = frame.pixel(x, y);
            if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b) return false;
        }
    }
    return true;
}
}

int main() {
    const Color ink{22, 71, 87, 255};
    FrameBuffer frame(172, 320, Color{0, 0, 0, 255});
    BitmapFontContext context{&benchmark::kClockFont, benchmark::kClockScale};
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context, nullptr, true});
    auto commands = benchmark::clock_text_commands(ink);
    for (const auto& command : commands) {
        if (measure_bitmap_text(context, command.text, 14, 400).width != 132) {
            throw std::runtime_error("fixture advance changed");
        }
        for (unsigned char codepoint : command.text) {
            if (find_bitmap_glyph(benchmark::kClockFont, codepoint) == nullptr) {
                throw std::runtime_error("fixture must never exercise missing-font fallback");
            }
        }
        rasterizer.rasterize(command, frame, command.rect);
    }
    if (!matches_oracle(frame, ink)) throw std::runtime_error("text fixture differs from oracle");
    std::size_t count = 0;
    for (const auto& pixel : frame.pixels) if (pixel.r != 0) ++count;
    if (count != 4384) throw std::runtime_error("fixture ink-pixel accounting changed");
    frame.pixel(0, 0) = ink;
    if (matches_oracle(frame, ink)) throw std::runtime_error("oracle ignored writes outside text");
    frame.clear(Color{0, 0, 0, 255});
    for (auto& command : commands) {
        command.text_align = TextCommandAlign::Start;
        rasterizer.rasterize(command, frame, command.rect);
    }
    if (matches_oracle(frame, ink)) throw std::runtime_error("oracle ignored alignment change");
    frame.clear(Color{0, 0, 0, 255});
    if (matches_oracle(frame, ink)) throw std::runtime_error("oracle accepted blank output");
}
