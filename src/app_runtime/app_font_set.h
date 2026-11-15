#pragma once

#include "render_core/font_resource.h"
#include "render_core/text_backend.h"
#include "render_core/text_system.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

struct CoreFontResourceView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

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
    AppFontSet(const AppFontSet&) = delete;
    AppFontSet& operator=(const AppFontSet&) = delete;
    AppFontSet(AppFontSet&&) = delete;
    AppFontSet& operator=(AppFontSet&&) = delete;

    AppFontLoadResult load_core_font(std::uint32_t app_instance_id,
                                     const std::uint8_t* data,
                                     std::size_t size,
                                     std::string_view family = {});
    AppFontLoadResult attach_core_font_view(std::uint32_t app_instance_id,
                                            const std::uint8_t* data,
                                            std::size_t size,
                                            std::string_view family = {});
    bool set_core_system_font(const std::uint8_t* data, std::size_t size);
    bool attach_core_system_font_view(const std::uint8_t* data, std::size_t size);
    bool set_core_system_font_catalog(const CoreFontResourceView* resources, std::size_t count);
    bool attach_core_system_font_catalog_view(const CoreFontResourceView* resources, std::size_t count);
    const TextSystem* core_text_system() const;
    std::size_t clear_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t size() const {
        return core_fonts_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return core_fonts_.empty();
    }

    std::uint32_t app_instance_id() const {
        return app_instance_id_;
    }

private:
    struct CoreFont {
        std::uint32_t family_hash = 0;
        std::uint64_t resource_id = 0;
        std::vector<std::uint8_t> bytes;
        FontResource resource;
    };

    struct CoreSystemFont {
        std::uint64_t resource_id = 0;
        std::vector<std::uint8_t> bytes;
        FontResource resource;
    };

    AppFontLoadResult add_core_font(std::uint32_t app_instance_id,
                                    const std::uint8_t* data,
                                    std::size_t size,
                                    bool copy_bytes,
                                    std::string_view family);
    bool bind_core_system_font_catalog(const CoreFontResourceView* resources,
                                       std::size_t count,
                                       bool copy_bytes);
    bool rebuild_core_text_system();

    std::size_t capacity_ = 1;
    std::uint32_t app_instance_id_ = 0;
    std::vector<CoreSystemFont> core_system_fonts_;
    std::vector<CoreFont> core_fonts_;
    std::vector<TextFontFace> core_faces_;
    TextSystem core_text_system_;
    std::uint64_t next_core_resource_id_ = 1;
    bool core_text_system_ready_ = false;
};

} // namespace jellyframe
