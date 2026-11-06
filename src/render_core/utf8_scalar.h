#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe::detail {

// Strict scalar decoding shared by compatibility scanning and fragment input.
// Failure leaves position unchanged and clears value; callers choose recovery.
inline bool read_utf8_scalar(std::string_view text, std::size_t& position, std::uint32_t& value) {
    value = 0;
    if (position >= text.size()) return false;
    const auto lead = static_cast<unsigned char>(text[position]);
    if (lead <= 0x7fU) { value = lead; ++position; return true; }
    const std::size_t count = lead >= 0xc2U && lead <= 0xdfU ? 2
        : lead >= 0xe0U && lead <= 0xefU ? 3 : lead >= 0xf0U && lead <= 0xf4U ? 4 : 0;
    if (count == 0 || count > text.size() - position) return false;
    std::uint32_t scalar = lead & (count == 2 ? 0x1fU : count == 3 ? 0x0fU : 0x07U);
    for (std::size_t offset = 1; offset < count; ++offset) {
        const auto byte = static_cast<unsigned char>(text[position + offset]);
        if ((byte & 0xc0U) != 0x80U) return false;
        scalar = (scalar << 6U) | (byte & 0x3fU);
    }
    if ((count == 2 && scalar < 0x80U) || (count == 3 && scalar < 0x800U) ||
        (count == 4 && scalar < 0x10000U) || scalar > 0x10ffffU ||
        (scalar >= 0xd800U && scalar <= 0xdfffU)) return false;
    value = scalar; position += count; return true;
}

} // namespace jellyframe::detail
