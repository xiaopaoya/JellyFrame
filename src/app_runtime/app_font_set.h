#pragma once

#include "render_core/bitmap_font.h"
#include "render_core/bitmap_font_resource.h"
#include "render_core/text_backend.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

enum class AppFontLoadStatus {
    Loaded,
    EmptyInstance,
    CapacityExceeded,
    InvalidFont,
};

struct AppFontLoadResult {
    AppFontLoadStatus status = AppFontLoadStatus::InvalidFont;
    std::size_t font_count = 0;

    bool loaded() const {
        return status == AppFontLoadStatus::Loaded;
    }
};

class AppFontSet {
public:
    explicit AppFontSet(std::size_t capacity = 1);

    AppFontLoadResult load_jffont(std::uint32_t app_instance_id,
                                  const std::uint8_t* data,
                                  std::size_t size);
    AppFontLoadResult attach_jffont_view(std::uint32_t app_instance_id,
                                         const std::uint8_t* data,
                                         std::size_t size);
    std::size_t clear_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t size() const {
        return fonts_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return fonts_.empty();
    }

    void set_system_font(const BitmapFont* font);

    std::uint32_t app_instance_id() const {
        return app_instance_id_;
    }

    const BitmapFont* primary_font() const;
    TextMeasureProvider measure_provider();
    TextPainter painter();

private:
    struct LoadedFont {
        std::vector<std::uint8_t> bytes;
        BitmapFontResource resource;
    };

    AppFontLoadResult add_jffont(std::uint32_t app_instance_id,
                                 const std::uint8_t* data,
                                 std::size_t size,
                                 bool copy_bytes);
    void refresh_context();

    std::size_t capacity_ = 1;
    std::uint32_t app_instance_id_ = 0;
    const BitmapFont* system_font_ = nullptr;
    std::vector<LoadedFont> fonts_;
    BitmapFontContext primary_context_{};
    std::vector<const BitmapFont*> fallback_fonts_;
    BitmapFontFallbackContext fallback_context_{};
};

} // namespace jellyframe
