#include "../src/render_core/benchmarks/cpu2d_rounded_fixture.h"
#include "render_core/software_renderer.h"

#include <stdexcept>

using namespace jellyframe;

namespace {
bool matches_oracle(const FrameBuffer& frame, const benchmark::RoundedFixture& fixture, Color ink) {
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const Color expected = benchmark::rounded_fixture_color(
                ink, benchmark::rounded_fixture_coverage(fixture, x, y));
            const Color actual = frame.pixel(x, y);
            if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b || actual.a != 255) return false;
        }
    }
    return true;
}
}

int main() {
    const Color ink{22, 71, 87, 255};
    FrameBuffer frame(172, 320, Color{0, 0, 0, 255});
    SoftwareRasterizer rasterizer;
    for (const auto& fixture : benchmark::kRoundedFixtures) {
        DisplayCommand command;
        command.type = DisplayCommandType::FillRect;
        command.rect = fixture.rect;
        command.border_radius = fixture.radius;
        command.color = ink;
        frame.clear(Color{0, 0, 0, 255});
        rasterizer.rasterize(command, frame, Rect{0, 0, frame.width, frame.height});
        if (!matches_oracle(frame, fixture, ink)) throw std::runtime_error("rounded fixture differs from independent oracle");
        frame.pixel(0, 0) = ink;
        if (matches_oracle(frame, fixture, ink)) throw std::runtime_error("oracle ignored out-of-bounds ink");
        frame.clear(Color{0, 0, 0, 255});
        if (matches_oracle(frame, fixture, ink)) throw std::runtime_error("oracle accepted blank output");
    }
    const auto& card = benchmark::kRoundedFixtures.front();
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = card.rect;
    command.color = ink;
    frame.clear(Color{0, 0, 0, 255});
    rasterizer.rasterize(command, frame, command.rect);
    if (matches_oracle(frame, card, ink)) throw std::runtime_error("oracle accepted missing round corners");
    command.border_radius = card.radius;
    ++command.rect.x;
    frame.clear(Color{0, 0, 0, 255});
    rasterizer.rasterize(command, frame, command.rect);
    if (matches_oracle(frame, card, ink)) throw std::runtime_error("oracle accepted translated shape");
    // Exact circle-boundary inclusion: the lower-right r=1 pixel has one
    // sample outside the circle, unlike binary fill's edge ownership rules.
    const benchmark::RoundedFixture tiny{"tiny", {0, 0, 2, 2}, 1};
    if (benchmark::rounded_fixture_coverage(tiny, 1, 1) != 239 ||
        benchmark::rounded_fixture_coverage(tiny, 0, 0) != 128 ||
        benchmark::rounded_fixture_coverage(tiny, 2, 1) != 0) {
        throw std::runtime_error("quarter-grid circle boundary changed");
    }
}
