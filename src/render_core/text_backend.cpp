#include "render_core/text_backend.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace jellyframe {
namespace {

constexpr std::uint32_t kFontFamilyFnvOffset = 0x811c9dc5U;
constexpr std::uint32_t kFontFamilyFnvPrime = 0x01000193U;

bool consume_utf8_codepoint(const std::string& text, std::size_t& index, unsigned char& lead) {
    if (index >= text.size()) {
        return false;
    }
    lead = static_cast<unsigned char>(text[index]);
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U) {
        width = 2;
    } else if ((lead & 0xf0U) == 0xe0U) {
        width = 3;
    } else if ((lead & 0xf8U) == 0xf0U) {
        width = 4;
    }
    index += std::min(width, text.size() - index);
    return true;
}

TextMetrics sanitize_metrics(TextMetrics metrics, int font_size, int font_weight) {
    const TextMetrics fallback = fallback_text_metrics({}, font_size, font_weight);
    metrics.width = std::max(0, metrics.width);
    metrics.line_height = metrics.line_height > 0 ? metrics.line_height : fallback.line_height;
    return metrics;
}

std::string_view trim_family_view(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
    }
    return value;
}

bool family_equals_ascii(std::string_view family, std::string_view expected) {
    family = trim_family_view(family);
    if (family.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < family.size(); ++index) {
        const char normalized = static_cast<char>(std::tolower(static_cast<unsigned char>(family[index])));
        if (normalized != expected[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::uint32_t normalized_font_family_hash(std::string_view family) {
    family = trim_family_view(family);
    if (family.empty() || is_generic_font_family(family)) {
        return 0;
    }

    std::uint32_t hash = kFontFamilyFnvOffset;
    bool pending_space = false;
    bool wrote = false;
    for (char ch : family) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte) != 0) {
            pending_space = wrote;
            continue;
        }
        if (pending_space) {
            hash ^= static_cast<std::uint8_t>(' ');
            hash *= kFontFamilyFnvPrime;
            pending_space = false;
        }
        const std::uint8_t normalized =
            byte < 0x80U ? static_cast<std::uint8_t>(std::tolower(byte)) : byte;
        hash ^= normalized;
        hash *= kFontFamilyFnvPrime;
        wrote = true;
    }
    return wrote ? hash : 0;
}

bool is_generic_font_family(std::string_view family) {
    return family_equals_ascii(family, "serif") ||
        family_equals_ascii(family, "sans-serif") ||
        family_equals_ascii(family, "monospace") ||
        family_equals_ascii(family, "cursive") ||
        family_equals_ascii(family, "fantasy") ||
        family_equals_ascii(family, "system-ui") ||
        family_equals_ascii(family, "ui-serif") ||
        family_equals_ascii(family, "ui-sans-serif") ||
        family_equals_ascii(family, "ui-monospace") ||
        family_equals_ascii(family, "emoji") ||
        family_equals_ascii(family, "math") ||
        family_equals_ascii(family, "fangsong");
}

TextMetrics fallback_text_metrics(const std::string& text, int font_size, int font_weight) {
    const int safe_font_size = std::max(1, font_size);
    const int ascii_advance = std::max(1, (safe_font_size * 2) / 3);
    const int bold_extra = font_weight >= 600 ? std::max(1, safe_font_size / 12) : 0;
    int width = 0;
    for (std::size_t index = 0; index < text.size();) {
        unsigned char lead = 0;
        if (!consume_utf8_codepoint(text, index, lead)) {
            break;
        }
        width += lead < 0x80U ? ascii_advance : safe_font_size;
    }
    return TextMetrics{
        width + (text.empty() ? 0 : std::max(6, safe_font_size / 2)) + bold_extra,
        safe_font_size + std::max(6, safe_font_size / 3),
    };
}

TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight) {
    return measure_text(provider, text, font_size, font_weight, 0);
}

TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight,
                         std::uint32_t font_family_hash) {
    if (font_family_hash != 0 && provider.measure_family != nullptr) {
        TextMetrics metrics;
        if (provider.measure_family(text, font_size, font_weight, font_family_hash, &metrics, provider.context)) {
            return sanitize_metrics(metrics, font_size, font_weight);
        }
    }
    if (provider.measure != nullptr) {
        TextMetrics metrics;
        if (provider.measure(text, font_size, font_weight, &metrics, provider.context)) {
            return sanitize_metrics(metrics, font_size, font_weight);
        }
    }
    return fallback_text_metrics(text, font_size, font_weight);
}

} // namespace jellyframe
