#include "render_core/prepared_text.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace jellyframe {
namespace {
std::uint32_t u32(std::string_view s, std::size_t p) noexcept {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[p + i])) << (8 * i);
    return v;
}
std::int32_t i32(std::string_view s, std::size_t p) noexcept {
    const auto v = u32(s, p);
    return v <= 0x7fffffffU ? static_cast<std::int32_t>(v)
        : static_cast<std::int32_t>(static_cast<std::int64_t>(v) - 4294967296LL);
}
void put(std::string& s, std::size_t p, std::uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) s[p + i] = static_cast<char>((v >> (8 * i)) & 255);
}
unsigned utf8_bytes(std::uint32_t cp) noexcept {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}
bool record_valid(const PreparedTextGlyph& g, std::uint32_t& byte, std::int64_t& x) noexcept {
    const auto& m = g.geometry;
    const auto width = utf8_bytes(g.source_codepoint);
    if (!g.resource_id || !g.glyph_id || g.glyph_id > 65535 || !utf8_bytes(g.codepoint) ||
        !width || g.source_begin != byte || static_cast<std::uint64_t>(byte) + width != g.source_end ||
        g.x != x || m.advance < 0 || m.width < 0 || m.height < 0) return false;
    byte = g.source_end;
    x += m.advance;
    return x <= std::numeric_limits<std::int32_t>::max();
}
void write_record(std::string& s, std::size_t p, const PreparedTextGlyph& g) noexcept {
    put(s, p, static_cast<std::uint32_t>(g.resource_id));
    put(s, p + 4, static_cast<std::uint32_t>(g.resource_id >> 32));
    put(s, p + 8, g.glyph_id); put(s, p + 12, g.codepoint); put(s, p + 16, g.source_codepoint);
    put(s, p + 20, g.source_begin); put(s, p + 24, g.source_end); put(s, p + 28, static_cast<std::uint32_t>(g.x));
    put(s, p + 32, static_cast<std::uint32_t>(g.geometry.advance));
    put(s, p + 36, static_cast<std::uint32_t>(g.geometry.bearing_x));
    put(s, p + 40, static_cast<std::uint32_t>(g.geometry.bearing_y));
    put(s, p + 44, static_cast<std::uint32_t>(g.geometry.width));
    put(s, p + 48, static_cast<std::uint32_t>(g.geometry.height));
}
}

bool PreparedTextView::read(std::size_t index, PreparedTextGlyph& output) const noexcept {
    if (!valid() || index >= count_) return false;
    const auto p = kPreparedTextHeaderBytes + index * kPreparedTextRecordBytes;
    PreparedTextGlyph g;
    g.resource_id = u32(packet_, p) | (static_cast<std::uint64_t>(u32(packet_, p + 4)) << 32);
    g.glyph_id = u32(packet_, p + 8); g.codepoint = u32(packet_, p + 12);
    g.source_codepoint = u32(packet_, p + 16); g.source_begin = u32(packet_, p + 20);
    g.source_end = u32(packet_, p + 24); g.x = i32(packet_, p + 28);
    g.geometry = {i32(packet_, p + 32), i32(packet_, p + 36), i32(packet_, p + 40),
                  i32(packet_, p + 44), i32(packet_, p + 48)};
    output = g;
    return true;
}

bool encode_prepared_text(const PreparedTextGlyph* glyphs, std::size_t count,
                          std::int32_t size, std::uint32_t source_bytes,
                          std::string& output) noexcept {
    if (count > kMaxPreparedTextGlyphs || (count && !glyphs) || size < 0) return false;
    std::uint32_t byte = 0;
    std::int64_t x = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (!record_valid(glyphs[i], byte, x)) return false;
    if (byte != source_bytes) return false;
    try {
        std::string draft(kPreparedTextHeaderBytes + count * kPreparedTextRecordBytes, '\0');
        draft[0] = 'J'; draft[1] = 'F'; draft[2] = 'T'; draft[3] = 'P';
        draft[4] = 1; draft[6] = static_cast<char>(kPreparedTextRecordBytes);
        put(draft, 8, static_cast<std::uint32_t>(count));
        put(draft, 12, static_cast<std::uint32_t>(draft.size()));
        put(draft, 16, static_cast<std::uint32_t>(size)); put(draft, 20, source_bytes);
        for (std::size_t i = 0; i < count; ++i)
            write_record(draft, kPreparedTextHeaderBytes + i * kPreparedTextRecordBytes, glyphs[i]);
        output.swap(draft);
        return true;
    } catch (const std::bad_alloc&) { return false; }
      catch (const std::length_error&) { return false; }
}

bool decode_prepared_text(std::string_view packet, PreparedTextView& output) noexcept {
    if (packet.size() < kPreparedTextHeaderBytes || packet.size() > kMaxPreparedTextBytes ||
        packet.substr(0, 4) != "JFTP" || packet[4] != 1 || packet[5] ||
        static_cast<unsigned char>(packet[6]) != kPreparedTextRecordBytes || packet[7] ||
        i32(packet, 16) < 0 || u32(packet, 24) || u32(packet, 28)) return false;
    const auto count = u32(packet, 8);
    if (count > kMaxPreparedTextGlyphs ||
        packet.size() != kPreparedTextHeaderBytes + count * kPreparedTextRecordBytes ||
        u32(packet, 12) != packet.size()) return false;
    PreparedTextView draft;
    draft.packet_ = packet; draft.count_ = count;
    draft.size_q1000_ = i32(packet, 16); draft.source_bytes_ = u32(packet, 20);
    std::uint32_t byte = 0;
    std::int64_t x = 0;
    for (std::size_t i = 0; i < count; ++i) {
        PreparedTextGlyph g;
        if (!draft.read(i, g) || !record_valid(g, byte, x)) return false;
    }
    if (byte != draft.source_bytes_) return false;
    output = draft;
    return true;
}
} // namespace jellyframe
