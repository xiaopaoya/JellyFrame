#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

using TextMeasureRangeCallback = bool (*)(const char* data,
                                          std::size_t length,
                                          int font_size,
                                          int font_weight,
                                          TextMetrics* metrics,
                                          void* context);

using TextMeasureRangeFamilyCallback = bool (*)(const char* data,
                                                std::size_t length,
                                                int font_size,
                                                int font_weight,
                                                std::uint32_t font_family_hash,
                                                TextMetrics* metrics,
                                                void* context);

using TextAdditiveMeasurementCallback = bool (*)(int font_size,
                                                 int font_weight,
                                                 std::uint32_t font_family_hash,
                                                 void* context);

struct TextMeasureProvider {
    TextMeasureCallback measure = nullptr;
    void* context = nullptr;
    TextMeasureFamilyCallback measure_family = nullptr;
    // Returns true only when concatenated scalar advances are equivalent to
    // measuring the complete string for these font arguments. Keep it null
    // for shaping/kerning-aware hosts.
    TextAdditiveMeasurementCallback additive_measurement_supported = nullptr;
    // Optional non-owning range callbacks. Providers can avoid allocating a
    // temporary std::string when measuring UTF-8 scalar ranges.
    TextMeasureRangeCallback measure_range = nullptr;
    TextMeasureRangeFamilyCallback measure_range_family = nullptr;
};

std::uint32_t normalized_font_family_hash(std::string_view family);
bool is_generic_font_family(std::string_view family);
TextMetrics fallback_text_metrics(const std::string& text, int font_size, int font_weight);
int bounded_letter_spacing(int font_size, int letter_spacing);
TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight);
TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight,
                         std::uint32_t font_family_hash);
TextMetrics measure_text_range(const TextMeasureProvider& provider,
                               const char* data,
                               std::size_t length,
                               int font_size,
                               int font_weight,
                               std::uint32_t font_family_hash = 0);
TextMetrics measure_text_with_letter_spacing(const TextMeasureProvider& provider,
                                             std::string_view text,
                                             int font_size,
                                             int font_weight,
                                             std::uint32_t font_family_hash,
                                             int letter_spacing);
std::vector<std::string> wrap_text_anywhere(const TextMeasureProvider& provider,
                                            std::string_view text,
                                            int font_size,
                                            int font_weight,
                                            std::uint32_t font_family_hash,
                                            int letter_spacing,
                                            int available_width);
std::size_t count_wrapped_lines_anywhere(const TextMeasureProvider& provider,
                                         std::string_view text,
                                         int font_size,
                                         int font_weight,
                                         std::uint32_t font_family_hash,
                                         int letter_spacing,
                                         int available_width);
std::vector<std::string> wrap_text_at_opportunities(const TextMeasureProvider& provider,
                                                    std::string_view text,
                                                    int font_size,
                                                    int font_weight,
                                                    std::uint32_t font_family_hash,
                                                    int letter_spacing,
                                                    int available_width);
std::size_t count_wrapped_lines_at_opportunities(const TextMeasureProvider& provider,
                                                 std::string_view text,
                                                 int font_size,
                                                 int font_weight,
                                                 std::uint32_t font_family_hash,
                                                 int letter_spacing,
                                                 int available_width);

} // namespace jellyframe
