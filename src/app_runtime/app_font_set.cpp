#include "app_runtime/app_font_set.h"

#include <algorithm>
#include <unordered_map>

namespace jellyframe {
AppFontSet::AppFontSet(std::size_t capacity)
    : capacity_(capacity) {
    core_fonts_.reserve(capacity_);
    core_faces_.reserve(capacity_ + 1);
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
        return AppFontLoadResult{AppFontLoadStatus::EmptyInstance, core_fonts_.size()};
    if (data == nullptr || size == 0)
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, core_fonts_.size()};
    const bool replace_instance = app_instance_id_ != 0 && app_instance_id_ != app_instance_id;
    if ((!replace_instance && core_fonts_.size() >= capacity_) || capacity_ == 0)
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, core_fonts_.size()};

    CoreFont loaded;
    loaded.family_hash = normalized_font_family_hash(family);
    try {
        if (copy_bytes) loaded.bytes.assign(data, data + size);
    } catch (...) {
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, core_fonts_.size()};
    }
    const std::uint8_t* resource_bytes = copy_bytes ? loaded.bytes.data() : data;
    const std::size_t resource_size = copy_bytes ? loaded.bytes.size() : size;
    if (loaded.resource.load(resource_bytes, resource_size) != FontResourceStatus::Loaded)
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, core_fonts_.size()};
    if (next_core_resource_id_ == UINT64_MAX)
        return {AppFontLoadStatus::CapacityExceeded, core_fonts_.size()};
    auto old_fonts = replace_instance ? std::move(core_fonts_) : std::vector<CoreFont>{};
    try {
        core_fonts_.push_back(std::move(loaded));
    } catch (...) {
        if (replace_instance) core_fonts_ = std::move(old_fonts);
        return AppFontLoadResult{AppFontLoadStatus::CapacityExceeded, core_fonts_.size()};
    }
    if (!rebuild_core_text_system()) {
        core_fonts_.pop_back();
        if (replace_instance) core_fonts_ = std::move(old_fonts);
        return AppFontLoadResult{AppFontLoadStatus::InvalidFont, core_fonts_.size()};
    }
    core_fonts_.back().resource_id = next_core_resource_id_++;
    app_instance_id_ = app_instance_id;
    return AppFontLoadResult{AppFontLoadStatus::Loaded, core_fonts_.size()};
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

std::size_t AppFontSet::clear_app_instance(std::uint32_t app_instance_id) {
    if (app_instance_id == 0 || app_instance_id_ != app_instance_id) {
        return 0;
    }
    const std::size_t count = core_fonts_.size();
    core_text_system_.clear();
    core_text_system_ready_ = false;
    core_faces_.clear();
    core_fonts_.clear();
    app_instance_id_ = 0;
    rebuild_core_text_system();
    return count;
}

void AppFontSet::clear() {
    core_text_system_.clear();
    core_text_system_ready_ = false;
    core_faces_.clear();
    core_fonts_.clear();
    app_instance_id_ = 0;
    rebuild_core_text_system();
}

} // namespace jellyframe
