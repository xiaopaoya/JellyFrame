#include "jellyframe_esp32s3_image.h"

#include "esp_timer.h"

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

void BmpImageAdapter::configure(const jellyframe::HostBudgets& budgets, std::string_view base_url) {
    resource_stats_ = {};
    stats_ = {};
    entries_.clear();
    resource_context_ = make_resource_context(budgets, base_url, &resource_stats_);
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

bool BmpImageAdapter::resolve(const jellyframe::Node& node,
                              jellyframe::ImageResolveKind kind,
                              std::uint16_t,
                              std::uint32_t& handle) {
    handle = 0;
    if (kind != jellyframe::ImageResolveKind::Content) {
        return false;
    }
    const std::string_view url = node.attribute("src");
    if (url.empty()) {
        return false;
    }
    ++stats_.requests;
    if (Entry* cached = find(url)) {
        ++stats_.cache_hits;
        handle = cached->handle;
        return cached->status == CacheStatus::Ready;
    }
    if (url.size() > kMaxUrlBytes || entries_.size() >= kMaxCacheEntries) {
        ++stats_.budget_rejected;
        return false;
    }
    Entry entry;
    entry.url = std::string(url);
    entry.handle = static_cast<std::uint32_t>(entries_.size() + 1);
    if (!is_bmp_url(url)) {
        entry.status = CacheStatus::Unsupported;
    } else {
        std::string bytes;
        const std::uint32_t missing_before = resource_stats_.missing_loads;
        const std::uint32_t rejected_before = resource_stats_.rejected_loads;
        if (!load_resource(jellyframe::HostResourceRequest{
                jellyframe::HostResourceKind::Image, url, resource_context_.base_url}, bytes, &resource_context_)) {
            entry.status = rejected_before != resource_stats_.rejected_loads
                ? CacheStatus::Oversized
                : (missing_before != resource_stats_.missing_loads ? CacheStatus::Missing : CacheStatus::Corrupt);
        } else {
            const std::uint64_t start = esp_timer_get_time();
            entry.status = decode(url, bytes, entry);
            stats_.decode_us += static_cast<std::uint64_t>(esp_timer_get_time() - start);
        }
    }
    handle = entry.handle;
    switch (entry.status) {
    case CacheStatus::Ready: ++stats_.decoded; break;
    case CacheStatus::Missing: ++stats_.missing; break;
    case CacheStatus::Corrupt: ++stats_.corrupt; break;
    case CacheStatus::Oversized: ++stats_.oversized; break;
    case CacheStatus::Unsupported: ++stats_.unsupported; break;
    case CacheStatus::BudgetRejected: ++stats_.budget_rejected; break;
    }
    try {
        entries_.push_back(std::move(entry));
    } catch (const std::bad_alloc&) {
        handle = 0;
        ++stats_.paint_failures;
        return false;
    }
    return entries_.back().status == CacheStatus::Ready;
}

bool BmpImageAdapter::paint(jellyframe::FrameBuffer& target,
                            jellyframe::Rect rect,
                            std::uint32_t handle,
                            jellyframe::ObjectFit object_fit,
                            jellyframe::ObjectPosition object_position,
                            jellyframe::ImageRendering) {
    ++stats_.paint_calls;
    const Entry* entry = find(handle);
    if (entry == nullptr || entry->status != CacheStatus::Ready || rect.width <= 0 || rect.height <= 0) {
        ++stats_.paint_failures;
        return false;
    }
    const double source_w = static_cast<double>(entry->width);
    const double source_h = static_cast<double>(entry->height);
    double draw_w = static_cast<double>(rect.width);
    double draw_h = static_cast<double>(rect.height);
    if (object_fit == jellyframe::ObjectFit::None) {
        draw_w = source_w;
        draw_h = source_h;
    } else if (object_fit == jellyframe::ObjectFit::Contain || object_fit == jellyframe::ObjectFit::Cover ||
               object_fit == jellyframe::ObjectFit::ScaleDown) {
        const double contain_scale = std::min(rect.width / source_w, rect.height / source_h);
        const double cover_scale = std::max(rect.width / source_w, rect.height / source_h);
        double scale = object_fit == jellyframe::ObjectFit::Cover ? cover_scale : contain_scale;
        if (object_fit == jellyframe::ObjectFit::ScaleDown) {
            scale = std::min(1.0, contain_scale);
        }
        draw_w = std::max(1.0, std::round(source_w * scale));
        draw_h = std::max(1.0, std::round(source_h * scale));
    }
    const int position_x = std::clamp(object_position.x_percent, 0, 100);
    const int position_y = std::clamp(object_position.y_percent, 0, 100);
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
                      std::uint32_t& handle,
                      void* context) {
    auto* adapter = static_cast<BmpImageAdapter*>(context);
    return adapter != nullptr && adapter->resolve(node, kind, background_resource_id, handle);
}

bool paint_callback(jellyframe::FrameBuffer& target,
                    jellyframe::Rect rect,
                    std::uint32_t handle,
                    jellyframe::ObjectFit object_fit,
                    jellyframe::ObjectPosition object_position,
                    jellyframe::ImageRendering image_rendering,
                    void* context) {
    auto* adapter = static_cast<BmpImageAdapter*>(context);
    return adapter != nullptr && adapter->paint(target, rect, handle, object_fit, object_position, image_rendering);
}
} // namespace

jellyframe::ImageHandleResolver make_bmp_image_resolver(BmpImageAdapter& adapter) {
    return jellyframe::ImageHandleResolver{resolve_callback, &adapter};
}

jellyframe::ImagePainter make_bmp_image_painter(BmpImageAdapter& adapter) {
    return jellyframe::ImagePainter{paint_callback, &adapter};
}

} // namespace jellyframe_esp32s3
