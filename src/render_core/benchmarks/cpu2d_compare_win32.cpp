#include "render_core/software_renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr int kWarmupIterations = 30;
constexpr Color kFillColor{22, 71, 87, 255};
constexpr Color kGradientFirst{22, 71, 87, 255};
constexpr Color kGradientSecond{6, 22, 31, 255};

enum class Workload {
    OpaqueFill,
    HorizontalGradient,
    VerticalGradient,
};

struct OutputComparison {
    std::string jellyframe_digest;
    std::string gdi_digest;
    double rmse = 0.0;
    int max_channel_error = 0;
};

#ifndef JELLYFRAME_CPU2D_CORE_VERSION
#define JELLYFRAME_CPU2D_CORE_VERSION "unknown"
#endif

struct GdiSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous_bitmap = nullptr;
    HBRUSH brush = nullptr;
    std::uint8_t* pixels = nullptr;

    GdiSurface() {
        dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) {
            throw std::runtime_error("failed to create GDI memory DC");
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kWidth;
        info.bmiHeader.biHeight = -kHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bitmap_pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                  &bitmap_pixels, nullptr, 0);
        pixels = static_cast<std::uint8_t*>(bitmap_pixels);
        if (bitmap == nullptr || pixels == nullptr) {
            DeleteDC(dc);
            dc = nullptr;
            throw std::runtime_error("failed to create GDI comparison bitmap");
        }
        brush = CreateSolidBrush(RGB(kFillColor.r, kFillColor.g, kFillColor.b));
        if (brush == nullptr) {
            DeleteObject(bitmap);
            bitmap = nullptr;
            DeleteDC(dc);
            dc = nullptr;
            throw std::runtime_error("failed to create GDI comparison brush");
        }
        previous_bitmap = SelectObject(dc, bitmap);
        if (previous_bitmap == nullptr || previous_bitmap == HGDI_ERROR) {
            DeleteObject(brush);
            brush = nullptr;
            DeleteObject(bitmap);
            bitmap = nullptr;
            DeleteDC(dc);
            dc = nullptr;
            throw std::runtime_error("failed to select GDI comparison bitmap");
        }
    }

    ~GdiSurface() {
        if (dc != nullptr && previous_bitmap != nullptr) {
            SelectObject(dc, previous_bitmap);
        }
        if (brush != nullptr) DeleteObject(brush);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr) DeleteDC(dc);
    }

    GdiSurface(const GdiSurface&) = delete;
    GdiSurface& operator=(const GdiSurface&) = delete;

    void fill() const {
        RECT rect{0, 0, kWidth, kHeight};
        if (FillRect(dc, &rect, brush) == 0) {
            throw std::runtime_error("GDI FillRect failed");
        }
        if (GdiFlush() == 0) {
            throw std::runtime_error("GDI flush failed");
        }
    }

    void gradient(ULONG mode) const {
        TRIVERTEX vertices[2]{};
        vertices[0].x = 0;
        vertices[0].y = 0;
        vertices[0].Red = static_cast<COLOR16>(kGradientFirst.r << 8U);
        vertices[0].Green = static_cast<COLOR16>(kGradientFirst.g << 8U);
        vertices[0].Blue = static_cast<COLOR16>(kGradientFirst.b << 8U);
        vertices[0].Alpha = 0xffffU;
        vertices[1].x = kWidth;
        vertices[1].y = kHeight;
        vertices[1].Red = static_cast<COLOR16>(kGradientSecond.r << 8U);
        vertices[1].Green = static_cast<COLOR16>(kGradientSecond.g << 8U);
        vertices[1].Blue = static_cast<COLOR16>(kGradientSecond.b << 8U);
        vertices[1].Alpha = 0xffffU;
        GRADIENT_RECT rectangle{0, 1};
        if (GradientFill(dc, vertices, 2, &rectangle, 1, mode) == 0) {
            throw std::runtime_error("GDI GradientFill failed");
        }
        if (GdiFlush() == 0) {
            throw std::runtime_error("GDI gradient flush failed");
        }
    }

    void horizontal_gradient() const { gradient(GRADIENT_FILL_RECT_H); }
    void vertical_gradient() const { gradient(GRADIENT_FILL_RECT_V); }
};

int positive_int(const char* raw, const char* name) {
    try {
        const int value = std::stoi(raw);
        if (value > 0) return value;
    } catch (...) {
    }
    throw std::runtime_error(std::string(name) + " must be a positive integer");
}

Workload parse_workload(const char* raw) {
    const std::string name(raw);
    if (name == "opaque-fill") return Workload::OpaqueFill;
    if (name == "horizontal-gradient") return Workload::HorizontalGradient;
    if (name == "vertical-gradient") return Workload::VerticalGradient;
    throw std::runtime_error("workload must be opaque-fill, horizontal-gradient, or vertical-gradient");
}

const char* workload_id(Workload workload) {
    switch (workload) {
    case Workload::OpaqueFill: return "opaque-fill-rgb-v1";
    case Workload::HorizontalGradient: return "horizontal-gradient-rgb-v1";
    case Workload::VerticalGradient: return "vertical-gradient-rgb-v1";
    }
    return "unknown";
}

std::uint64_t hash_byte(std::uint64_t hash, std::uint8_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

std::string rgb_hash(const FrameBuffer& frame) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (const Color pixel : frame.pixels) {
        hash = hash_byte(hash, pixel.r);
        hash = hash_byte(hash, pixel.g);
        hash = hash_byte(hash, pixel.b);
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

std::string rgb_hash(const GdiSurface& surface) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    const std::size_t pixels = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t index = 0; index < pixels; ++index) {
        const std::uint8_t* bgra = surface.pixels + index * 4U;
        hash = hash_byte(hash, bgra[2]);
        hash = hash_byte(hash, bgra[1]);
        hash = hash_byte(hash, bgra[0]);
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

OutputComparison compare_output(const FrameBuffer& frame, const GdiSurface& surface) {
    OutputComparison comparison;
    comparison.jellyframe_digest = rgb_hash(frame);
    comparison.gdi_digest = rgb_hash(surface);
    double squared_error = 0.0;
    const std::size_t pixel_count = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const Color actual = frame.pixels[index];
        const std::uint8_t* expected_bgra = surface.pixels + index * 4U;
        const int errors[3]{
            std::abs(static_cast<int>(actual.r) - expected_bgra[2]),
            std::abs(static_cast<int>(actual.g) - expected_bgra[1]),
            std::abs(static_cast<int>(actual.b) - expected_bgra[0]),
        };
        for (const int error : errors) {
            comparison.max_channel_error = std::max(comparison.max_channel_error, error);
            squared_error += static_cast<double>(error * error);
        }
    }
    comparison.rmse = std::sqrt(squared_error / static_cast<double>(pixel_count * 3U));
    return comparison;
}

template <typename Fn>
std::vector<double> measure(int samples, Fn&& fn) {
    for (int index = 0; index < kWarmupIterations; ++index) fn();
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(samples));
    for (int index = 0; index < samples; ++index) {
        const auto begin = Clock::now();
        fn();
        const auto end = Clock::now();
        values.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    return values;
}

std::string json_array(const std::vector<double>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        output << std::fixed << std::setprecision(3) << values[index];
    }
    output << ']';
    return output.str();
}

std::string repeated_pixels(int samples) {
    std::ostringstream output;
    output << '[';
    for (int index = 0; index < samples; ++index) {
        if (index != 0) output << ',';
        output << kWidth * kHeight;
    }
    output << ']';
    return output.str();
}

void write_manifest(const std::filesystem::path& path,
                    const char* library,
                    const char* version,
                    Workload workload,
                    const std::vector<double>& samples,
                    const std::string& digest,
                    const OutputComparison& comparison,
                    double tolerance,
                    bool output_matches) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("failed to write " + path.string());
#if defined(_M_X64)
    constexpr const char* architecture = "x64";
#elif defined(_M_ARM64)
    constexpr const char* architecture = "arm64";
#else
    constexpr const char* architecture = "windows-unknown";
#endif
#if defined(NDEBUG)
    constexpr const char* build_type = "release";
#else
    constexpr const char* build_type = "debug";
#endif
    output << "{\n"
           << "  \"format\": \"jellyframe.benchmark.run.v0\",\n"
           << "  \"library\": \"" << library << "\",\n"
           << "  \"version\": \"" << version << "\",\n"
           << "  \"workload\": \"" << workload_id(workload) << "\",\n"
           << "  \"viewport\": {\"width\": " << kWidth << ", \"height\": " << kHeight << "},\n"
           << "  \"pixelFormat\": \"rgb888\",\n"
           << "  \"antialiasing\": false,\n"
           << "  \"mode\": \"full\",\n"
           << "  \"environment\": {\"os\": \"windows\", \"architecture\": \"" << architecture
           << "\", \"process\": \"same\", \"buildType\": \"" << build_type << "\"},\n"
           << "  \"warmupIterations\": " << kWarmupIterations << ",\n"
           << "  \"outputValidation\": {\"status\": \"" << (output_matches ? "pass" : "fail")
           << "\", \"method\": \"" << (tolerance == 0.0 ? "normalized-rgb-exact" : "normalized-rgb-rmse")
           << "\", \"reference\": \"" << workload_id(workload) << "\", \"tolerance\": "
           << std::fixed << std::setprecision(3) << tolerance
           << ", \"observedRmse\": " << comparison.rmse
           << ", \"maxChannelError\": " << comparison.max_channel_error
           << ", \"digest\": \"" << digest << "\"},\n"
           << "  \"measurements\": {\"paint_us\": " << json_array(samples)
           << ", \"pixels\": " << repeated_pixels(static_cast<int>(samples.size())) << "}\n"
           << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) {
            std::cout << "usage: jellyframe_cpu2d_compare <output-directory> [samples=100] [workload=opaque-fill]\n";
            return argc < 2 ? 2 : 0;
        }
        const int samples = argc >= 3 ? positive_int(argv[2], "samples") : 100;
        const Workload workload = argc == 4 ? parse_workload(argv[3]) : Workload::OpaqueFill;
        const std::filesystem::path output_directory(argv[1]);

        FrameBuffer jellyframe_surface(kWidth, kHeight, Color{0, 0, 0, 255});
        DisplayCommand command;
        command.type = workload == Workload::OpaqueFill
            ? DisplayCommandType::FillRect
            : DisplayCommandType::LinearGradient;
        command.rect = Rect{0, 0, kWidth, kHeight};
        command.color = workload == Workload::OpaqueFill ? kFillColor : kGradientFirst;
        command.color2 = kGradientSecond;
        command.gradient_axis = workload == Workload::VerticalGradient
            ? GradientAxis::Vertical
            : GradientAxis::Horizontal;
        SoftwareRasterizer rasterizer;
        const auto jellyframe_samples = measure(samples, [&] {
            rasterizer.rasterize(command, jellyframe_surface, Rect{0, 0, kWidth, kHeight});
        });

        GdiSurface gdi_surface;
        const auto gdi_samples = measure(samples, [&] {
            if (workload == Workload::OpaqueFill) gdi_surface.fill();
            else if (workload == Workload::HorizontalGradient) gdi_surface.horizontal_gradient();
            else gdi_surface.vertical_gradient();
        });
        const OutputComparison comparison = compare_output(jellyframe_surface, gdi_surface);
        const double tolerance = workload == Workload::OpaqueFill ? 0.0 : 1.0;
        const bool output_matches = comparison.rmse <= tolerance;
        write_manifest(output_directory / "jellyframe.json", "jellyframe-render-core", JELLYFRAME_CPU2D_CORE_VERSION,
                       workload, jellyframe_samples, comparison.jellyframe_digest,
                       comparison, tolerance, output_matches);
        write_manifest(output_directory / "gdi.json", "windows-gdi", "system",
                       workload, gdi_samples, comparison.gdi_digest,
                       comparison, tolerance, output_matches);
        std::cout << "output=" << output_directory.string()
                  << " samples=" << samples
                  << " workload=" << workload_id(workload)
                  << " output_validation=" << (output_matches ? "pass" : "fail")
                  << " rmse=" << comparison.rmse
                  << " max_channel_error=" << comparison.max_channel_error
                  << " jellyframe_digest=" << comparison.jellyframe_digest
                  << " gdi_digest=" << comparison.gdi_digest << '\n';
        return output_matches ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "jellyframe_cpu2d_compare failed: " << error.what() << '\n';
        return 1;
    }
}
