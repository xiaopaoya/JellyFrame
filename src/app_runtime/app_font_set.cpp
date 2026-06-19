#include "app_runtime/app_font_set.h"

namespace jellyframe {

AppFontSet::AppFontSet(std::size_t capacity)
    : capacity_(capacity) {
    fonts_.reserve(capacity_);
}

AppFontLoadResult AppFontSet::load_jffont(std::uint32_t app_instance_id,
                                          const std::uint8_t* data,
                                          std::size_t size) {
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
    if (data != nullptr && size > 0) {
        loaded.bytes.assign(data, data + size);
    }
    if (!loaded.resource.load_jffont(loaded.bytes.data(), loaded.bytes.size())) {
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size()};
    }
    app_instance_id_ = app_instance_id;
    fonts_.push_back(std::move(loaded));
    primary_context_.font = primary_font();
    primary_context_.scale = 1;
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
    primary_context_ = BitmapFontContext{};
}

const BitmapFont* AppFontSet::primary_font() const {
    if (fonts_.empty() || !fonts_.front().resource.valid()) {
        return nullptr;
    }
    return &fonts_.front().resource.font();
}

TextMeasureProvider AppFontSet::measure_provider() {
    primary_context_.font = primary_font();
    return TextMeasureProvider{bitmap_font_measure_callback, &primary_context_};
}

TextPainter AppFontSet::painter() {
    primary_context_.font = primary_font();
    return TextPainter{bitmap_font_paint_callback, &primary_context_};
}

} // namespace jellyframe
