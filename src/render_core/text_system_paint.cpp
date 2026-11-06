#include "render_core/text_system.h"
#include "render_core/prepared_text.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
#include <limits>

namespace jellyframe {
namespace {
using Status = TextSystemStatus;
struct Budget {
    std::size_t work;
    std::size_t pixels;
    bool take(std::size_t amount) noexcept {
        if (amount > work) return false;
        work -= amount;
        return true;
    }
};
std::int64_t ceil1000(std::int64_t value) noexcept {
    return value / 1000 + (value % 1000 > 0);
}
struct Bounds { std::int64_t left = 0, top = 0, right = 0, bottom = 0; };
Bounds bounds(const PreparedTextGlyph& glyph, std::int64_t x, std::int64_t y) noexcept {
    const auto left = x + glyph.x + glyph.geometry.bearing_x;
    const auto top = y + glyph.geometry.bearing_y;
    return {ceil1000(left - 500), ceil1000(top - 500),
            ceil1000(left + glyph.geometry.width - 500),
            ceil1000(top + glyph.geometry.height - 500)};
}
bool equal(const FontScaledGlyphMetrics& left, const FontScaledGlyphMetrics& right) noexcept {
    return left.advance == right.advance && left.bearing_x == right.bearing_x &&
        left.bearing_y == right.bearing_y && left.width == right.width && left.height == right.height;
}
const TextFontFace* resolve(const TextFontFace* faces, std::size_t count, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < count; ++i)
        if (faces[i].resource_id == id) return faces + i;
    return nullptr;
}
std::size_t lookup_cost(std::size_t count) noexcept {
    std::size_t cost = 1;
    while (count) { ++cost; count >>= 1; }
    return cost;
}
unsigned coverage(const FontGlyphView& glyph, unsigned bits, unsigned x, unsigned y) noexcept {
    const auto bit = x * bits;
    const auto value = (glyph.coverage[static_cast<std::size_t>(y) * glyph.row_stride + bit / 8] >>
        (8 - bits - bit % 8)) & ((1U << bits) - 1);
    return (value * 255 + ((1U << bits) - 1) / 2) / ((1U << bits) - 1);
}
}

TextSystemStatus TextSystem::paint(const PreparedTextView& view, FrameBuffer& target, Rect clip,
    std::int64_t origin, std::int64_t baseline, Color color, TextPaintLimits limits) const noexcept {
    if (!ready()) return Status::NotReady;
    if (!view.valid()) return Status::InvalidPreparedText;
    std::size_t target_pixels = 0;
    if (target.width < 0 || target.height < 0 ||
        !checked_multiply(static_cast<std::size_t>(target.width),
                          static_cast<std::size_t>(target.height), target_pixels) ||
        target.pixels.size() != target_pixels) return Status::InvalidTarget;
    constexpr auto coordinate_limit = static_cast<std::int64_t>(std::numeric_limits<int>::max()) * 1000;
    if (clip.width < 0 || clip.height < 0 || origin < -coordinate_limit || origin > coordinate_limit ||
        baseline < -coordinate_limit || baseline > coordinate_limit) return Status::InvalidRequest;
    if (view.size() > limits.max_glyphs) return Status::ResourceLimit;

    Budget budget{limits.max_work, limits.max_pixels};
    if (!budget.take(kPreparedTextHeaderBytes + view.size() * kPreparedTextRecordBytes))
        return Status::ResourceLimit;
    for (std::size_t i = 0; i < view.size(); ++i) {
        PreparedTextGlyph glyph;
        if (!view.read(i, glyph)) return Status::InvalidPreparedText;
        if (!budget.take(2 * (count_ + 1))) return Status::ResourceLimit;
        const auto* face = resolve(faces_, count_, glyph.resource_id);
        if (!face) return Status::MissingGlyph;
        if (!budget.take(3 * lookup_cost(face->resource.glyph_count()))) return Status::ResourceLimit;
        FontGlyphView raw;
        FontScaledGlyphMetrics geometry;
        if (!face->resource.find_glyph(glyph.codepoint, raw) || raw.glyph_id != glyph.glyph_id ||
            !face->resource.glyph_metrics_at_size(glyph.codepoint, view.size_q1000(), geometry) ||
            !equal(geometry, glyph.geometry)) return Status::InvalidPreparedText;
        const auto box = bounds(glyph, origin, baseline);
        const auto width = static_cast<std::uint64_t>(box.right - box.left);
        const auto height = static_cast<std::uint64_t>(box.bottom - box.top);
        if (height && width > budget.pixels / height) return Status::ResourceLimit;
        const auto area = static_cast<std::size_t>(width * height);
        budget.pixels -= area;
        if (!budget.take(area)) return Status::ResourceLimit;
    }

    const auto visible = raster_intersect_rect(clip, {0, 0, target.width, target.height});
    if (!color.a || !visible.width || !visible.height) return Status::Ok;
    for (std::size_t i = 0; i < view.size(); ++i) {
        PreparedTextGlyph glyph;
        view.read(i, glyph);
        if (!glyph.geometry.width || !glyph.geometry.height) continue;
        const auto* face = resolve(faces_, count_, glyph.resource_id);
        FontGlyphView raw;
        face->resource.find_glyph(glyph.codepoint, raw);
        const auto box = bounds(glyph, origin, baseline);
        const auto left = std::max(box.left, static_cast<std::int64_t>(visible.x));
        const auto top = std::max(box.top, static_cast<std::int64_t>(visible.y));
        const auto right = std::min(box.right, static_cast<std::int64_t>(visible.x) + visible.width);
        const auto bottom = std::min(box.bottom, static_cast<std::int64_t>(visible.y) + visible.height);
        const auto x0 = origin + glyph.x + glyph.geometry.bearing_x;
        const auto y0 = baseline + glyph.geometry.bearing_y;
        for (auto y = top; y < bottom; ++y) {
            const auto source_y = static_cast<unsigned>(((y * 1000 + 500 - y0) * raw.height) /
                                                        glyph.geometry.height);
            for (auto x = left; x < right; ++x) {
                const auto source_x = static_cast<unsigned>(((x * 1000 + 500 - x0) * raw.width) /
                                                            glyph.geometry.width);
                const auto alpha = coverage(raw, face->resource.metadata().coverage_bits, source_x, source_y);
                blend_pixel_unchecked(target, static_cast<int>(x), static_cast<int>(y), with_coverage(color, alpha));
            }
        }
    }
    return Status::Ok;
}
} // namespace jellyframe
