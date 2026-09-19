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
constexpr Rect kFullRect{0, 0, kWidth, kHeight};
constexpr Rect kDirtyRect{38, 136, 96, 48};

enum class Workload {
    OpaqueFill,
    OpaqueDirtyFill,
    HorizontalGradient,
    VerticalGradient,
};

enum class Backend {
    Gdi,
    Sdl2,
};

struct OutputComparison {
    std::string jellyframe_digest;
    std::string reference_digest;
    double rmse = 0.0;
    int max_channel_error = 0;
};

struct SdlVersion {
    std::uint8_t major = 0;
    std::uint8_t minor = 0;
    std::uint8_t patch = 0;
};

struct SdlRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SdlSurface {
    using InitFn = int(__cdecl*)(std::uint32_t);
    using QuitFn = void(__cdecl*)();
    using GetErrorFn = const char*(__cdecl*)();
    using GetVersionFn = void(__cdecl*)(SdlVersion*);
    using CreateSurfaceFn = void*(__cdecl*)(void*, int, int, int, int,
                                            std::uint32_t, std::uint32_t,
                                            std::uint32_t, std::uint32_t);
    using FreeSurfaceFn = void(__cdecl*)(void*);
    using CreateRendererFn = void*(__cdecl*)(void*);
    using DestroyRendererFn = void(__cdecl*)(void*);
    using SetDrawBlendModeFn = int(__cdecl*)(void*, int);
    using SetDrawColorFn = int(__cdecl*)(void*, std::uint8_t, std::uint8_t,
                                         std::uint8_t, std::uint8_t);
    using RenderClearFn = int(__cdecl*)(void*);
    using RenderFillRectFn = int(__cdecl*)(void*, const SdlRect*);
    using RenderPresentFn = void(__cdecl*)(void*);

    HMODULE library = nullptr;
    InitFn init = nullptr;
    QuitFn quit = nullptr;
    GetErrorFn get_error = nullptr;
    GetVersionFn get_version = nullptr;
    CreateSurfaceFn create_surface = nullptr;
    FreeSurfaceFn free_surface = nullptr;
    CreateRendererFn create_renderer = nullptr;
    DestroyRendererFn destroy_renderer = nullptr;
    SetDrawBlendModeFn set_draw_blend_mode = nullptr;
    SetDrawColorFn set_draw_color = nullptr;
    RenderClearFn render_clear = nullptr;
    RenderFillRectFn render_fill_rect = nullptr;
    RenderPresentFn render_present = nullptr;
    std::vector<std::uint32_t> storage;
    void* surface = nullptr;
    void* renderer = nullptr;
    bool initialized = false;
    std::string version;

    explicit SdlSurface(const std::filesystem::path& library_path)
        : storage(static_cast<std::size_t>(kWidth) * kHeight, 0U) {
        library = LoadLibraryW(library_path.c_str());
        if (library == nullptr) {
            throw std::runtime_error("failed to load SDL2 library: " + library_path.string());
        }
        try {
            init = symbol<InitFn>("SDL_Init");
            quit = symbol<QuitFn>("SDL_Quit");
            get_error = symbol<GetErrorFn>("SDL_GetError");
            get_version = symbol<GetVersionFn>("SDL_GetVersion");
            create_surface = symbol<CreateSurfaceFn>("SDL_CreateRGBSurfaceFrom");
            free_surface = symbol<FreeSurfaceFn>("SDL_FreeSurface");
            create_renderer = symbol<CreateRendererFn>("SDL_CreateSoftwareRenderer");
            destroy_renderer = symbol<DestroyRendererFn>("SDL_DestroyRenderer");
            set_draw_blend_mode = symbol<SetDrawBlendModeFn>("SDL_SetRenderDrawBlendMode");
            set_draw_color = symbol<SetDrawColorFn>("SDL_SetRenderDrawColor");
            render_clear = symbol<RenderClearFn>("SDL_RenderClear");
            render_fill_rect = symbol<RenderFillRectFn>("SDL_RenderFillRect");
            render_present = symbol<RenderPresentFn>("SDL_RenderPresent");
            if (init(0) != 0) fail("SDL_Init failed");
            initialized = true;

            constexpr std::uint32_t red_mask = 0x00ff0000U;
            constexpr std::uint32_t green_mask = 0x0000ff00U;
            constexpr std::uint32_t blue_mask = 0x000000ffU;
            constexpr std::uint32_t alpha_mask = 0xff000000U;
            surface = create_surface(storage.data(), kWidth, kHeight, 32,
                                     kWidth * 4, red_mask, green_mask,
                                     blue_mask, alpha_mask);
            if (surface == nullptr) fail("SDL_CreateRGBSurfaceFrom failed");
            renderer = create_renderer(surface);
            if (renderer == nullptr) fail("SDL_CreateSoftwareRenderer failed");
            if (set_draw_blend_mode(renderer, 0) != 0) {
                fail("SDL_SetRenderDrawBlendMode failed");
            }
            if (set_draw_color(renderer, kFillColor.r, kFillColor.g,
                               kFillColor.b, kFillColor.a) != 0) {
                fail("SDL_SetRenderDrawColor failed");
            }
            SdlVersion current{};
            get_version(&current);
            version = std::to_string(current.major) + "." +
                      std::to_string(current.minor) + "." +
                      std::to_string(current.patch);
        } catch (...) {
            release();
            throw;
        }
    }

    ~SdlSurface() { release(); }

    SdlSurface(const SdlSurface&) = delete;
    SdlSurface& operator=(const SdlSurface&) = delete;

    void fill(Rect rect) const {
        if (rect.x == 0 && rect.y == 0 && rect.width == kWidth && rect.height == kHeight) {
            if (render_clear(renderer) != 0) fail("SDL_RenderClear failed");
        } else {
            const SdlRect target{rect.x, rect.y, rect.width, rect.height};
            if (render_fill_rect(renderer, &target) != 0) fail("SDL_RenderFillRect failed");
        }
        render_present(renderer);
    }

    const std::uint8_t* pixels() const {
        return reinterpret_cast<const std::uint8_t*>(storage.data());
    }

private:
    template <typename Fn>
    Fn symbol(const char* name) const {
        const FARPROC address = GetProcAddress(library, name);
        if (address == nullptr) {
            throw std::runtime_error(std::string("SDL2 library is missing ") + name);
        }
        return reinterpret_cast<Fn>(address);
    }

    [[noreturn]] void fail(const char* operation) const {
        const char* detail = get_error != nullptr ? get_error() : nullptr;
        throw std::runtime_error(std::string(operation) +
                                 (detail != nullptr && detail[0] != '\0'
                                      ? std::string(": ") + detail
                                      : std::string()));
    }

    void release() {
        if (renderer != nullptr && destroy_renderer != nullptr) {
            destroy_renderer(renderer);
            renderer = nullptr;
        }
        if (surface != nullptr && free_surface != nullptr) {
            free_surface(surface);
            surface = nullptr;
        }
        if (initialized && quit != nullptr) {
            quit();
            initialized = false;
        }
        if (library != nullptr) {
            FreeLibrary(library);
            library = nullptr;
        }
    }
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

    void fill(Rect target) const {
        RECT rect{target.x, target.y, target.x + target.width, target.y + target.height};
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
    if (name == "opaque-dirty-fill") return Workload::OpaqueDirtyFill;
    if (name == "horizontal-gradient") return Workload::HorizontalGradient;
    if (name == "vertical-gradient") return Workload::VerticalGradient;
    throw std::runtime_error(
        "workload must be opaque-fill, opaque-dirty-fill, horizontal-gradient, or vertical-gradient");
}

Backend parse_backend(const char* raw) {
    const std::string name(raw);
    if (name == "gdi") return Backend::Gdi;
    if (name == "sdl2") return Backend::Sdl2;
    throw std::runtime_error("backend must be gdi or sdl2");
}

const char* workload_id(Workload workload) {
    switch (workload) {
    case Workload::OpaqueFill: return "opaque-fill-rgb-v1";
    case Workload::OpaqueDirtyFill: return "opaque-dirty-fill-rgb-v1";
    case Workload::HorizontalGradient: return "horizontal-gradient-rgb-v1";
    case Workload::VerticalGradient: return "vertical-gradient-rgb-v1";
    }
    return "unknown";
}

bool is_fill_workload(Workload workload) {
    return workload == Workload::OpaqueFill || workload == Workload::OpaqueDirtyFill;
}

Rect workload_rect(Workload workload) {
    return workload == Workload::OpaqueDirtyFill ? kDirtyRect : kFullRect;
}

const char* workload_mode(Workload workload) {
    return workload == Workload::OpaqueDirtyFill ? "dirty" : "full";
}

int workload_pixels(Workload workload) {
    const Rect rect = workload_rect(workload);
    return rect.width * rect.height;
}

int workload_operations_per_sample(Workload workload) {
    return workload == Workload::OpaqueDirtyFill ? 64 : 1;
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

std::string rgb_hash_bgra(const std::uint8_t* pixels) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    const std::size_t pixel_count = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const std::uint8_t* bgra = pixels + index * 4U;
        hash = hash_byte(hash, bgra[2]);
        hash = hash_byte(hash, bgra[1]);
        hash = hash_byte(hash, bgra[0]);
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}

OutputComparison compare_output(const FrameBuffer& frame, const std::uint8_t* reference_pixels) {
    OutputComparison comparison;
    comparison.jellyframe_digest = rgb_hash(frame);
    comparison.reference_digest = rgb_hash_bgra(reference_pixels);
    double squared_error = 0.0;
    const std::size_t pixel_count = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const Color actual = frame.pixels[index];
        const std::uint8_t* expected_bgra = reference_pixels + index * 4U;
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
std::vector<double> measure(int samples, int operations_per_sample, Fn&& fn) {
    for (int index = 0; index < kWarmupIterations; ++index) fn();
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(samples));
    for (int index = 0; index < samples; ++index) {
        const auto begin = Clock::now();
        for (int operation = 0; operation < operations_per_sample; ++operation) fn();
        const auto end = Clock::now();
        values.push_back(std::chrono::duration<double, std::micro>(end - begin).count() /
                         operations_per_sample);
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

std::string repeated_pixels(int samples, int pixels) {
    std::ostringstream output;
    output << '[';
    for (int index = 0; index < samples; ++index) {
        if (index != 0) output << ',';
        output << pixels;
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
           << "  \"mode\": \"" << workload_mode(workload) << "\",\n"
           << "  \"environment\": {\"os\": \"windows\", \"architecture\": \"" << architecture
           << "\", \"process\": \"same\", \"buildType\": \"" << build_type << "\"},\n"
           << "  \"warmupIterations\": " << kWarmupIterations << ",\n"
           << "  \"operationsPerSample\": " << workload_operations_per_sample(workload) << ",\n"
           << "  \"outputValidation\": {\"status\": \"" << (output_matches ? "pass" : "fail")
           << "\", \"method\": \"" << (tolerance == 0.0 ? "normalized-rgb-exact" : "normalized-rgb-rmse")
           << "\", \"reference\": \"" << workload_id(workload) << "\", \"tolerance\": "
           << std::fixed << std::setprecision(3) << tolerance
           << ", \"observedRmse\": " << comparison.rmse
           << ", \"maxChannelError\": " << comparison.max_channel_error
           << ", \"digest\": \"" << digest << "\"},\n"
           << "  \"measurements\": {\"paint_us\": " << json_array(samples)
           << ", \"pixels\": " << repeated_pixels(static_cast<int>(samples.size()),
                                                    workload_pixels(workload)) << "}\n"
           << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 6) {
            std::cout << "usage: jellyframe_cpu2d_compare <output-directory> [samples=100] "
                         "[workload=opaque-fill] [backend=gdi] [SDL2.dll]\n";
            return argc < 2 ? 2 : 0;
        }
        const int samples = argc >= 3 ? positive_int(argv[2], "samples") : 100;
        const Workload workload = argc >= 4 ? parse_workload(argv[3]) : Workload::OpaqueFill;
        const Backend backend = argc >= 5 ? parse_backend(argv[4]) : Backend::Gdi;
        if (backend == Backend::Sdl2 && !is_fill_workload(workload)) {
            throw std::runtime_error(
                "SDL2 adapter currently supports only opaque-fill and opaque-dirty-fill");
        }
        if (backend == Backend::Gdi && argc == 6) {
            throw std::runtime_error("SDL2 library path is valid only with the sdl2 backend");
        }
        const std::filesystem::path output_directory(argv[1]);

        FrameBuffer jellyframe_surface(kWidth, kHeight, Color{0, 0, 0, 255});
        DisplayCommand command;
        command.type = is_fill_workload(workload)
            ? DisplayCommandType::FillRect
            : DisplayCommandType::LinearGradient;
        command.rect = workload_rect(workload);
        command.color = is_fill_workload(workload) ? kFillColor : kGradientFirst;
        command.color2 = kGradientSecond;
        command.gradient_axis = workload == Workload::VerticalGradient
            ? GradientAxis::Vertical
            : GradientAxis::Horizontal;
        SoftwareRasterizer rasterizer;
        const int operations_per_sample = workload_operations_per_sample(workload);
        const auto jellyframe_samples = measure(samples, operations_per_sample, [&] {
            rasterizer.rasterize(command, jellyframe_surface, command.rect);
        });

        OutputComparison comparison;
        std::vector<double> reference_samples;
        std::string reference_name;
        std::string reference_version;
        std::string reference_file;
        if (backend == Backend::Gdi) {
            GdiSurface gdi_surface;
            reference_samples = measure(samples, operations_per_sample, [&] {
                if (is_fill_workload(workload)) gdi_surface.fill(workload_rect(workload));
                else if (workload == Workload::HorizontalGradient) gdi_surface.horizontal_gradient();
                else gdi_surface.vertical_gradient();
            });
            comparison = compare_output(jellyframe_surface, gdi_surface.pixels);
            reference_name = "windows-gdi";
            reference_version = "system";
            reference_file = "gdi.json";
        } else {
            const std::filesystem::path library_path = argc == 6
                ? std::filesystem::path(argv[5])
                : std::filesystem::path(L"SDL2.dll");
            SdlSurface sdl_surface(library_path);
            reference_samples = measure(samples, operations_per_sample, [&] {
                sdl_surface.fill(workload_rect(workload));
            });
            comparison = compare_output(jellyframe_surface, sdl_surface.pixels());
            reference_name = "sdl2-software-renderer";
            reference_version = sdl_surface.version;
            reference_file = "sdl2.json";
        }
        const double tolerance = is_fill_workload(workload) ? 0.0 : 1.0;
        const bool output_matches = comparison.rmse <= tolerance;
        write_manifest(output_directory / "jellyframe.json", "jellyframe-render-core",
                       JELLYFRAME_CPU2D_CORE_VERSION, workload, jellyframe_samples,
                       comparison.jellyframe_digest, comparison, tolerance, output_matches);
        write_manifest(output_directory / reference_file, reference_name.c_str(),
                       reference_version.c_str(), workload, reference_samples,
                       comparison.reference_digest, comparison, tolerance, output_matches);
        std::cout << "output=" << output_directory.string()
                  << " samples=" << samples
                  << " workload=" << workload_id(workload)
                  << " backend=" << reference_name
                  << " output_validation=" << (output_matches ? "pass" : "fail")
                  << " rmse=" << comparison.rmse
                  << " max_channel_error=" << comparison.max_channel_error
                  << " jellyframe_digest=" << comparison.jellyframe_digest
                  << " reference_digest=" << comparison.reference_digest << '\n';
        return output_matches ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "jellyframe_cpu2d_compare failed: " << error.what() << '\n';
        return 1;
    }
}
