#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace jellyframe {

struct TextMetrics {
    int width = 0;
    int line_height = 0;
};

using TextMeasureCallback = bool (*)(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     TextMetrics* metrics,
                                     void* context);

using TextMeasureFamilyCallback = bool (*)(const std::string& text,
                                           int font_size,
                                           int font_weight,
                                           std::uint32_t font_family_hash,
                                           TextMetrics* metrics,
                                           void* context);

struct TextMeasureProvider {
    TextMeasureCallback measure = nullptr;
    void* context = nullptr;
    TextMeasureFamilyCallback measure_family = nullptr;
};

std::uint32_t normalized_font_family_hash(std::string_view family);
bool is_generic_font_family(std::string_view family);
TextMetrics fallback_text_metrics(const std::string& text, int font_size, int font_weight);
TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight);
TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight,
                         std::uint32_t font_family_hash);

} // namespace jellyframe
