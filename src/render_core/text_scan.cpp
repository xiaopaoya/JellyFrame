#include "render_core/text_scan.h"

#include <algorithm>

namespace jellyframe {

std::uint32_t consume_utf8_codepoint(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U && index + 1 < text.size()) {
        width = 2;
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
    } else if ((lead & 0xf0U) == 0xe0U && index + 2 < text.size()) {
        width = 3;
        codepoint = ((lead & 0x0fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
    } else if ((lead & 0xf8U) == 0xf0U && index + 3 < text.size()) {
        width = 4;
        codepoint = ((lead & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
    }
    index += std::min(width, text.size() - index);
    return codepoint;
}

bool is_cjk_codepoint(std::uint32_t codepoint) {
    return (codepoint >= 0x3400U && codepoint <= 0x4dbfU) ||
        (codepoint >= 0x4e00U && codepoint <= 0x9fffU) ||
        (codepoint >= 0xf900U && codepoint <= 0xfaffU);
}

bool has_text_wrap_opportunity(std::string_view text) {
    int cjk_count = 0;
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        if (codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
            codepoint == '-' || codepoint == '/' ||
            codepoint == 0x3001U || codepoint == 0x3002U) {
            return true;
        }
        if (is_cjk_codepoint(codepoint)) {
            ++cjk_count;
            if (cjk_count > 1) {
                return true;
            }
        }
    }
    return false;
}

} // namespace jellyframe
