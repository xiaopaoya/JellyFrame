#pragma once

#include "render_core/text_system.h"

#include <string>

namespace jellyframe {

constexpr std::size_t kPreparedTextHeaderBytes = 32;
constexpr std::size_t kPreparedTextRecordBytes = 52;
constexpr std::size_t kMaxPreparedTextGlyphs = 4096;
constexpr std::size_t kMaxPreparedTextBytes =
    kPreparedTextHeaderBytes + kPreparedTextRecordBytes * kMaxPreparedTextGlyphs;

// Borrowed immutable JFTP v1 packet. Decoding is allocation-free and never
// creates typed aliases into potentially unaligned transport storage.
class PreparedTextView {
public:
    bool valid() const noexcept { return !packet_.empty(); }
    std::size_t size() const noexcept { return count_; }
    std::int32_t size_q1000() const noexcept { return size_q1000_; }
    std::uint32_t source_bytes() const noexcept { return source_bytes_; }
    bool read(std::size_t index, PreparedTextGlyph& output) const noexcept;
private:
    friend bool decode_prepared_text(std::string_view, PreparedTextView&) noexcept;
    std::string_view packet_;
    std::size_t count_ = 0;
    std::int32_t size_q1000_ = 0;
    std::uint32_t source_bytes_ = 0;
};

// JFTP carries immutable per-glyph choices and fixed-point geometry, not raw
// pointers or source text. Encode is transactional; decode preserves its view
// on refusal. This is a packet codec, not a general Unicode shaping format.
bool encode_prepared_text(const PreparedTextGlyph* glyphs, std::size_t count,
                          std::int32_t size_q1000, std::uint32_t source_bytes,
                          std::string& output) noexcept;
bool decode_prepared_text(std::string_view packet, PreparedTextView& output) noexcept;

} // namespace jellyframe
