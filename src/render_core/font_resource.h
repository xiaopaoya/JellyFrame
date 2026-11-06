#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace jellyframe {

// Canonical prepared-font profile. No kerning, ligatures, bidi or contextual
// shaping is implied by a scalar bitmap asset.
enum class FontResourceProfile : std::uint16_t { ScalarBitmap = 1 };
enum class FontResourceStatus {
    Loaded, InvalidHeader, UnsupportedFormat, ResourceLimit, ChecksumMismatch,
    InvalidMetrics, InvalidGlyph
};
struct FontResourceLimits {
    std::size_t max_bytes = 8U * 1024U * 1024U;
    std::size_t max_glyphs = 65535;
    // Conservative byte/record visits: resource bytes + bitmap bytes + glyphs.
    std::size_t max_work = 16U * 1024U * 1024U + 65535U;
    std::uint16_t max_bitmap_dimension = 4096;
};
struct FontFaceMetadata {
    FontResourceProfile profile = FontResourceProfile::ScalarBitmap;
    std::uint16_t units_per_em = 0;
    std::uint16_t raster_pixel_size = 0;
    std::uint16_t weight = 0;
    std::uint8_t coverage_bits = 0;
    // Original hhea units; descent is stored as a nonnegative extent.
    std::int32_t ascent = 0, descent = 0, line_gap = 0;
    std::uint32_t fallback_codepoint = 0; // Zero means no declared fallback.
    std::array<std::uint8_t, 32> source_sha256{};
};
struct FontGlyphView {
    std::uint32_t codepoint = 0, glyph_id = 0;
    std::int32_t advance = 0; // Original font units; zero is meaningful.
    // Raster-pixel offset from the baseline pen; Y increases downwards.
    std::int32_t bearing_x = 0, bearing_y = 0;
    std::uint16_t width = 0, height = 0;
    std::uint32_t row_stride = 0;
    const std::uint8_t* coverage = nullptr;
    std::size_t coverage_bytes = 0;
};
struct FontScaledMetrics {
    std::int32_t ascent = 0, descent = 0, line_gap = 0, line_height = 0;
};
struct FontScaledGlyphMetrics {
    std::int32_t advance = 0, bearing_x = 0, bearing_y = 0, width = 0, height = 0;
};

// Validated immutable borrowed resource; no allocation, font-policy callback or
// hidden fallback. The bytes must remain alive AND unchanged for every copy/view.
// load accepts only JFFONT2 and preserves the previous resource on every refusal.
class FontResource {
public:
    FontResourceStatus load(const std::uint8_t* data, std::size_t size,
                            FontResourceLimits limits = {}) noexcept;
    void clear() noexcept { *this = FontResource{}; }
    bool valid() const noexcept { return data_ != nullptr; }
    const FontFaceMetadata& metadata() const noexcept { return metadata_; }
    std::size_t glyph_count() const noexcept { return glyph_count_; }
    bool find_glyph(std::uint32_t codepoint, FontGlyphView& glyph) const noexcept;
    // Size and all returned coordinates are Q1000 CSS pixels. Original-unit
    // metrics and raster bearings have distinct denominators. Round nearest,
    // exact ties away from zero; reject overflow. False zeroes output.
    bool metrics_at_size(std::int32_t size_q1000, FontScaledMetrics& metrics) const noexcept;
    bool glyph_metrics_at_size(std::uint32_t codepoint, std::int32_t size_q1000,
                                FontScaledGlyphMetrics& metrics) const noexcept;

private:
    FontGlyphView glyph_at(std::size_t index) const noexcept;
    const std::uint8_t* data_ = nullptr;
    std::size_t glyph_count_ = 0, bitmap_offset_ = 0;
    FontFaceMetadata metadata_{};
};

} // namespace jellyframe
