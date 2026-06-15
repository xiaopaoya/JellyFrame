#pragma once

#include <string>

namespace wearweb {

struct TextMetrics {
    int width = 0;
    int line_height = 0;
};

using TextMeasureCallback = bool (*)(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     TextMetrics* metrics,
                                     void* context);

struct TextMeasureProvider {
    TextMeasureCallback measure = nullptr;
    void* context = nullptr;
};

TextMetrics fallback_text_metrics(const std::string& text, int font_size, int font_weight);
TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight);

} // namespace wearweb
