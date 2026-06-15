#include "core/text_backend.h"

#include <algorithm>
#include <cstddef>

namespace wearweb {
namespace {

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

} // namespace

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
    if (provider.measure != nullptr) {
        TextMetrics metrics;
        if (provider.measure(text, font_size, font_weight, &metrics, provider.context)) {
            return sanitize_metrics(metrics, font_size, font_weight);
        }
    }
    return fallback_text_metrics(text, font_size, font_weight);
}

} // namespace wearweb
