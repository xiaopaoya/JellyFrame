#pragma once

#include "jellyframe_esp32s3_resources.h"

#include "render_core/geometry.h"
#include "render_core/layer_tree.h"
#include "render_core/software_renderer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe_esp32s3 {

struct ImageAdapterStats {
    std::uint32_t requests = 0;
    std::uint32_t cache_hits = 0;
    std::uint32_t decoded = 0;
    std::uint32_t missing = 0;
    std::uint32_t corrupt = 0;
    std::uint32_t oversized = 0;
    std::uint32_t unsupported = 0;
    std::uint32_t budget_rejected = 0;
    std::uint32_t paint_calls = 0;
    std::uint32_t paint_failures = 0;
    std::size_t decoded_bytes = 0;
    std::uint64_t decode_us = 0;
};

class BmpImageAdapter final {
public:
    using Clock = std::int64_t (*)();

    void configure(const jellyframe::HostBudgets& budgets,
                   std::string_view base_url,
                   const ResourceBundle* bundle,
                   Clock clock);

    void set_background_resource_resolver(const jellyframe::StyleResolver* resolver) {
        background_resource_resolver_ = resolver;
    }

    bool resolve(const jellyframe::Node& node,
                 jellyframe::ImageResolveKind kind,
                 std::uint16_t background_resource_id,
                 jellyframe::ResolvedImageResource& result);

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
    jellyframe::ImageIntrinsicSize natural_size(const jellyframe::Node& node);
#endif

    bool paint(jellyframe::FrameBuffer& target,
               const jellyframe::ImagePaintRequest& request);

    const ImageAdapterStats& stats() const { return stats_; }

private:
    enum class CacheStatus : std::uint8_t {
        MetadataOnly,
        Ready,
        Missing,
        Corrupt,
        Oversized,
        Unsupported,
        BudgetRejected,
    };

    struct Entry {
        std::string url;
        CacheStatus status = CacheStatus::Corrupt;
        std::uint32_t handle = 0;
        int width = 0;
        int height = 0;
        std::vector<std::uint16_t> pixels;
        std::vector<std::uint8_t> alpha;
    };

    ResourceLoadStats resource_stats_{};
    ResourceBundleContext resource_context_{};
    Clock clock_ = nullptr;
    const jellyframe::StyleResolver* background_resource_resolver_ = nullptr;
    ImageAdapterStats stats_{};
    std::vector<Entry> entries_;

    Entry* find(std::string_view url);
    const Entry* find(std::uint32_t handle) const;
    bool decode_cached(Entry& entry);
    CacheStatus decode(std::string_view url, std::string_view bytes, Entry& entry);
};

jellyframe::ImageHandleResolver make_bmp_image_resolver(BmpImageAdapter& adapter);
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
jellyframe::NaturalImageSizeProvider make_bmp_natural_image_size_provider(BmpImageAdapter& adapter);
#endif
jellyframe::ImagePainter make_bmp_image_painter(BmpImageAdapter& adapter);

} // namespace jellyframe_esp32s3
