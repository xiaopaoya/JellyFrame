#include "render_core/bitmap_font_resource.h"

#include <cstring>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::size_t kJfFontHeaderSize = 32;
constexpr std::size_t kJfFontGlyphEntrySize = 16;

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
}

bool range_within(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

bool checked_multiply(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

} // namespace

void BitmapFontResource::clear() {
    font_ = BitmapFont{};
    glyphs_.clear();
}

bool BitmapFontResource::load_jffont(const std::uint8_t* data, std::size_t size) {
    clear();
    if (data == nullptr || size < kJfFontHeaderSize) {
        return false;
    }
    static constexpr char kMagic[8] = {'J', 'F', 'F', 'O', 'N', 'T', '0', '\0'};
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    const std::uint16_t header_size = read_u16(data + 8);
    const std::uint16_t version = read_u16(data + 10);
    const std::uint32_t glyph_count = read_u32(data + 12);
    const std::uint8_t line_height = data[16];
    const std::uint8_t fallback_advance = data[17];
    const std::uint16_t flags = read_u16(data + 18);
    const std::uint32_t glyph_table_offset = read_u32(data + 20);
    const std::uint32_t row_data_offset = read_u32(data + 24);
    const std::uint32_t row_data_size = read_u32(data + 28);

    if (header_size != kJfFontHeaderSize || version > 1 ||
        line_height == 0 || fallback_advance == 0) {
        return false;
    }
    if ((version == 0 && flags != 0) || (version == 1 && (flags & 0xff00U) != 0)) {
        return false;
    }
    const std::uint8_t bits_per_pixel =
        version == 0 ? 1 : static_cast<std::uint8_t>(flags & 0x00ffU);
    if (!(bits_per_pixel == 1 || bits_per_pixel == 2 || bits_per_pixel == 4)) {
        return false;
    }
    std::size_t glyph_table_bytes = 0;
    if (!checked_multiply(static_cast<std::size_t>(glyph_count),
                          kJfFontGlyphEntrySize,
                          glyph_table_bytes)) {
        return false;
    }
    if (!range_within(glyph_table_offset, glyph_table_bytes, size) ||
        !range_within(row_data_offset, row_data_size, size)) {
        return false;
    }

    glyphs_.reserve(glyph_count);
    std::uint32_t previous_codepoint = 0;
    for (std::uint32_t index = 0; index < glyph_count; ++index) {
        const std::uint8_t* entry = data + glyph_table_offset +
            static_cast<std::size_t>(index) * kJfFontGlyphEntrySize;
        const std::uint32_t codepoint = read_u32(entry);
        const std::uint32_t row_offset = read_u32(entry + 4);
        const std::uint32_t row_size = read_u32(entry + 8);
        const std::uint8_t width = entry[12];
        const std::uint8_t height = entry[13];
        const std::uint8_t advance = entry[14];
        const std::uint8_t bytes_per_row = entry[15];
        const std::size_t minimum_row_size =
            static_cast<std::size_t>(height) * static_cast<std::size_t>(bytes_per_row);
        const std::uint8_t minimum_bytes_per_row =
            static_cast<std::uint8_t>((static_cast<int>(width) * bits_per_pixel + 7) / 8);
        if (codepoint == 0 || (index > 0 && codepoint <= previous_codepoint) ||
            width == 0 || height == 0 || bytes_per_row < minimum_bytes_per_row ||
            row_size < minimum_row_size ||
            !range_within(row_offset, row_size, row_data_size)) {
            clear();
            return false;
        }
        glyphs_.push_back(BitmapFontGlyph{
            codepoint,
            width,
            height,
            advance,
            bytes_per_row,
            data + row_data_offset + row_offset,
            bits_per_pixel,
        });
        previous_codepoint = codepoint;
    }

    font_ = BitmapFont{
        glyphs_.empty() ? nullptr : glyphs_.data(),
        glyphs_.size(),
        line_height,
        fallback_advance,
    };
    return true;
}

} // namespace jellyframe
