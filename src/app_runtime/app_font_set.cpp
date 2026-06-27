#include "app_runtime/app_font_set.h"

namespace jellyframe {
namespace {

bool app_font_set_measure_callback(const std::string& text,
                                   int font_size,
                                   int font_weight,
                                   TextMetrics* metrics,
                                   void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    if (fonts == nullptr || metrics == nullptr) {
        return false;
    }
    *metrics = fonts->measure_text(text, font_size, font_weight, 0);
    return true;
}

bool app_font_set_measure_family_callback(const std::string& text,
                                          int font_size,
                                          int font_weight,
                                          std::uint32_t font_family_hash,
                                          TextMetrics* metrics,
                                          void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    if (fonts == nullptr || metrics == nullptr || !fonts->has_family(font_family_hash)) {
        return false;
    }
    *metrics = fonts->measure_text(text, font_size, font_weight, font_family_hash);
    return true;
}

bool app_font_set_paint_callback(FrameBuffer& target,
                                 Rect rect,
                                 Color color,
                                 const std::string& text,
                                 int font_size,
                                 int font_weight,
                                 TextCommandAlign align,
                                 bool single_line,
                                 void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    return fonts != nullptr &&
        fonts->paint_text(target, rect, color, text, font_size, font_weight, 0, align, single_line);
}

bool app_font_set_paint_family_callback(FrameBuffer& target,
                                        Rect rect,
                                        Color color,
                                        const std::string& text,
                                        int font_size,
                                        int font_weight,
                                        std::uint32_t font_family_hash,
                                        TextCommandAlign align,
                                        bool single_line,
                                        void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    return fonts != nullptr &&
        fonts->has_family(font_family_hash) &&
        fonts->paint_text(target, rect, color, text, font_size, font_weight, font_family_hash, align, single_line);
}

void append_unique_font(std::vector<const BitmapFont*>& fonts, const BitmapFont* font) {
    if (font == nullptr) {
        return;
    }
    for (const BitmapFont* existing : fonts) {
        if (existing == font) {
            return;
        }
    }
    fonts.push_back(font);
}

} // namespace

AppFontSet::AppFontSet(std::size_t capacity)
    : capacity_(capacity) {
    fonts_.reserve(capacity_);
    fallback_fonts_.reserve(capacity_ + 1);
    family_fonts_.reserve(capacity_ + 1);
}

AppFontLoadResult AppFontSet::load_jffont(std::uint32_t app_instance_id,
                                          const std::uint8_t* data,
                                          std::size_t size) {
    return add_jffont(app_instance_id, data, size, true, {});
}

AppFontLoadResult AppFontSet::load_jffont(std::uint32_t app_instance_id,
                                          const std::uint8_t* data,
                                          std::size_t size,
                                          std::string_view family) {
    return add_jffont(app_instance_id, data, size, true, family);
}

AppFontLoadResult AppFontSet::attach_jffont_view(std::uint32_t app_instance_id,
                                                 const std::uint8_t* data,
                                                 std::size_t size) {
    return add_jffont(app_instance_id, data, size, false, {});
}

AppFontLoadResult AppFontSet::attach_jffont_view(std::uint32_t app_instance_id,
                                                 const std::uint8_t* data,
                                                 std::size_t size,
                                                 std::string_view family) {
    return add_jffont(app_instance_id, data, size, false, family);
}

AppFontLoadResult AppFontSet::add_jffont(std::uint32_t app_instance_id,
                                         const std::uint8_t* data,
                                         std::size_t size,
                                         bool copy_bytes,
                                         std::string_view family) {
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
    loaded.family_hash = normalized_font_family_hash(family);
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
    return TextMeasureProvider{app_font_set_measure_callback, this, app_font_set_measure_family_callback};
}

TextPainter AppFontSet::painter() {
    refresh_context();
    return TextPainter{app_font_set_paint_callback, this, app_font_set_paint_family_callback};
}

bool AppFontSet::has_family(std::uint32_t font_family_hash) const {
    if (font_family_hash == 0) {
        return true;
    }
    for (const LoadedFont& loaded : fonts_) {
        if (loaded.family_hash == font_family_hash && loaded.resource.valid()) {
            return true;
        }
    }
    return false;
}

TextMetrics AppFontSet::measure_text(const std::string& text,
                                     int font_size,
                                     int font_weight,
                                     std::uint32_t font_family_hash) {
    const BitmapFontFallbackContext* context = context_for_family(font_family_hash);
    if (context == nullptr) {
        return fallback_text_metrics(text, font_size, font_weight);
    }
    return measure_bitmap_text_with_fallback(*context, text, font_size, font_weight);
}

bool AppFontSet::paint_text(FrameBuffer& target,
                            Rect rect,
                            Color color,
                            const std::string& text,
                            int font_size,
                            int font_weight,
                            std::uint32_t font_family_hash,
                            TextCommandAlign align,
                            bool single_line) {
    const BitmapFontFallbackContext* context = context_for_family(font_family_hash);
    return context != nullptr &&
        bitmap_font_fallback_paint_callback(target,
                                            rect,
                                            color,
                                            text,
                                            font_size,
                                            font_weight,
                                            align,
                                            single_line,
                                            const_cast<BitmapFontFallbackContext*>(context));
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

const BitmapFontFallbackContext* AppFontSet::context_for_family(std::uint32_t font_family_hash) {
    refresh_context();
    if (font_family_hash == 0) {
        return fallback_context_.font_count == 0 ? nullptr : &fallback_context_;
    }

    family_fonts_.clear();
    for (const LoadedFont& loaded : fonts_) {
        if (loaded.family_hash == font_family_hash && loaded.resource.valid()) {
            append_unique_font(family_fonts_, &loaded.resource.font());
        }
    }
    if (family_fonts_.empty()) {
        return nullptr;
    }
    append_unique_font(family_fonts_, system_font_);
    for (const LoadedFont& loaded : fonts_) {
        if (loaded.resource.valid()) {
            append_unique_font(family_fonts_, &loaded.resource.font());
        }
    }
    family_context_.fonts = family_fonts_.data();
    family_context_.font_count = family_fonts_.size();
    family_context_.scale = 1;
    return &family_context_;
}

} // namespace jellyframe
