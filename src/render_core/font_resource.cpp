#include "render_core/font_resource.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace jellyframe {
namespace {
constexpr std::size_t header_bytes = 128, glyph_bytes = 40;
constexpr std::int32_t metric_limit = 1 << 20;
std::uint16_t u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8U));
}
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return p[0] | (static_cast<std::uint32_t>(p[1]) << 8U) |
        (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}
std::int32_t i32(const std::uint8_t* p) noexcept {
    const auto v = u32(p);
    return v <= 0x7fffffffU ? static_cast<std::int32_t>(v)
        : static_cast<std::int32_t>(-1 - static_cast<std::int64_t>(0xffffffffU - v));
}
bool scalar(std::uint32_t c) noexcept { return c <= 0x10ffffU && (c < 0xd800U || c > 0xdfffU); }
bool bounded(std::int32_t v) noexcept { return v >= -metric_limit && v <= metric_limit; }
bool zero(const std::uint8_t* p, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}
std::uint32_t checksum(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= i >= 96 && i < 100 ? 0U : p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
bool scale(std::int32_t v, std::int32_t size, std::uint16_t divisor, std::int32_t& out) noexcept {
    const std::int64_t product = static_cast<std::int64_t>(v) * size;
    const auto rounded = product < 0 ? -((-product + divisor / 2) / divisor)
        : (product + divisor / 2) / divisor;
    if (rounded < std::numeric_limits<std::int32_t>::min() || rounded > std::numeric_limits<std::int32_t>::max()) return false;
    out = static_cast<std::int32_t>(rounded);
    return true;
}
} // namespace

FontResourceStatus FontResource::load(const std::uint8_t* data, std::size_t size,
                                       FontResourceLimits limits) noexcept {
    using S = FontResourceStatus;
    if (!data || size < header_bytes) return S::InvalidHeader;
    if (size > limits.max_bytes) return S::ResourceLimit;
    constexpr char magic[] = "JFFONT2";
    if (std::memcmp(data, magic, 8) != 0 || u16(data + 8) != 2 ||
        u16(data + 14) != static_cast<unsigned>(FontResourceProfile::ScalarBitmap) ||
        u32(data + 100) != 1 || u16(data + 104) != 1) return S::UnsupportedFormat;
    // Profile100 is project Pillow/FreeType grayscale bitmap preparation;
    // metrics source104 is hhea. Neither platform callbacks nor inferred A/D.
    if (u16(data + 10) != header_bytes || u16(data + 12) != glyph_bytes ||
        u32(data + 16) != size || u32(data + 24) != header_bytes || data[43] ||
        u32(data + 60) || !zero(data + 106, 22)) return S::InvalidHeader;
    const std::size_t count = u32(data + 20), bitmap = u32(data + 28), bytes = u32(data + 32);
    if (!count || count > 65535 || count > limits.max_glyphs) return S::ResourceLimit;
    if (count > (size - header_bytes) / glyph_bytes || bitmap != header_bytes + count * glyph_bytes ||
        bitmap > size || bytes != size - bitmap) return S::InvalidHeader;
    if (size > limits.max_work || bytes > limits.max_work - size ||
        count > limits.max_work - size - bytes) return S::ResourceLimit;
    if (checksum(data, size) != u32(data + 96)) return S::ChecksumMismatch;
    FontResource draft;
    auto& m = draft.metadata_;
    m.units_per_em = u16(data + 36); m.raster_pixel_size = u16(data + 38);
    m.weight = u16(data + 40); m.coverage_bits = data[42];
    m.ascent = i32(data + 44); m.descent = i32(data + 48); m.line_gap = i32(data + 52);
    m.fallback_codepoint = u32(data + 56);
    std::copy_n(data + 64, m.source_sha256.size(), m.source_sha256.begin());
    const auto natural = static_cast<std::int64_t>(m.ascent) + m.descent + m.line_gap;
    if (!m.units_per_em || m.units_per_em > 16384 || !m.raster_pixel_size || m.raster_pixel_size > 4096 ||
        !m.weight || m.weight > 1000 || (m.coverage_bits != 1 && m.coverage_bits != 2 && m.coverage_bits != 4 && m.coverage_bits != 8) ||
        m.ascent < 0 || m.descent < 0 || !bounded(m.ascent) || !bounded(m.descent) ||
        !bounded(m.line_gap) || natural < 0 || natural > metric_limit ||
        !scalar(m.fallback_codepoint) || zero(data + 64, 32)) return S::InvalidMetrics;
    draft.data_ = data; draft.glyph_count_ = count; draft.bitmap_offset_ = bitmap;
    std::uint32_t previous = 0;
    std::size_t used = 0;
    bool fallback_found = !m.fallback_codepoint;
    for (std::size_t i = 0; i < count; ++i) {
        const auto* p = data + header_bytes + i * glyph_bytes;
        const auto g = draft.glyph_at(i);
        const auto offset = u32(p + 28), length = u32(p + 32);
        const auto stride = (static_cast<std::uint32_t>(g.width) * m.coverage_bits + 7U) / 8U;
        if (!scalar(g.codepoint) || (i && g.codepoint <= previous) || !g.glyph_id || g.glyph_id > 65535 ||
            g.advance < 0 || !bounded(g.advance) || !bounded(g.bearing_x) || !bounded(g.bearing_y) ||
            g.width > 4096 || g.height > 4096 ||
            g.width > limits.max_bitmap_dimension || g.height > limits.max_bitmap_dimension ||
            (!g.width != !g.height) || g.row_stride != stride || u32(p + 36) ||
            offset != used || used > bytes || length > bytes - used ||
            static_cast<std::uint64_t>(g.row_stride) * g.height != length ||
            (!g.width && (g.bearing_x || g.bearing_y))) return S::InvalidGlyph;
        // Canonical row padding bits are zero, so no unreferenced coverage data.
        const auto trailing = (static_cast<unsigned>(g.width) * m.coverage_bits) % 8U;
        if (trailing) {
            const unsigned mask = (1U << (8U - trailing)) - 1U;
            for (unsigned y = 0; y < g.height; ++y)
                if (data[bitmap + used + static_cast<std::size_t>(y + 1) * stride - 1] & mask) return S::InvalidGlyph;
        }
        used += length; previous = g.codepoint;
        fallback_found |= g.codepoint == m.fallback_codepoint;
    }
    if (used != bytes || !fallback_found) return S::InvalidGlyph;
    *this = draft;
    return S::Loaded;
}

FontGlyphView FontResource::glyph_at(std::size_t index) const noexcept {
    const auto* p = data_ + header_bytes + index * glyph_bytes;
    FontGlyphView g;
    g.codepoint = u32(p); g.glyph_id = u32(p + 4); g.advance = i32(p + 8);
    g.bearing_x = i32(p + 12); g.bearing_y = i32(p + 16);
    g.width = u16(p + 20); g.height = u16(p + 22); g.row_stride = u32(p + 24);
    g.coverage_bytes = u32(p + 32);
    // During load, offsets have not yet been validated. Form the view only after
    // checking the record's range in load; queries use a fully validated asset.
    g.coverage = nullptr;
    return g;
}

bool FontResource::find_glyph(std::uint32_t codepoint, FontGlyphView& out) const noexcept {
    out = {};
    if (!valid() || !scalar(codepoint)) return false;
    std::size_t left = 0, right = glyph_count_;
    while (left < right) {
        const auto middle = left + (right - left) / 2;
        const auto g = glyph_at(middle);
        if (g.codepoint < codepoint) left = middle + 1;
        else right = middle;
    }
    if (left == glyph_count_) return false;
    auto g = glyph_at(left);
    if (g.codepoint != codepoint) return false;
    const auto offset = u32(data_ + header_bytes + left * glyph_bytes + 28);
    g.coverage = g.coverage_bytes ? data_ + bitmap_offset_ + offset : nullptr;
    out = g;
    return true;
}

bool FontResource::metrics_at_size(std::int32_t size, FontScaledMetrics& out) const noexcept {
    out = {};
    if (!valid() || size < 0) return false;
    FontScaledMetrics draft;
    const auto& m = metadata_;
    if (!scale(m.ascent, size, m.units_per_em, draft.ascent) ||
        !scale(m.descent, size, m.units_per_em, draft.descent) ||
        !scale(m.line_gap, size, m.units_per_em, draft.line_gap) ||
        !scale(m.ascent + m.descent + m.line_gap, size, m.units_per_em, draft.line_height)) return false;
    out = draft;
    return true;
}

bool FontResource::glyph_metrics_at_size(std::uint32_t cp, std::int32_t size,
                                         FontScaledGlyphMetrics& out) const noexcept {
    out = {};
    FontGlyphView g;
    if (size < 0 || !find_glyph(cp, g)) return false;
    FontScaledGlyphMetrics draft;
    if (!scale(g.advance, size, metadata_.units_per_em, draft.advance) ||
        !scale(g.bearing_x, size, metadata_.raster_pixel_size, draft.bearing_x) ||
        !scale(g.bearing_y, size, metadata_.raster_pixel_size, draft.bearing_y) ||
        !scale(g.width, size, metadata_.raster_pixel_size, draft.width) ||
        !scale(g.height, size, metadata_.raster_pixel_size, draft.height)) return false;
    out = draft;
    return true;
}
} // namespace jellyframe
