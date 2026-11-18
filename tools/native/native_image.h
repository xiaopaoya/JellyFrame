#pragma once

#include "app_runtime/app_services.h"
#include "app_package.h"
#include <cstdlib>
#include <limits>

namespace jellyframe::native {

inline std::int32_t read_i32_le(const std::uint8_t* data) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U));
}

inline bool decode_bmp_to_fixture(const std::string& url,
                           const std::string& bytes,
                           ImageDecodeFixture& fixture,
                           DiagnosticSink* diagnostics) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t size = bytes.size();
    if (size < 54 || data[0] != 'B' || data[1] != 'M') {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-unsupported",
                          "Package image is not a supported BMP file",
                          url);
        return false;
    }
    const std::uint32_t pixel_offset = jellyframe_example::read_le32(data + 10);
    const std::uint32_t dib_size = jellyframe_example::read_le32(data + 14);
    if (dib_size < 40 || !jellyframe_example::byte_range_is_valid(size, 14, dib_size) ||
        pixel_offset < static_cast<std::size_t>(14) + dib_size || pixel_offset >= size) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-invalid",
                          "BMP header is truncated or invalid",
                          url);
        return false;
    }
    const std::int32_t width = read_i32_le(data + 18);
    const std::int32_t signed_height = read_i32_le(data + 22);
    const std::uint16_t planes = jellyframe_example::read_le16(data + 26);
    const std::uint16_t bits_per_pixel = jellyframe_example::read_le16(data + 28);
    const std::uint32_t compression = jellyframe_example::read_le32(data + 30);
    if (width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min() ||
        planes != 1 || compression != 0 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32)) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-unsupported",
                          "Only uncompressed 24-bit and 32-bit BMP package images are supported in the native desktop tools",
                          url);
        return false;
    }
    const int height = std::abs(signed_height);
    if (width > 256 || height > 256) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP exceeds the native desktop tools image budget",
                          url);
        return false;
    }
    const std::size_t decoded_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (decoded_bytes > 256U * 256U * 4U) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP decoded bytes exceed the native desktop tools image budget",
                          url);
        return false;
    }
    const bool top_down = signed_height < 0;
    const std::size_t bytes_per_pixel = bits_per_pixel / 8;
    const std::size_t row_stride = ((static_cast<std::size_t>(width) * bits_per_pixel + 31U) / 32U) * 4U;
    if (!jellyframe_example::byte_range_is_valid(size, pixel_offset, row_stride * static_cast<std::size_t>(height))) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-invalid",
                          "BMP pixel data is truncated",
                          url);
        return false;
    }

    std::vector<std::uint8_t> pixels;
    pixels.resize(decoded_bytes);
    for (int y = 0; y < height; ++y) {
        const int source_y = top_down ? y : height - 1 - y;
        const std::uint8_t* source_row = data + pixel_offset + static_cast<std::size_t>(source_y) * row_stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* source = source_row + static_cast<std::size_t>(x) * bytes_per_pixel;
            const std::size_t output_index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(x)) * 4U;
            pixels[output_index] = source[2];
            pixels[output_index + 1] = source[1];
            pixels[output_index + 2] = source[0];
            pixels[output_index + 3] = bits_per_pixel == 32 ? source[3] : 255;
        }
    }

    fixture = ImageDecodeFixture{
        url,
        width,
        height,
        width,
        HostPixelFormat::Rgba8888,
        std::move(pixels),
    };
    return true;
}


} // namespace jellyframe::native
