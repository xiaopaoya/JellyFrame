#include "render_core/text_scan.h"

#include <algorithm>

namespace jellyframe {

std::uint32_t consume_utf8_codepoint(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    const auto continuation = [&](std::size_t offset) {
        return index + offset < text.size() &&
            (static_cast<unsigned char>(text[index + offset]) & 0xc0U) == 0x80U;
    };
    std::uint32_t codepoint = 0;
    std::size_t width = 0;
    if (lead <= 0x7fU) {
        codepoint = lead;
        width = 1;
    } else if (lead >= 0xc2U && lead <= 0xdfU && continuation(1)) {
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
        width = 2;
    } else if (lead >= 0xe0U && lead <= 0xefU && continuation(1) && continuation(2)) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if ((lead != 0xe0U || second >= 0xa0U) && (lead != 0xedU || second <= 0x9fU)) {
            codepoint = ((lead & 0x0fU) << 12U) |
                ((second & 0x3fU) << 6U) |
                (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
            width = 3;
        }
    } else if (lead >= 0xf0U && lead <= 0xf4U && continuation(1) &&
               continuation(2) && continuation(3)) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if ((lead != 0xf0U || second >= 0x90U) && (lead != 0xf4U || second <= 0x8fU)) {
            codepoint = ((lead & 0x07U) << 18U) |
                ((second & 0x3fU) << 12U) |
                ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
                (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
            width = 4;
        }
    }
    if (width == 0) {
        ++index;
        return 0xfffdU;
    }
    index += width;
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
