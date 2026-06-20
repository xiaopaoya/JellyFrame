#include "app_runtime/app_font_set.h"

namespace jellyframe {

AppFontSet::AppFontSet(std::size_t capacity)
    : capacity_(capacity) {
    fonts_.reserve(capacity_);
    fallback_fonts_.reserve(capacity_ + 1);
}

AppFontLoadResult AppFontSet::load_jffont(std::uint32_t app_instance_id,
                                          const std::uint8_t* data,
                                          std::size_t size) {
    return add_jffont(app_instance_id, data, size, true);
}

AppFontLoadResult AppFontSet::attach_jffont_view(std::uint32_t app_instance_id,
                                                 const std::uint8_t* data,
                                                 std::size_t size) {
    return add_jffont(app_instance_id, data, size, false);
}

AppFontLoadResult AppFontSet::add_jffont(std::uint32_t app_instance_id,
                                         const std::uint8_t* data,
                                         std::size_t size,
                                         bool copy_bytes) {
    if (app_instance_id == 0) {
        return AppFontLoadResult{AppFontLoadStatus::EmptyInstance, fonts_.size()};
    }
    if (app_instance_id_ != 0 && app_instance_id_ != app_instance_id) {
        clear();
    }
    if (fonts_.size() >= capacity_) {
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, fonts_.size()};
    }

    LoadedFont loaded;
    if (copy_bytes && data != nullptr && size > 0) {
        loaded.bytes.assign(data, data + size);
    }
    const std::uint8_t* view_data = copy_bytes ? loaded.bytes.data() : data;
    const std::size_t view_size = copy_bytes ? loaded.bytes.size() : size;
    if (!loaded.resource.load_jffont(view_data, view_size)) {
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size()};
    }
    app_instance_id_ = app_instance_id;
    fonts_.push_back(std::move(loaded));
    refresh_context();
    return AppFontLoadResult{AppFontLoadStatus::Loaded, fonts_.size()};
}

std::size_t AppFontSet::clear_app_instance(std::uint32_t app_instance_id) {
    if (app_instance_id == 0 || app_instance_id_ != app_instance_id) {
        return 0;
    }
    const std::size_t count = fonts_.size();
    clear();
    return count;
}

void AppFontSet::clear() {
    fonts_.clear();
    app_instance_id_ = 0;
    refresh_context();
}

void AppFontSet::set_system_font(const BitmapFont* font) {
    system_font_ = font;
    refresh_context();
}

const BitmapFont* AppFontSet::primary_font() const {
    if (system_font_ != nullptr) {
        return system_font_;
    }
    if (fonts_.empty() || !fonts_.front().resource.valid()) {
        return nullptr;
    }
    return &fonts_.front().resource.font();
}

TextMeasureProvider AppFontSet::measure_provider() {
    refresh_context();
    return TextMeasureProvider{bitmap_font_fallback_measure_callback, &fallback_context_};
}

TextPainter AppFontSet::painter() {
    refresh_context();
    return TextPainter{bitmap_font_fallback_paint_callback, &fallback_context_};
}

void AppFontSet::refresh_context() {
    fallback_fonts_.clear();
    if (system_font_ != nullptr) {
        fallback_fonts_.push_back(system_font_);
    }
    for (const LoadedFont& loaded : fonts_) {
        if (loaded.resource.valid()) {
            fallback_fonts_.push_back(&loaded.resource.font());
        }
    }
    primary_context_.font = primary_font();
    primary_context_.scale = 1;
    fallback_context_.fonts = fallback_fonts_.empty() ? nullptr : fallback_fonts_.data();
    fallback_context_.font_count = fallback_fonts_.size();
    fallback_context_.scale = 1;
}

} // namespace jellyframe
