#pragma once

#include "render_core/font_resource.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

class PreparedTextView;
struct FrameBuffer;
struct Rect;
struct Color;

enum class TextSystemStatus {
    Ok, NotReady, InvalidFontSet, InvalidRequest, InvalidUtf8, UnsupportedStyle,
    UnsupportedText, MissingGlyph, ResourceLimit, OutputTooSmall, Overflow,
    InvalidPreparedText, InvalidTarget
};
enum class TextFaceStyle { Normal, Italic, Oblique };

// Registration data, not host policy. Family 0 is the required default family.
// IDs name exact prepared resources; never reuse an ID for different bytes while
// any glyph result/cache/transport referencing that ID survives.
struct TextFontFace {
    std::uint64_t resource_id = 0;
    std::uint32_t family_id = 0;
    FontResource resource;
    // Optional CSS family token hash registered by the Runtime catalog. Zero
    // denotes the generic/default family and maps to family ID 0.
    std::uint32_t css_family_hash = 0;
};
struct TextFontRequest {
    // Ordered already-resolved family identities; duplicates are ignored.
    // Default family 0 is appended if absent. No unnamed family is searched.
    const std::uint32_t* families = nullptr;
    std::size_t family_count = 0;
    std::int32_t size_q1000 = 16000;
    std::uint16_t weight = 400;
    TextFaceStyle style = TextFaceStyle::Normal;
};
struct TextSystemLimits {
    std::size_t max_faces = 64, max_families = 16;
    std::size_t max_text_bytes = 65536, max_glyphs = 4096;
    // Byte, registration and binary-search-step accounting; not CPU cycles.
    std::size_t max_work = 1024U * 1024U;
};
struct TextPaintLimits {
    std::size_t max_glyphs = 4096;
    // Sum of complete glyph pixel-center bounds, before target/dirty clipping.
    std::size_t max_pixels = 1024U * 1024U;
    std::size_t max_work = 2U * 1024U * 1024U;
};
struct TextRunMetrics {
    std::uint64_t primary_resource_id = 0;
    FontScaledMetrics primary_font;
    // Union of primary/used font extents; not the final CSS line box.
    std::int32_t ascent = 0, descent = 0, natural_height = 0;
    std::int32_t advance = 0;
    std::int32_t ink_left = 0, ink_top = 0, ink_right = 0, ink_bottom = 0;
    std::size_t glyph_count = 0, substitutions = 0;
    bool has_ink = false;
};
// Final scalar-profile geometry in Q1000 CSS pixels relative to pen/baseline.
// Value-only records, without callback/context/resource pointers. These are not
// yet a transport codec or a claim of contextual shaping/cluster-safe wrapping.
struct PreparedTextGlyph {
    std::uint64_t resource_id = 0;
    std::uint32_t glyph_id = 0, codepoint = 0, source_codepoint = 0;
    std::uint32_t source_begin = 0, source_end = 0;
    std::int32_t x = 0;
    FontScaledGlyphMetrics geometry;
};

// Project-owned selection/metrics/scalar preparation. No allocation or public
// text-policy callback. Registrations AND their resource bytes must stay alive
// and unchanged while bound; Runtime owns those lifetimes. Failed bind preserves
// the previous set. This bounded first implementation admits only the documented
// simple horizontal ASCII/Han/punctuation repertoire and upright style; it is
// not yet the production layout/paint replacement or a general Unicode shaper.
class TextSystem {
public:
    TextSystemStatus bind(const TextFontFace* faces, std::size_t count,
                          TextSystemLimits limits = {}) noexcept;
    void clear() noexcept {
        faces_ = nullptr;
        count_ = 0;
        limits_ = {};
        if (generation_ != UINT64_MAX) ++generation_;
    }
    bool ready() const noexcept { return faces_ != nullptr; }
    std::uint64_t generation() const noexcept { return generation_; }
    // Resolves the Core parser's normalized family token to a registered ID.
    // Unknown hashes and conflicting catalog mappings fail explicitly.
    bool resolve_family_hash(std::uint32_t css_family_hash,
                             std::uint32_t& family_id) const noexcept;
    TextSystemStatus measure(std::string_view text, const TextFontRequest& font,
                             TextRunMetrics& metrics) const noexcept;
    // Two-pass transactional preparation. On refusal: metrics zeroed, output
    // storage unchanged. Nonempty results require capacity >= glyph_count.
    // Output must not overlap input, registrations or font bytes. No allocation.
    TextSystemStatus prepare(std::string_view text, const TextFontRequest& font,
                             PreparedTextGlyph* output, std::size_t capacity,
                             TextRunMetrics& metrics) const noexcept;
    // Paints an immutable prepared packet using only this Core-owned font set.
    // All resource/geometry and budget checks complete before the first pixel write.
    TextSystemStatus paint(const PreparedTextView& glyphs, FrameBuffer& target,
                           Rect clip, std::int64_t origin_x_q1000,
                           std::int64_t baseline_y_q1000, Color color,
                           TextPaintLimits limits = {}) const noexcept;
private:
    TextSystemStatus process(std::string_view, const TextFontRequest&,
                             PreparedTextGlyph*, std::size_t, bool,
                             TextRunMetrics&) const noexcept;
    const TextFontFace* faces_ = nullptr;
    std::size_t count_ = 0;
    TextSystemLimits limits_{};
    std::uint64_t generation_ = 0;
};

} // namespace jellyframe
