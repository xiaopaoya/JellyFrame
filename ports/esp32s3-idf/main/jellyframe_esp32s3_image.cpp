#include "jellyframe_esp32s3_image.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace jellyframe_esp32s3 {
namespace {

constexpr int kMaxDimension = 96;
constexpr std::size_t kMaxDecodedBytes = 32 * 1024;
constexpr std::size_t kMaxCacheEntries = 16;
constexpr std::size_t kMaxUrlBytes = 256;

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(data[1] << 8U);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::int32_t read_i32(const std::uint8_t* data) {
    return static_cast<std::int32_t>(read_u32(data));
}

std::uint16_t pack_rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3U) << 11U) |
                                      ((g >> 2U) << 5U) |
                                      (b >> 3U));
}

jellyframe::Color unpack_rgb565(std::uint16_t value, std::uint8_t alpha) {
    const std::uint8_t r = static_cast<std::uint8_t>(((value >> 11U) & 0x1fU) * 255U / 31U);
    const std::uint8_t g = static_cast<std::uint8_t>(((value >> 5U) & 0x3fU) * 255U / 63U);
    const std::uint8_t b = static_cast<std::uint8_t>((value & 0x1fU) * 255U / 31U);
    return jellyframe::Color{r, g, b, alpha};
}

bool is_bmp_url(std::string_view url) {
    const std::size_t dot = url.rfind('.');
    return dot != std::string_view::npos && url.substr(dot) == ".bmp";
}

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
bool inspect_bmp_intrinsic_size(std::string_view bytes,
                                jellyframe::ImageIntrinsicSize& size) {
    size = {};
    if (bytes.size() < 54) return false;
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    if (data[0] != 'B' || data[1] != 'M') return false;
    const std::uint32_t pixel_offset = read_u32(data + 10);
    const std::uint32_t dib_size = read_u32(data + 14);
    if (dib_size < 40 || dib_size > bytes.size() - 14) return false;

    const std::int32_t width = read_i32(data + 18);
    const std::int32_t signed_height = read_i32(data + 22);
    const std::int64_t height = signed_height < 0
        ? -static_cast<std::int64_t>(signed_height)
        : static_cast<std::int64_t>(signed_height);
    if (width <= 0 || height == 0) {
        size = {jellyframe::ImageIntrinsicSizeState::Invalid, 0, 0};
        return true;
    }
    const std::uint16_t planes = read_u16(data + 26);
    const std::uint16_t bits = read_u16(data + 28);
    const std::uint32_t compression = read_u32(data + 30);
    if (planes != 1 || compression != 0 || (bits != 24 && bits != 32)) return false;

    const std::size_t row_bits = static_cast<std::size_t>(width) * bits;
    const std::size_t row_bytes = ((row_bits + 31U) / 32U) * 4U;
    const std::size_t pixel_height = static_cast<std::size_t>(height);
    if (pixel_offset < 14U + dib_size || pixel_offset > bytes.size() ||
        row_bytes > std::numeric_limits<std::size_t>::max() / pixel_height ||
        row_bytes * pixel_height > bytes.size() - pixel_offset) return false;

    if (width > kMaxDimension || height > kMaxDimension ||
        static_cast<std::size_t>(width) * pixel_height > kMaxDecodedBytes / 3U) return false;

    size = {jellyframe::ImageIntrinsicSizeState::Known,
            static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(height)};
    return true;
}
#endif

void blend_pixel(jellyframe::Color& destination, jellyframe::Color source) {
    if (source.a == 255) {
        destination = source;
        return;
    }
    if (source.a == 0) {
        return;
    }
    const unsigned alpha = source.a;
    const unsigned inverse = 255U - alpha;
    destination.r = static_cast<std::uint8_t>((source.r * alpha + destination.r * inverse + 127U) / 255U);
    destination.g = static_cast<std::uint8_t>((source.g * alpha + destination.g * inverse + 127U) / 255U);
    destination.b = static_cast<std::uint8_t>((source.b * alpha + destination.b * inverse + 127U) / 255U);
    destination.a = static_cast<std::uint8_t>(std::min(255U, alpha + destination.a * inverse / 255U));
}

} // namespace

void BmpImageAdapter::configure(const jellyframe::HostBudgets& budgets,
                                std::string_view base_url,
                                const ResourceBundle* bundle,
                                Clock clock) {
    resource_stats_ = {};
    stats_ = {};
    entries_.clear();
    clock_ = clock;
    background_resource_resolver_ = nullptr;
    resource_context_ = bundle != nullptr
        ? make_resource_context(budgets, base_url, *bundle, &resource_stats_)
        : make_resource_context(budgets, base_url, &resource_stats_);
}

BmpImageAdapter::Entry* BmpImageAdapter::find(std::string_view url) {
    for (Entry& entry : entries_) {
        if (entry.url == url) {
            return &entry;
        }
    }
    return nullptr;
}

const BmpImageAdapter::Entry* BmpImageAdapter::find(std::uint32_t handle) const {
    if (handle == 0 || handle > entries_.size()) {
        return nullptr;
    }
    const Entry& entry = entries_[handle - 1];
    return entry.handle == handle ? &entry : nullptr;
}

BmpImageAdapter::CacheStatus BmpImageAdapter::decode(std::string_view url,
                                                      std::string_view bytes,
                                                      Entry& entry) {
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    if (bytes.size() < 54 || data[0] != 'B' || data[1] != 'M') {
        return CacheStatus::Corrupt;
    }
    const std::uint32_t pixel_offset = read_u32(data + 10);
    const std::uint32_t dib_size = read_u32(data + 14);
    if (dib_size < 40 || dib_size > bytes.size() - 14) {
        return CacheStatus::Corrupt;
    }
    const std::int32_t width = read_i32(data + 18);
    const std::int32_t signed_height = read_i32(data + 22);
    const std::uint16_t planes = read_u16(data + 26);
    const std::uint16_t bits = read_u16(data + 28);
    const std::uint32_t compression = read_u32(data + 30);
    if (width <= 0 || signed_height == 0 || planes != 1 || compression != 0 ||
        (bits != 24 && bits != 32)) {
        return bits != 24 && bits != 32 ? CacheStatus::Unsupported : CacheStatus::Corrupt;
    }
    const std::int64_t height64 = signed_height < 0
        ? -static_cast<std::int64_t>(signed_height)
        : static_cast<std::int64_t>(signed_height);
    if (width > kMaxDimension || height64 > kMaxDimension) {
        return CacheStatus::Oversized;
    }
    const std::size_t bytes_per_pixel = bits / 8U;
    const std::size_t row_bits = static_cast<std::size_t>(width) * bits;
    const std::size_t row_bytes = ((row_bits + 31U) / 32U) * 4U;
    const std::size_t height = static_cast<std::size_t>(height64);
    if (pixel_offset < 14U + dib_size || pixel_offset > bytes.size() ||
        row_bytes > std::numeric_limits<std::size_t>::max() / height ||
        row_bytes * height > bytes.size() - pixel_offset) {
        return CacheStatus::Corrupt;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > kMaxDecodedBytes / 3U || stats_.decoded_bytes > kMaxDecodedBytes - pixel_count * 3U) {
        return CacheStatus::Oversized;
    }

    try {
        entry.width = width;
        entry.height = static_cast<int>(height);
        entry.pixels.resize(pixel_count);
        entry.alpha.resize(pixel_count, 255);
    } catch (const std::bad_alloc&) {
        entry.pixels.clear();
        entry.alpha.clear();
        return CacheStatus::Oversized;
    }
    for (int y = 0; y < entry.height; ++y) {
        const int source_y = signed_height < 0 ? y : entry.height - 1 - y;
        const std::uint8_t* row = data + pixel_offset + static_cast<std::size_t>(source_y) * row_bytes;
        for (int x = 0; x < entry.width; ++x) {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * bytes_per_pixel;
            const std::size_t index = static_cast<std::size_t>(y) * entry.width + x;
            entry.pixels[index] = pack_rgb565(pixel[2], pixel[1], pixel[0]);
            entry.alpha[index] = bits == 32 ? pixel[3] : 255;
        }
    }
    stats_.decoded_bytes += pixel_count * 3U;
    (void)url;
    return CacheStatus::Ready;
}

bool BmpImageAdapter::decode_cached(Entry& entry) {
#if !defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
    (void)entry;
    return false;
#else
    if (entry.status != CacheStatus::MetadataOnly) return entry.status == CacheStatus::Ready;
    std::string bytes;
    const std::uint32_t missing_before = resource_stats_.missing_loads;
    const std::uint32_t rejected_before = resource_stats_.rejected_loads;
    if (!load_resource(jellyframe::HostResourceRequest{
            jellyframe::HostResourceKind::Image, entry.url, resource_context_.base_url},
            bytes, &resource_context_)) {
        entry.status = rejected_before != resource_stats_.rejected_loads
            ? CacheStatus::Oversized
            : (missing_before != resource_stats_.missing_loads ? CacheStatus::Missing : CacheStatus::Corrupt);
        return false;
    }
    const std::int64_t start = clock_ ? clock_() : 0;
    entry.status = decode(entry.url, bytes, entry);
    if (clock_) stats_.decode_us += static_cast<std::uint64_t>(clock_() - start);
    return entry.status == CacheStatus::Ready;
#endif
}

bool BmpImageAdapter::resolve(const jellyframe::Node& node,
                              jellyframe::ImageResolveKind kind,
                              std::uint16_t background_resource_id,
                              jellyframe::ResolvedImageResource& result) {
    result = {};
    std::string_view url;
    if (kind == jellyframe::ImageResolveKind::Content) {
        url = node.attribute("src");
    } else if (background_resource_resolver_ != nullptr) {
        const std::string* resolved = background_resource_resolver_->background_image_resource_url(
            background_resource_id);
        if (resolved != nullptr) url = *resolved;
    }
    if (url.empty()) {
        return false;
    }
    std::string canonical_url;
    if (url.size() > kMaxUrlBytes ||
        !resolve_local_resource_url(url, resource_context_.base_url, canonical_url) ||
        canonical_url.size() > kMaxUrlBytes) {
        ++stats_.budget_rejected;
        return false;
    }
    ++stats_.requests;
    if (Entry* cached = find(canonical_url)) {
        ++stats_.cache_hits;
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
        if (cached->status == CacheStatus::MetadataOnly) {
            decode_cached(*cached);
            switch (cached->status) {
            case CacheStatus::Ready: ++stats_.decoded; break;
            case CacheStatus::Missing: ++stats_.missing; break;
            case CacheStatus::Corrupt: ++stats_.corrupt; break;
            case CacheStatus::Oversized: ++stats_.oversized; break;
            case CacheStatus::Unsupported: ++stats_.unsupported; break;
            case CacheStatus::BudgetRejected: ++stats_.budget_rejected; break;
            case CacheStatus::MetadataOnly: break;
            }
        }
#else
        if (cached->status == CacheStatus::MetadataOnly) return false;
#endif
        if (cached->status == CacheStatus::Ready)
            result = {cached->handle, {jellyframe::ImageIntrinsicSizeState::Known,
                                       static_cast<std::uint16_t>(cached->width),
                                       static_cast<std::uint16_t>(cached->height)}};
        return cached->status == CacheStatus::Ready;
    }
    if (entries_.size() >= kMaxCacheEntries) {
        ++stats_.budget_rejected;
        return false;
    }
    Entry entry;
    entry.url = canonical_url;
    entry.handle = static_cast<std::uint32_t>(entries_.size() + 1);
    if (!is_bmp_url(canonical_url)) {
        entry.status = CacheStatus::Unsupported;
    } else {
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
        entry.status = CacheStatus::MetadataOnly;
        decode_cached(entry);
#else
        std::string bytes;
        const std::uint32_t missing_before = resource_stats_.missing_loads;
        const std::uint32_t rejected_before = resource_stats_.rejected_loads;
        if (!load_resource(jellyframe::HostResourceRequest{
                jellyframe::HostResourceKind::Image, canonical_url, resource_context_.base_url}, bytes, &resource_context_)) {
            entry.status = rejected_before != resource_stats_.rejected_loads
                ? CacheStatus::Oversized
                : (missing_before != resource_stats_.missing_loads ? CacheStatus::Missing : CacheStatus::Corrupt);
        } else {
            const std::int64_t start = clock_ ? clock_() : 0;
            entry.status = decode(canonical_url, bytes, entry);
            if (clock_) stats_.decode_us += static_cast<std::uint64_t>(clock_() - start);
        }
#endif
    }
    const std::uint32_t handle = entry.handle;
    switch (entry.status) {
    case CacheStatus::Ready: ++stats_.decoded; break;
    case CacheStatus::Missing: ++stats_.missing; break;
    case CacheStatus::Corrupt: ++stats_.corrupt; break;
    case CacheStatus::Oversized: ++stats_.oversized; break;
    case CacheStatus::Unsupported: ++stats_.unsupported; break;
    case CacheStatus::BudgetRejected: ++stats_.budget_rejected; break;
    case CacheStatus::MetadataOnly: break;
    }
    try {
        entries_.push_back(std::move(entry));
    } catch (const std::bad_alloc&) {
        ++stats_.paint_failures;
        return false;
    }
    const Entry& stored = entries_.back();
    if (stored.status != CacheStatus::Ready) return false;
    result = {handle, {jellyframe::ImageIntrinsicSizeState::Known,
                       static_cast<std::uint16_t>(stored.width),
                       static_cast<std::uint16_t>(stored.height)}};
    return true;
}

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
jellyframe::ImageIntrinsicSize BmpImageAdapter::natural_size(const jellyframe::Node& node) {
    const std::string_view url = node.attribute("src");
    if (url.empty() || url.size() > kMaxUrlBytes) return {};
    std::string canonical_url;
    if (!resolve_local_resource_url(url, resource_context_.base_url, canonical_url) ||
        canonical_url.size() > kMaxUrlBytes || !is_bmp_url(canonical_url)) return {};

    if (const Entry* cached = find(canonical_url)) {
        if (cached->status == CacheStatus::MetadataOnly || cached->status == CacheStatus::Ready) {
            return {jellyframe::ImageIntrinsicSizeState::Known,
                    static_cast<std::uint16_t>(cached->width),
                    static_cast<std::uint16_t>(cached->height)};
        }
        return {};
    }
    if (entries_.size() >= kMaxCacheEntries || resource_context_.bundle == nullptr ||
        resource_context_.bundle->entries == nullptr) return {};

    for (std::size_t index = 0; index < resource_context_.bundle->count; ++index) {
        const ResourceEntry& source = resource_context_.bundle->entries[index];
        if (source.url != canonical_url || source.kind != jellyframe::HostResourceKind::Image ||
            source.bytes == nullptr || source.size == 0 ||
            (resource_context_.max_resource_bytes > 0 && source.size > resource_context_.max_resource_bytes)) {
            continue;
        }
        jellyframe::ImageIntrinsicSize size;
        const std::string_view bytes(reinterpret_cast<const char*>(source.bytes), source.size);
        if (!inspect_bmp_intrinsic_size(bytes, size)) return {};
        if (size.state != jellyframe::ImageIntrinsicSizeState::Known) return size;

        Entry metadata;
        metadata.url = std::move(canonical_url);
        metadata.status = CacheStatus::MetadataOnly;
        metadata.handle = static_cast<std::uint32_t>(entries_.size() + 1);
        metadata.width = size.width;
        metadata.height = size.height;
        try {
            entries_.push_back(std::move(metadata));
        } catch (const std::bad_alloc&) {
            return {};
        }
        return size;
    }
    return {};
}
#endif

bool BmpImageAdapter::paint(jellyframe::FrameBuffer& target,
                            const jellyframe::ImagePaintRequest& request) {
    ++stats_.paint_calls;
    const jellyframe::Rect rect = request.destination;
    const Entry* entry = find(request.image_handle);
    if (entry == nullptr || entry->status != CacheStatus::Ready || rect.width <= 0 || rect.height <= 0) {
        ++stats_.paint_failures;
        return false;
    }
    const double source_w = static_cast<double>(entry->width);
    const double source_h = static_cast<double>(entry->height);
    double draw_w = static_cast<double>(rect.width);
    double draw_h = static_cast<double>(rect.height);
    if (request.object_fit == jellyframe::ObjectFit::None) {
        draw_w = source_w;
        draw_h = source_h;
    } else if (request.object_fit == jellyframe::ObjectFit::Contain ||
               request.object_fit == jellyframe::ObjectFit::Cover ||
               request.object_fit == jellyframe::ObjectFit::ScaleDown) {
        const double contain_scale = std::min(rect.width / source_w, rect.height / source_h);
        const double cover_scale = std::max(rect.width / source_w, rect.height / source_h);
        double scale = request.object_fit == jellyframe::ObjectFit::Cover ? cover_scale : contain_scale;
        if (request.object_fit == jellyframe::ObjectFit::ScaleDown) {
            scale = std::min(1.0, contain_scale);
        }
        draw_w = std::max(1.0, std::round(source_w * scale));
        draw_h = std::max(1.0, std::round(source_h * scale));
    }
    const int position_x = std::clamp(request.object_position.x_percent, 0, 100);
    const int position_y = std::clamp(request.object_position.y_percent, 0, 100);
    const double offset_x = (static_cast<double>(rect.width) - draw_w) * position_x / 100.0;
    const double offset_y = (static_cast<double>(rect.height) - draw_h) * position_y / 100.0;
    for (int y = 0; y < rect.height; ++y) {
        const double source_y = (y - offset_y) * source_h / draw_h;
        if (source_y < 0.0 || source_y >= source_h) continue;
        const int sy = std::clamp(static_cast<int>(source_y), 0, entry->height - 1);
        for (int x = 0; x < rect.width; ++x) {
            const double source_x = (x - offset_x) * source_w / draw_w;
            if (source_x < 0.0 || source_x >= source_w) continue;
            const int sx = std::clamp(static_cast<int>(source_x), 0, entry->width - 1);
            const std::size_t index = static_cast<std::size_t>(sy) * entry->width + sx;
            const int tx = rect.x + x;
            const int ty = rect.y + y;
            if (target.contains(tx, ty)) {
                blend_pixel(target.pixel(tx, ty), unpack_rgb565(entry->pixels[index], entry->alpha[index]));
            }
        }
    }
    return true;
}

namespace {
bool resolve_callback(const jellyframe::Node& node,
                      jellyframe::ImageResolveKind kind,
                      std::uint16_t background_resource_id,
                      jellyframe::ResolvedImageResource& result,
                      void* context) {
    auto* adapter = static_cast<BmpImageAdapter*>(context);
    return adapter != nullptr && adapter->resolve(node, kind, background_resource_id, result);
}

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
jellyframe::ImageIntrinsicSize natural_size_callback(const jellyframe::Node& node, void* context) {
    auto* adapter = static_cast<BmpImageAdapter*>(context);
    return adapter != nullptr ? adapter->natural_size(node) : jellyframe::ImageIntrinsicSize{};
}
#endif

bool paint_callback(jellyframe::FrameBuffer& target,
                    const jellyframe::ImagePaintRequest& request,
                    void* context) {
    auto* adapter = static_cast<BmpImageAdapter*>(context);
    return adapter != nullptr && adapter->paint(target, request);
}
} // namespace

jellyframe::ImageHandleResolver make_bmp_image_resolver(BmpImageAdapter& adapter) {
    return jellyframe::ImageHandleResolver{resolve_callback, &adapter};
}

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION)
jellyframe::NaturalImageSizeProvider make_bmp_natural_image_size_provider(BmpImageAdapter& adapter) {
    return jellyframe::NaturalImageSizeProvider{natural_size_callback, &adapter};
}
#endif

jellyframe::ImagePainter make_bmp_image_painter(BmpImageAdapter& adapter) {
    return jellyframe::ImagePainter{paint_callback, &adapter};
}

} // namespace jellyframe_esp32s3
