#include "jellyframe_esp32s3_image.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace jellyframe_esp32s3 {
const ResourceBundle& generated_resource_bundle() {
    static const ResourceBundle empty{};
    return empty;
}
} // namespace jellyframe_esp32s3

namespace {

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::vector<std::uint8_t> make_bmp(std::int32_t width, std::int32_t height) {
    const std::size_t absolute_height = height < 0
        ? static_cast<std::size_t>(-static_cast<std::int64_t>(height))
        : static_cast<std::size_t>(height);
    const std::size_t row_bytes = ((static_cast<std::size_t>(std::max(width, 0)) * 24U + 31U) / 32U) * 4U;
    std::vector<std::uint8_t> bytes(54U + row_bytes * absolute_height, 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    put_u32(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
    put_u32(bytes, 10, 54);
    put_u32(bytes, 14, 40);
    put_u32(bytes, 18, static_cast<std::uint32_t>(width));
    put_u32(bytes, 22, static_cast<std::uint32_t>(height));
    put_u16(bytes, 26, 1);
    put_u16(bytes, 28, 24);
    put_u32(bytes, 34, static_cast<std::uint32_t>(row_bytes * absolute_height));
    if (width >= 2 && height != 0) {
        bytes[54] = 0;
        bytes[55] = 0;
        bytes[56] = 255;
        bytes[57] = 0;
        bytes[58] = 255;
        bytes[59] = 0;
    }
    return bytes;
}

std::unique_ptr<jellyframe::Node> image_node(std::string_view source) {
    auto node = std::make_unique<jellyframe::Node>(jellyframe::NodeType::Element);
    node->tag_name = "img";
    node->set_attribute("src", std::string(source));
    return node;
}

} // namespace

int main() {
    using namespace jellyframe;
    using namespace jellyframe_esp32s3;

    const std::string known_url = "/apps/demo/images/known.bmp";
    const std::string invalid_url = "/apps/demo/images/invalid.bmp";
    const std::string large_url = "/apps/demo/images/large.bmp";
    const std::string budget_url = "/apps/demo/images/budget.bmp";
    const std::vector<std::uint8_t> known_bytes = make_bmp(2, 1);
    const std::vector<std::uint8_t> invalid_bytes = make_bmp(0, 1);
    const std::vector<std::uint8_t> large_bytes = make_bmp(97, 1);
    const std::vector<std::uint8_t> budget_bytes = make_bmp(2, 1);
    const ResourceEntry resources[] = {
        {known_url, HostResourceKind::Image, known_bytes.data(), known_bytes.size()},
        {invalid_url, HostResourceKind::Image, invalid_bytes.data(), invalid_bytes.size()},
        {large_url, HostResourceKind::Image, large_bytes.data(), large_bytes.size()},
        {budget_url, HostResourceKind::Image, budget_bytes.data(), budget_bytes.size()},
    };
    const ResourceBundle bundle{resources, 4};
    BmpImageAdapter adapter;
    adapter.configure(HostBudgets{}, "/apps/demo/index.html", &bundle, nullptr);

    const auto known = image_node("images/known.bmp");
    const NaturalImageSizeProvider provider = make_bmp_natural_image_size_provider(adapter);
    const ImageIntrinsicSize metadata = provider.intrinsic_size(*known, provider.context);
    assert(metadata.state == ImageIntrinsicSizeState::Known);
    assert(metadata.width == 2 && metadata.height == 1);
    assert(adapter.stats().decoded == 0);

    ResolvedImageResource resolved;
    const ImageHandleResolver resolver = make_bmp_image_resolver(adapter);
    assert(resolver.resolve(*known, ImageResolveKind::Content, 0, resolved, resolver.context));
    assert(resolved.image_handle != 0);
    assert(resolved.intrinsic_size.state == ImageIntrinsicSizeState::Known);
    assert(adapter.stats().decoded == 1);
    assert(adapter.stats().cache_hits == 1);
    const ImageIntrinsicSize cached_metadata = provider.intrinsic_size(*known, provider.context);
    assert(cached_metadata.state == ImageIntrinsicSizeState::Known &&
           cached_metadata.width == metadata.width && cached_metadata.height == metadata.height);

    FrameBuffer target(2, 1, Color{0, 0, 0, 255});
    const ImagePainter painter = make_bmp_image_painter(adapter);
    const ImagePaintRequest paint_request{resolved.image_handle, Rect{0, 0, 2, 1}};
    assert(painter.paint(target, paint_request, painter.context));
    assert(target.pixel(0, 0).r > 200 && target.pixel(0, 0).g < 40);
    assert(target.pixel(1, 0).g > 200 && target.pixel(1, 0).r < 40);

    const auto missing = image_node("images/missing.bmp");
    assert(provider.intrinsic_size(*missing, provider.context).state == ImageIntrinsicSizeState::Unknown);
    const auto invalid = image_node("images/invalid.bmp");
    assert(provider.intrinsic_size(*invalid, provider.context).state == ImageIntrinsicSizeState::Invalid);
    const auto large = image_node("images/large.bmp");
    assert(provider.intrinsic_size(*large, provider.context).state == ImageIntrinsicSizeState::Unknown);

    HostBudgets bounded;
    bounded.max_resource_bytes = known_bytes.size() - 1;
    BmpImageAdapter limited_adapter;
    limited_adapter.configure(bounded, "/apps/demo/index.html", &bundle, nullptr);
    const NaturalImageSizeProvider limited_provider =
        make_bmp_natural_image_size_provider(limited_adapter);
    const auto budget_limited = image_node("images/budget.bmp");
    assert(limited_provider.intrinsic_size(*budget_limited, limited_provider.context).state ==
           ImageIntrinsicSizeState::Unknown);

    std::cout << "ESP32 BMP natural metadata snapshot and shared paint cache passed\n";
    return 0;
}
