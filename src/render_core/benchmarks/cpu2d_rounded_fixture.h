#pragma once

#include "render_core/geometry.h"

#include <algorithm>
#include <array>

namespace jellyframe::benchmark {

struct RoundedFixture {
    const char* name;
    Rect rect;
    int radius;
};

inline constexpr std::array<RoundedFixture, 8> kRoundedFixtures{{
    {"card", {14, 20, 144, 96}, 24},
    {"rectangle", {14, 20, 144, 96}, 0},
    {"small-radius", {23, 29, 12, 10}, 1},
    {"odd-dimensions", {35, 37, 55, 31}, 11},
    {"maximum-radius", {14, 20, 144, 96}, 48},
    {"clipped-left", {-9, 23, 70, 53}, 20},
    {"clipped-bottom-right", {101, 281, 90, 62}, 24},
    {"tiny", {3, 3, 2, 2}, 1},
}};

// Independent distance-to-inset-rectangle oracle for these bounded uniform-radius
// fixtures. Do not call Core's raster/coverage helpers to qualify their output.
inline int rounded_fixture_coverage(const RoundedFixture& fixture, int x, int y) {
    const Rect rect = fixture.rect;
    if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) return 0;
    const int radius = fixture.radius;
    int covered = 0;
    for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
            const int px = x * 4 + sx;
            const int py = y * 4 + sy;
            const int nearest_x = std::clamp(px, (rect.x + radius) * 4,
                                           (rect.x + rect.width - radius) * 4);
            const int nearest_y = std::clamp(py, (rect.y + radius) * 4,
                                           (rect.y + rect.height - radius) * 4);
            const int dx = px - nearest_x;
            const int dy = py - nearest_y;
            if (dx * dx + dy * dy <= radius * radius * 16) ++covered;
        }
    }
    return (covered * 255 + 8) / 16;
}

inline Color rounded_fixture_color(Color source, int coverage) {
    return {static_cast<std::uint8_t>((source.r * coverage + 127) / 255),
            static_cast<std::uint8_t>((source.g * coverage + 127) / 255),
            static_cast<std::uint8_t>((source.b * coverage + 127) / 255), 255};
}

} // namespace jellyframe::benchmark
