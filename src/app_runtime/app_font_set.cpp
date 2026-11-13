#include "app_runtime/app_font_set.h"

#include <algorithm>
#include <unordered_map>

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

bool app_font_set_measure_range_callback(const char* data,
                                         std::size_t length,
                                         int font_size,
                                         int font_weight,
                                         TextMetrics* metrics,
                                         void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    if (fonts == nullptr || metrics == nullptr || (data == nullptr && length != 0)) {
        return false;
    }
    *metrics = fonts->measure_text(std::string_view(data != nullptr ? data : "", length),
                                   font_size,
                                   font_weight,
                                   0);
    return true;
}

bool app_font_set_measure_range_family_callback(const char* data,
                                                std::size_t length,
                                                int font_size,
                                                int font_weight,
                                                std::uint32_t font_family_hash,
                                                TextMetrics* metrics,
                                                void* context) {
    auto* fonts = static_cast<AppFontSet*>(context);
    if (fonts == nullptr || metrics == nullptr || (data == nullptr && length != 0) ||
        !fonts->has_family(font_family_hash)) {
        return false;
    }
    *metrics = fonts->measure_text(std::string_view(data != nullptr ? data : "", length),
                                   font_size,
                                   font_weight,
                                   font_family_hash);
    return true;
}

bool app_font_set_additive_measurement_supported(int,
                                                 int font_weight,
                                                 std::uint32_t font_family_hash,
                                                 void* context) {
    const auto* fonts = static_cast<const AppFontSet*>(context);
    return fonts != nullptr && font_weight < 600 &&
        (font_family_hash == 0 || fonts->has_family(font_family_hash));
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

int app_font_scale_for_size(const BitmapFontFallbackContext& context, int font_size) {
    const int fixed_scale = std::max(1, context.scale);
    if (context.fonts == nullptr || context.font_count == 0 || context.fonts[0] == nullptr) {
        return fixed_scale;
    }
    const int line_height = std::max(1, static_cast<int>(context.fonts[0]->line_height));
    const int requested = std::max(1, (std::max(1, font_size) + line_height / 2) / line_height);
    return std::max(fixed_scale, std::min(requested, 8));
}

BitmapFontFallbackContext app_font_context_for_size(const BitmapFontFallbackContext& context, int font_size) {
    BitmapFontFallbackContext scaled = context;
    scaled.scale = app_font_scale_for_size(context, font_size);
    return scaled;
}

} // namespace

AppFontSet::AppFontSet(std::size_t capacity)
    : capacity_(capacity) {
    fonts_.reserve(capacity_);
    fallback_fonts_.reserve(capacity_ + 1);
    family_fonts_.reserve(capacity_ + 1);
    core_fonts_.reserve(capacity_);
    core_faces_.reserve(capacity_ + 1);
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

AppFontLoadResult AppFontSet::load_core_font(std::uint32_t app_instance_id,
                                             const std::uint8_t* data,
                                             std::size_t size,
                                             std::string_view family) {
    return add_core_font(app_instance_id, data, size, true, family);
}

AppFontLoadResult AppFontSet::attach_core_font_view(std::uint32_t app_instance_id,
                                                    const std::uint8_t* data,
                                                    std::size_t size,
                                                    std::string_view family) {
    return add_core_font(app_instance_id, data, size, false, family);
}

bool AppFontSet::set_core_system_font(const std::uint8_t* data, std::size_t size) {
    const CoreFontResourceView resource{data, size};
    return set_core_system_font_catalog(&resource, 1);
}

bool AppFontSet::attach_core_system_font_view(const std::uint8_t* data, std::size_t size) {
    const CoreFontResourceView resource{data, size};
    return attach_core_system_font_catalog_view(&resource, 1);
}

bool AppFontSet::set_core_system_font_catalog(const CoreFontResourceView* resources, std::size_t count) {
    return bind_core_system_font_catalog(resources, count, true);
}

bool AppFontSet::attach_core_system_font_catalog_view(const CoreFontResourceView* resources,
                                                      std::size_t count) {
    return bind_core_system_font_catalog(resources, count, false);
}

bool AppFontSet::bind_core_system_font_catalog(const CoreFontResourceView* resources,
                                              std::size_t count,
                                              bool copy_bytes) {
    if (resources == nullptr || count == 0 || count > 64 ||
        count > UINT64_MAX - next_core_resource_id_) return false;
    std::vector<CoreSystemFont> candidate;
    try {
        candidate.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (resources[i].data == nullptr || resources[i].size == 0) return false;
            CoreSystemFont face;
            if (copy_bytes) face.bytes.assign(resources[i].data, resources[i].data + resources[i].size);
            const auto* data = copy_bytes ? face.bytes.data() : resources[i].data;
            const auto size = copy_bytes ? face.bytes.size() : resources[i].size;
            if (face.resource.load(data, size) != FontResourceStatus::Loaded) return false;
            face.resource_id = next_core_resource_id_ + i;
            candidate.push_back(std::move(face));
        }
    } catch (...) {
        return false;
    }
    auto old = std::move(core_system_fonts_);
    core_system_fonts_ = std::move(candidate);
    if (!rebuild_core_text_system()) {
        core_system_fonts_ = std::move(old);
        rebuild_core_text_system();
        return false;
    }
    next_core_resource_id_ += count;
    return true;
}

const TextSystem* AppFontSet::core_text_system() const {
    return core_text_system_ready_ ? &core_text_system_ : nullptr;
}

AppFontLoadResult AppFontSet::add_core_font(std::uint32_t app_instance_id,
                                           const std::uint8_t* data,
                                           std::size_t size,
                                           bool copy_bytes,
                                           std::string_view family) {
    if (app_instance_id == 0)
        return AppFontLoadResult{AppFontLoadStatus::EmptyInstance, fonts_.size() + core_fonts_.size()};
    if (data == nullptr || size == 0)
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size() + core_fonts_.size()};
    if (app_instance_id_ != 0 && app_instance_id_ != app_instance_id) clear();
    if (fonts_.size() + core_fonts_.size() >= capacity_)
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, fonts_.size() + core_fonts_.size()};

    CoreFont loaded;
    loaded.family_hash = normalized_font_family_hash(family);
    try {
        if (copy_bytes) loaded.bytes.assign(data, data + size);
    } catch (...) {
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size() + core_fonts_.size()};
    }
    const std::uint8_t* resource_bytes = copy_bytes ? loaded.bytes.data() : data;
    const std::size_t resource_size = copy_bytes ? loaded.bytes.size() : size;
    if (loaded.resource.load(resource_bytes, resource_size) != FontResourceStatus::Loaded)
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size() + core_fonts_.size()};
    try {
        core_fonts_.push_back(std::move(loaded));
    } catch (...) {
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, fonts_.size() + core_fonts_.size()};
    }
    if (!rebuild_core_text_system()) {
        core_fonts_.pop_back();
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, fonts_.size() + core_fonts_.size()};
    }
    core_fonts_.back().resource_id = next_core_resource_id_++;
    app_instance_id_ = app_instance_id;
    return AppFontLoadResult{AppFontLoadStatus::Loaded, fonts_.size() + core_fonts_.size()};
}

bool AppFontSet::rebuild_core_text_system() {
    std::vector<TextFontFace> candidate_faces;
    std::unordered_map<std::uint32_t, std::uint32_t> family_ids;
    try {
        candidate_faces.reserve(core_fonts_.size() + core_system_fonts_.size());
        family_ids.reserve(core_fonts_.size());
        for (const auto& loaded : core_system_fonts_)
            candidate_faces.push_back({loaded.resource_id, 0, loaded.resource, 0});
        std::uint32_t next_family_id = 1;
        for (const auto& loaded : core_fonts_) {
            std::uint32_t family_id = 0;
            if (loaded.family_hash != 0) {
                auto [it, inserted] = family_ids.emplace(loaded.family_hash, next_family_id);
                if (inserted) ++next_family_id;
                family_id = it->second;
            }
            const std::uint64_t resource_id = loaded.resource_id != 0
                ? loaded.resource_id : next_core_resource_id_;
            candidate_faces.push_back({resource_id, family_id, loaded.resource, loaded.family_hash});
        }
    } catch (...) {
        return false;
    }
    if (candidate_faces.empty()) {
        core_faces_.clear();
        core_text_system_.clear();
        core_text_system_ready_ = false;
        return true;
    }
    if (core_text_system_.bind(candidate_faces.data(), candidate_faces.size()) != TextSystemStatus::Ok)
        return false;
    core_faces_.swap(candidate_faces);
    core_text_system_ready_ = true;
    return true;
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
    if (fonts_.size() + core_fonts_.size() >= capacity_) {
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, fonts_.size() + core_fonts_.size()};
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
    contexts_dirty_ = true;
    refresh_context();
    return AppFontLoadResult{AppFontLoadStatus::Loaded, fonts_.size()};
}

std::size_t AppFontSet::clear_app_instance(std::uint32_t app_instance_id) {
    if (app_instance_id == 0 || app_instance_id_ != app_instance_id) {
        return 0;
    }
    const std::size_t count = fonts_.size() + core_fonts_.size();
    fonts_.clear();
    core_fonts_.clear();
    app_instance_id_ = 0;
    contexts_dirty_ = true;
    refresh_context();
    rebuild_core_text_system();
    return count;
}

void AppFontSet::clear() {
    fonts_.clear();
    core_fonts_.clear();
    app_instance_id_ = 0;
    contexts_dirty_ = true;
    refresh_context();
    rebuild_core_text_system();
}

void AppFontSet::set_system_font(const BitmapFont* font) {
    system_font_ = font;
    contexts_dirty_ = true;
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
    return TextMeasureProvider{app_font_set_measure_callback,
                               this,
                               app_font_set_measure_family_callback,
                               app_font_set_additive_measurement_supported,
                               app_font_set_measure_range_callback,
                               app_font_set_measure_range_family_callback};
}

TextPainter AppFontSet::painter() {
    refresh_context();
    // AppFontSet delegates to the Core bitmap backend, which clips glyphs,
    // fallback boxes and synthetic bold strokes to the supplied text rect.
    return TextPainter{app_font_set_paint_callback, this, app_font_set_paint_family_callback, true};
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
    return measure_text(std::string_view(text), font_size, font_weight, font_family_hash);
}

TextMetrics AppFontSet::measure_text(std::string_view text,
                                     int font_size,
                                     int font_weight,
                                     std::uint32_t font_family_hash) {
    const BitmapFontFallbackContext* context = context_for_family(font_family_hash);
    if (context == nullptr) {
        return fallback_text_metrics(std::string(text), font_size, font_weight);
    }
    const BitmapFontFallbackContext scaled = app_font_context_for_size(*context, font_size);
    // Keep the runtime link-compatible with the published 0.6.2 Core package.
    // The range helper is available in the source tree but was not exported by
    // that package; the established string API has identical metrics.
    return measure_bitmap_text_with_fallback(scaled, std::string(text), font_size, font_weight);
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
    if (context == nullptr) {
        return false;
    }
    const BitmapFontFallbackContext scaled = app_font_context_for_size(*context, font_size);
    return bitmap_font_fallback_paint_callback(target,
                                               rect,
                                               color,
                                               text,
                                               font_size,
                                               font_weight,
                                               align,
                                               single_line,
                                               const_cast<BitmapFontFallbackContext*>(&scaled));
}

void AppFontSet::refresh_context() {
    if (!contexts_dirty_) {
        return;
    }
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
    contexts_dirty_ = false;
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
