#include "render_core/software_renderer.h"
#include "cpu2d_sampling.h"
#include "cpu2d_text_fixture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cpu2d_rounded_gdiplus.h"

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
constexpr Color kAlphaColor{80, 180, 220, 128};
constexpr Rect kFullRect{0, 0, kWidth, kHeight};
constexpr Rect kDirtyRect{38, 136, 96, 48};
constexpr int kAlphaGridColumns = 8;
constexpr int kAlphaGridRows = 8;
constexpr int kAlphaGridTileWidth = 16;
constexpr int kAlphaGridTileHeight = 12;
constexpr int kAlphaGridOriginX = 6;
constexpr int kAlphaGridOriginY = 20;
constexpr int kAlphaGridStepX = 20;
constexpr int kAlphaGridStepY = 36;

enum class Workload {
    OpaqueFill,
    OpaqueDirtyFill,
    AlphaGrid,
    BitmapText,
    RoundedQualification,
    RoundedSupersampleQualification,
    RoundedQualityMasks,
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
    using RenderFlushFn = int(__cdecl*)(void*);

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
    RenderFlushFn render_flush = nullptr;
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
            render_flush = symbol<RenderFlushFn>("SDL_RenderFlush");
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

    void reset_alpha_grid() const {
        if (set_draw_blend_mode(renderer, 0) != 0 ||
            set_draw_color(renderer, 0, 0, 0, 255) != 0 ||
            render_clear(renderer) != 0 ||
            set_draw_blend_mode(renderer, 1) != 0 ||
            set_draw_color(renderer, kAlphaColor.r, kAlphaColor.g,
                           kAlphaColor.b, kAlphaColor.a) != 0) {
            fail("failed to reset SDL alpha-grid surface");
        }
        finish_alpha_grid();
    }

    void finish_alpha_grid() const {
        if (render_flush(renderer) != 0) fail("SDL_RenderFlush failed");
    }

    void alpha_fill(Rect rect) const {
        const SdlRect target{rect.x, rect.y, rect.width, rect.height};
        if (render_fill_rect(renderer, &target) != 0) fail("SDL alpha RenderFillRect failed");
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
    HDC alpha_source_dc = nullptr;
    HBITMAP alpha_source_bitmap = nullptr;
    HGDIOBJ previous_alpha_source_bitmap = nullptr;
    std::uint8_t* alpha_source_pixels = nullptr;
    std::uint8_t* pixels = nullptr;

    explicit GdiSurface(bool enable_alpha = false) {
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
        if (enable_alpha) {
            try {
                initialize_alpha_source();
            } catch (...) {
                if (previous_bitmap != nullptr) SelectObject(dc, previous_bitmap);
                if (brush != nullptr) DeleteObject(brush);
                if (bitmap != nullptr) DeleteObject(bitmap);
                if (dc != nullptr) DeleteDC(dc);
                previous_bitmap = nullptr;
                brush = nullptr;
                bitmap = nullptr;
                dc = nullptr;
                throw;
            }
        }
    }

    ~GdiSurface() {
        if (alpha_source_dc != nullptr && previous_alpha_source_bitmap != nullptr) {
            SelectObject(alpha_source_dc, previous_alpha_source_bitmap);
        }
        if (alpha_source_bitmap != nullptr) DeleteObject(alpha_source_bitmap);
        if (alpha_source_dc != nullptr) DeleteDC(alpha_source_dc);
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

    void reset_alpha_grid() const {
        if (PatBlt(dc, 0, 0, kWidth, kHeight, BLACKNESS) == 0 || GdiFlush() == 0) {
            throw std::runtime_error("failed to reset GDI alpha-grid surface");
        }
    }

    void finish_alpha_grid() const {
        if (GdiFlush() == 0) throw std::runtime_error("GDI alpha-grid flush failed");
    }

    void alpha_fill(Rect rect) const {
        if (alpha_source_dc == nullptr) {
            throw std::runtime_error("GDI alpha source was not initialized");
        }
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (AlphaBlend(dc, rect.x, rect.y, rect.width, rect.height,
                       alpha_source_dc, 0, 0, 1, 1, blend) == 0) {
            throw std::runtime_error("GDI AlphaBlend failed");
        }
    }

private:
    void initialize_alpha_source() {
        alpha_source_dc = CreateCompatibleDC(nullptr);
        if (alpha_source_dc == nullptr) {
            throw std::runtime_error("failed to create GDI alpha source DC");
        }
        BITMAPINFO source_info{};
        source_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        source_info.bmiHeader.biWidth = 1;
        source_info.bmiHeader.biHeight = -1;
        source_info.bmiHeader.biPlanes = 1;
        source_info.bmiHeader.biBitCount = 32;
        source_info.bmiHeader.biCompression = BI_RGB;
        void* source_pixels = nullptr;
        alpha_source_bitmap = CreateDIBSection(alpha_source_dc, &source_info, DIB_RGB_COLORS,
                                               &source_pixels, nullptr, 0);
        alpha_source_pixels = static_cast<std::uint8_t*>(source_pixels);
        if (alpha_source_bitmap == nullptr || alpha_source_pixels == nullptr) {
            DeleteDC(alpha_source_dc);
            alpha_source_dc = nullptr;
            throw std::runtime_error("failed to create GDI alpha source bitmap");
        }
        previous_alpha_source_bitmap = SelectObject(alpha_source_dc, alpha_source_bitmap);
        if (previous_alpha_source_bitmap == nullptr || previous_alpha_source_bitmap == HGDI_ERROR) {
            DeleteObject(alpha_source_bitmap);
            alpha_source_bitmap = nullptr;
            DeleteDC(alpha_source_dc);
            alpha_source_dc = nullptr;
            throw std::runtime_error("failed to select GDI alpha source bitmap");
        }
        alpha_source_pixels[0] = static_cast<std::uint8_t>((kAlphaColor.b * kAlphaColor.a + 127) / 255);
        alpha_source_pixels[1] = static_cast<std::uint8_t>((kAlphaColor.g * kAlphaColor.a + 127) / 255);
        alpha_source_pixels[2] = static_cast<std::uint8_t>((kAlphaColor.r * kAlphaColor.a + 127) / 255);
        alpha_source_pixels[3] = kAlphaColor.a;
    }
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
    if (name == "alpha-grid") return Workload::AlphaGrid;
    if (name == "bitmap-text") return Workload::BitmapText;
    if (name == "rounded-qualification") return Workload::RoundedQualification;
    if (name == "rounded-supersample-qualification") return Workload::RoundedSupersampleQualification;
    if (name == "rounded-quality-masks") return Workload::RoundedQualityMasks;
    if (name == "horizontal-gradient") return Workload::HorizontalGradient;
    if (name == "vertical-gradient") return Workload::VerticalGradient;
    throw std::runtime_error(
        "workload must be opaque-fill, opaque-dirty-fill, alpha-grid, bitmap-text, rounded-qualification, rounded-supersample-qualification, rounded-quality-masks, horizontal-gradient, or vertical-gradient");
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
    case Workload::AlphaGrid: return "alpha-grid-rgb-v2";
    case Workload::BitmapText: return "bitmap-clock-text-rgb-v1";
    case Workload::RoundedQualification: return "rounded-card-qualification-v0";
    case Workload::RoundedSupersampleQualification: return "rounded-supersample-qualification-v0";
    case Workload::RoundedQualityMasks: return "rounded-native-aa-quality-v0";
    case Workload::HorizontalGradient: return "horizontal-gradient-rgb-v1";
    case Workload::VerticalGradient: return "vertical-gradient-rgb-v1";
    }
    return "unknown";
}

bool is_fill_workload(Workload workload) {
    return workload == Workload::OpaqueFill || workload == Workload::OpaqueDirtyFill;
}

Rect workload_rect(Workload workload) {
    if (workload == Workload::BitmapText) return Rect{14, 20, 144, 20};
    return workload == Workload::OpaqueDirtyFill ? kDirtyRect : kFullRect;
}

Rect alpha_grid_rect(int index) {
    const int column = index % kAlphaGridColumns;
    const int row = index / kAlphaGridColumns;
    return Rect{kAlphaGridOriginX + column * kAlphaGridStepX,
                kAlphaGridOriginY + row * kAlphaGridStepY,
                kAlphaGridTileWidth,
                kAlphaGridTileHeight};
}

const char* workload_mode(Workload workload) {
    return workload == Workload::OpaqueDirtyFill || workload == Workload::AlphaGrid ||
        workload == Workload::BitmapText ? "dirty" : "full";
}

int workload_pixels(Workload workload) {
    if (workload == Workload::BitmapText) {
        int pixels = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                if (benchmark::clock_text_ink(x, y)) ++pixels;
            }
        }
        return pixels / benchmark::kClockLineCount;
    }
    const Rect rect = workload == Workload::AlphaGrid ? alpha_grid_rect(0) : workload_rect(workload);
    return rect.width * rect.height;
}

int workload_operations_per_sample(Workload workload) {
    if (workload == Workload::BitmapText) return benchmark::kClockLineCount;
    return workload == Workload::OpaqueDirtyFill || workload == Workload::AlphaGrid ? 64 : 1;
}

std::string workload_parameters_json(Workload workload) {
    const Rect rect = workload == Workload::AlphaGrid ? alpha_grid_rect(0) : workload_rect(workload);
    std::ostringstream output;
    output << "{\"surfaceInitialRgb\":\"000000\","
           << "\"rect\":{\"x\":" << rect.x << ",\"y\":" << rect.y
           << ",\"width\":" << rect.width << ",\"height\":" << rect.height << "},";
    if (workload == Workload::AlphaGrid) {
        output << "\"operation\":\"source-over-grid\","
               << "\"sourceRgba\":\"50b4dc80\","
               << "\"blend\":\"source-over\","
               << "\"timing\":\"completed-batch-per-tile\","
               << "\"sampleOrder\":\"alternating-pairs\","
               << "\"reset\":\"completed-before-timer\","
               << "\"columns\":" << kAlphaGridColumns << ','
               << "\"rows\":" << kAlphaGridRows << ','
               << "\"stepX\":" << kAlphaGridStepX << ','
               << "\"stepY\":" << kAlphaGridStepY;
    } else if (workload == Workload::BitmapText) {
        output << "\"operation\":\"bitmap-text-lines\","
               << "\"fontIdentity\":\"clock-5x7-v1\","
               << "\"fontMasksHex\":\"";
        for (std::size_t index = 0; index < benchmark::kClockFont.glyph_count; ++index) {
            if (index != 0) output << ':';
            for (const auto row : benchmark::kClockMasks[index]) {
                output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(row);
            }
        }
        output << std::dec << "\",\"codepoints\":[32,48,50,52,56,58],"
               << "\"fontSize\":14,\"fontWeight\":400,\"scale\":2,"
               << "\"lineHeight\":14,\"wrapWidth\":144,\"fallback\":\"forbidden\","
               << "\"glyphWidth\":5,\"glyphHeight\":7,\"advance\":6,"
               << "\"fontRasterizer\":\"shared-fixed-1bpp-mask-nearest\","
               << "\"lineBreakMode\":\"fixed-lines-no-wrap\",\"align\":\"center\","
               << "\"texts\":[\"08:42 20:48\",\"24:00 08:42\"],\"lineCount\":8,\"stepY\":32,"
               << "\"sourceRgb\":\"164757\",\"blend\":\"opaque-ink-transparent-background\","
               << "\"pixelAccounting\":\"mean-ink-pixels-per-line\","
               << "\"timing\":\"completed-batch-per-line\",\"sampleOrder\":\"alternating-pairs\","
               << "\"reset\":\"completed-before-timer\",\"atlasSetup\":\"excluded\","
               << "\"measurement\":\"shared-bitmap-advance-in-timer\"";
    } else if (is_fill_workload(workload)) {
        output << "\"operation\":\"fill-rect\","
               << "\"sourceRgb\":\"164757\","
               << "\"blend\":\"opaque-replace\"";
    } else {
        output << "\"operation\":\"linear-gradient\","
               << "\"firstRgb\":\"164757\","
               << "\"secondRgb\":\"06161f\","
               << "\"axis\":\""
               << (workload == Workload::VerticalGradient ? "vertical" : "horizontal") << "\","
               << "\"colorSpace\":\"srgb-byte-linear\","
               << "\"endpoint\":\"rect-exclusive\"";
    }
    output << '}';
    return output.str();
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

OutputComparison compare_output(const FrameBuffer& frame, const std::uint8_t* reference_pixels,
                                Workload workload) {
    OutputComparison comparison;
    comparison.jellyframe_digest = rgb_hash(frame);
    comparison.reference_digest = rgb_hash_bgra(reference_pixels);
    double squared_error = 0.0;
    const std::size_t pixel_count = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const Color actual = frame.pixels[index];
        const std::uint8_t* expected_bgra = reference_pixels + index * 4U;
        if (workload == Workload::BitmapText) {
            const bool ink = benchmark::clock_text_ink(static_cast<int>(index % kWidth),
                                                       static_cast<int>(index / kWidth));
            const Color expected = ink ? kFillColor : Color{0, 0, 0, 255};
            if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
                expected_bgra[2] != expected.r || expected_bgra[1] != expected.g || expected_bgra[0] != expected.b) {
                throw std::runtime_error("bitmap-text output failed independent pixel oracle");
            }
        }
        if (workload == Workload::AlphaGrid) {
            const int x = static_cast<int>(index % kWidth) - kAlphaGridOriginX;
            const int y = static_cast<int>(index / kWidth) - kAlphaGridOriginY;
            const bool in_tile = x >= 0 && y >= 0 &&
                x / kAlphaGridStepX < kAlphaGridColumns &&
                y / kAlphaGridStepY < kAlphaGridRows &&
                x % kAlphaGridStepX < kAlphaGridTileWidth &&
                y % kAlphaGridStepY < kAlphaGridTileHeight;
            const auto channel = [in_tile](int value) {
                return in_tile ? (value * kAlphaColor.a + 127) / 255 : 0;
            };
            if (actual.r != channel(kAlphaColor.r) || actual.g != channel(kAlphaColor.g) ||
                actual.b != channel(kAlphaColor.b) ||
                expected_bgra[2] != channel(kAlphaColor.r) ||
                expected_bgra[1] != channel(kAlphaColor.g) ||
                expected_bgra[0] != channel(kAlphaColor.b)) {
                throw std::runtime_error("alpha-grid output failed independent pixel oracle");
            }
        }
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

// Validate native coverage before allowing any rounded performance comparison.
// This intentionally emits a qualification report, never a benchmark manifest.
int qualify_native_rounded_card(const std::filesystem::path& directory) {
    constexpr Rect rect{14, 20, 144, 96};
    constexpr int radius = 24;
    FrameBuffer core(kWidth, kHeight, Color{0, 0, 0, 255});
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = kFillColor;
    command.border_radius = radius;
    SoftwareRasterizer{}.rasterize(command, core, rect);

    GdiSurface native;
    native.reset_alpha_grid();
    const auto old_brush = SelectObject(native.dc, native.brush);
    const auto old_pen = SelectObject(native.dc, GetStockObject(NULL_PEN));
    if (old_brush == nullptr || old_brush == HGDI_ERROR || old_pen == nullptr || old_pen == HGDI_ERROR) {
        if (old_brush != nullptr && old_brush != HGDI_ERROR) SelectObject(native.dc, old_brush);
        if (old_pen != nullptr && old_pen != HGDI_ERROR) SelectObject(native.dc, old_pen);
        throw std::runtime_error("GDI rounded qualification selection failed");
    }
    const bool drawn = RoundRect(native.dc, rect.x, rect.y, rect.x + rect.width,
                                rect.y + rect.height, radius * 2, radius * 2) != 0;
    SelectObject(native.dc, old_brush);
    SelectObject(native.dc, old_pen);
    if (!drawn) throw std::runtime_error("GDI RoundRect failed");
    native.finish_alpha_grid();

    FrameBuffer reference(kWidth, kHeight, Color{0, 0, 0, 255});
    FrameBuffer difference(kWidth, kHeight, Color{0, 0, 0, 255});
    int different_pixels = 0;
    int partial_coverage_pixels = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int coverage = benchmark::rounded_fixture_coverage(benchmark::kRoundedFixtures.front(), x, y);
            const Color expected = benchmark::rounded_fixture_color(kFillColor, coverage);
            const Color actual = core.pixel(x, y);
            if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b || actual.a != 255) {
                throw std::runtime_error("Core rounded output failed independent 4x4 oracle");
            }
            if (coverage > 0 && coverage < 255) ++partial_coverage_pixels;
            const auto* pixel = native.pixels + (static_cast<std::size_t>(y) * kWidth + x) * 4;
            reference.pixel(x, y) = Color{pixel[2], pixel[1], pixel[0], 255};
            if (actual.r != pixel[2] || actual.g != pixel[1] || actual.b != pixel[0]) {
                ++different_pixels;
                difference.pixel(x, y) = Color{255, 64, 64, 255};
            }
        }
    }
    const auto comparison = compare_output(core, native.pixels, Workload::RoundedQualification);
    std::filesystem::create_directories(directory);
    write_bmp(core, (directory / "core.bmp").string());
    write_bmp(reference, (directory / "gdi.bmp").string());
    write_bmp(difference, (directory / "difference.bmp").string());
    std::ofstream output(directory / "qualification.json", std::ios::binary);
    output << "{\n  \"format\":\"jellyframe.benchmark.qualification.v0\",\n"
           << "  \"workload\":\"rounded-card-qualification-v0\",\n"
           << "  \"status\":\"not-comparable\",\n  \"performanceMeasured\":false,\n"
           << "  \"reason\":\"Core 4x4 coverage and native GDI binary coverage are different contracts\",\n"
           << "  \"viewport\":{\"width\":172,\"height\":320},\n"
           << "  \"rect\":{\"x\":14,\"y\":20,\"width\":144,\"height\":96},\n"
           << "  \"radius\":24,\"sourceRgb\":\"164757\",\"backgroundRgb\":\"000000\",\n"
           << "  \"core\":{\"coverage\":\"4x4-quarter-pixel-grid\",\"oraclePassed\":true,\"digest\":\""
           << comparison.jellyframe_digest << "\"},\n"
           << "  \"gdi\":{\"coverage\":\"native-binary-RoundRect\",\"digest\":\""
           << comparison.reference_digest << "\"},\n"
           << "  \"differentPixels\":" << different_pixels << ",\n"
           << "  \"corePartialCoveragePixels\":" << partial_coverage_pixels << ",\n"
           << "  \"fullSurfaceRmse\":" << comparison.rmse << ",\n"
           << "  \"maxChannelError\":" << comparison.max_channel_error << "\n}\n";
    output.close();
    if (!output) throw std::runtime_error("rounded qualification report write failed");
    std::cout << "rounded_qualification=not-comparable different_pixels=" << different_pixels
              << " core_oracle=pass performance_measured=false\n";
    return 0;
}

int qualify_supersampled_rounded_cards(const std::filesystem::path& directory) {
    benchmark::GdiPlusSession gdiplus;
    std::filesystem::create_directories(directory);
    std::ostringstream cases;
    int failed_cases = 0;
    for (const auto& fixture : benchmark::kRoundedFixtures) {
        FrameBuffer core(kWidth, kHeight, Color{0, 0, 0, 255});
        FrameBuffer reference(kWidth, kHeight, Color{0, 0, 0, 255});
        FrameBuffer difference(kWidth, kHeight, Color{0, 0, 0, 255});
        DisplayCommand command;
        command.type = DisplayCommandType::FillRect;
        command.rect = fixture.rect;
        command.color = kFillColor;
        command.border_radius = fixture.radius;
        SoftwareRasterizer{}.rasterize(command, core, kFullRect);
        const auto coverage = benchmark::gdiplus_rounded_coverage(fixture, kWidth, kHeight);
        int coverage_mismatches = 0;
        int max_coverage_error = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const int expected_coverage = benchmark::rounded_fixture_coverage(fixture, x, y);
                const Color expected = benchmark::rounded_fixture_color(kFillColor, expected_coverage);
                const Color actual = core.pixel(x, y);
                if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b || actual.a != 255) {
                    throw std::runtime_error(std::string("Core rounded oracle failed: ") + fixture.name);
                }
                const int native_coverage = coverage[static_cast<std::size_t>(y) * kWidth + x];
                reference.pixel(x, y) = benchmark::rounded_fixture_color(kFillColor, native_coverage);
                if (expected_coverage != native_coverage) {
                    ++coverage_mismatches;
                    max_coverage_error = std::max(max_coverage_error, std::abs(expected_coverage - native_coverage));
                    difference.pixel(x, y) = Color{255, 64, 64, 255};
                }
            }
        }
        if (coverage_mismatches != 0) ++failed_cases;
        const auto case_directory = directory / fixture.name;
        std::filesystem::create_directories(case_directory);
        write_bmp(core, (case_directory / "core.bmp").string());
        write_bmp(reference, (case_directory / "gdiplus.bmp").string());
        write_bmp(difference, (case_directory / "difference.bmp").string());
        if (cases.tellp() > 0) cases << ",\n";
        cases << "    {\"name\":\"" << fixture.name << "\",\"rect\":{\"x\":" << fixture.rect.x
              << ",\"y\":" << fixture.rect.y << ",\"width\":" << fixture.rect.width
              << ",\"height\":" << fixture.rect.height << "},\"radius\":" << fixture.radius
              << ",\"coreOraclePassed\":true,\"coverageMismatches\":" << coverage_mismatches
              << ",\"maxCoverageError\":" << max_coverage_error
              << ",\"coreDigest\":\"" << rgb_hash(core) << "\",\"referenceDigest\":\""
              << rgb_hash(reference) << "\"}";
    }
    const char* status = failed_cases == 0 ? "output-qualified" : "not-comparable";
    std::ofstream output(directory / "qualification.json", std::ios::binary);
    output << "{\n  \"format\":\"jellyframe.benchmark.qualification.v0\",\n"
           << "  \"workload\":\"rounded-supersample-qualification-v0\",\n"
           << "  \"status\":\"" << status << "\",\n  \"performanceMeasured\":false,\n"
           << "  \"backend\":\"windows-gdiplus-supersampled-path\",\n"
           << "  \"viewport\":{\"width\":172,\"height\":320},\n"
           << "  \"requiredCoverage\":\"4x4-quarter-pixel-grid-exact\",\n"
           << "  \"adapter\":\"4x FillPath; SmoothingModeNone; PixelOffsetModeNone; 4x4 box reduction\",\n"
           << "  \"sourceRgb\":\"164757\",\"backgroundRgb\":\"000000\",\n"
           << "  \"failedCases\":" << failed_cases << ",\n  \"cases\":[\n" << cases.str() << "\n  ]\n}\n";
    output.close();
    if (!output) throw std::runtime_error("supersampled rounded report write failed");
    std::cout << "rounded_supersample_qualification=" << status << " failed_cases=" << failed_cases
              << " core_oracle=pass performance_measured=false\n";
    return 0;
}

int export_rounded_quality_masks(const std::filesystem::path& directory) {
    benchmark::GdiPlusSession gdiplus;
    std::filesystem::create_directories(directory);
    std::ofstream manifest(directory / "coverage.json", std::ios::binary);
    manifest << "{\"format\":\"jellyframe.rounded.coverage.v0\",\"fixtureSet\":\"rounded-uniform-v0\","
             << "\"viewport\":{\"width\":172,\"height\":320},\"performanceMeasured\":false,\"fixtures\":[";
    for (std::size_t index = 0; index < benchmark::kRoundedFixtures.size(); ++index) {
        const auto& fixture = benchmark::kRoundedFixtures[index];
        if (index) manifest << ',';
        manifest << "{\"name\":\"" << fixture.name << "\",\"rect\":[" << fixture.rect.x << ',' << fixture.rect.y
                 << ',' << fixture.rect.width << ',' << fixture.rect.height << "],\"radius\":" << fixture.radius << '}';
    }
    manifest << "],\"backends\":[";
    const char* names[]{"core-quarter-grid", "gdiplus-native-aa", "gdiplus-binary-control"};
    const char* methods[]{"Core 4x4 quarter-origin samples", "GDI+ AntiAlias; PixelOffsetHalf; SourceOver; AssumeLinear",
                          "GDI+ None; PixelOffsetHalf; SourceCopy"};
    for (int backend = 0; backend < 3; ++backend) {
        if (backend) manifest << ',';
        manifest << "{\"id\":\"" << names[backend] << "\",\"method\":\"" << methods[backend] << "\",\"cases\":[";
        std::filesystem::create_directories(directory / names[backend]);
        for (std::size_t index = 0; index < benchmark::kRoundedFixtures.size(); ++index) {
            const auto& fixture = benchmark::kRoundedFixtures[index];
            std::vector<int> coverage;
            if (backend == 0) {
                FrameBuffer frame(kWidth, kHeight, Color{0, 0, 0, 255});
                DisplayCommand command;
                command.type = DisplayCommandType::FillRect;
                command.rect = fixture.rect;
                command.border_radius = fixture.radius;
                command.color = Color{255, 255, 255, 255};
                SoftwareRasterizer{}.rasterize(command, frame, kFullRect);
                for (const auto pixel : frame.pixels) {
                    if (pixel.r != pixel.g || pixel.g != pixel.b || pixel.a != 255) {
                        throw std::runtime_error("Core quality mask is not opaque grayscale");
                    }
                    coverage.push_back(pixel.r);
                }
            } else {
                coverage = benchmark::gdiplus_rounded_coverage(fixture, kWidth, kHeight,
                    backend == 1 ? benchmark::GdiPlusRoundedMode::NativeAa : benchmark::GdiPlusRoundedMode::BinaryControl);
            }
            const std::string relative = std::string(names[backend]) + '/' + fixture.name + ".pgm";
            std::ofstream mask(directory / relative, std::ios::binary);
            mask << "P5\n172 320\n255\n";
            for (const int value : coverage) mask.put(static_cast<char>(value));
            mask.close();
            if (!mask) throw std::runtime_error("quality mask write failed");
            if (index) manifest << ',';
            manifest << "{\"name\":\"" << fixture.name << "\",\"mask\":\"" << relative << "\"}";
        }
        manifest << "]}";
    }
    manifest << "]}\n";
    manifest.close();
    if (!manifest) throw std::runtime_error("quality manifest write failed");
    std::cout << "rounded_quality_masks=exported backends=3 cases=8 performance_measured=false\n";
    return 0;
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
           << "  \"workloadParameters\": " << workload_parameters_json(workload) << ",\n"
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
        if (backend == Backend::Sdl2 && !is_fill_workload(workload) && workload != Workload::AlphaGrid) {
            throw std::runtime_error(
                "SDL2 adapter currently supports opaque-fill, opaque-dirty-fill, and alpha-grid");
        }
        if (backend == Backend::Gdi && argc == 6) {
            throw std::runtime_error("SDL2 library path is valid only with the sdl2 backend");
        }
        const std::filesystem::path output_directory(argv[1]);
        if (workload == Workload::RoundedQualityMasks) {
            if (samples != 1) throw std::runtime_error("rounded quality masks require samples=1; no timing is measured");
            return export_rounded_quality_masks(output_directory);
        }
        if (workload == Workload::RoundedQualification || workload == Workload::RoundedSupersampleQualification) {
            if (samples != 1) throw std::runtime_error("rounded qualification requires samples=1; no timing is measured");
            return workload == Workload::RoundedQualification ? qualify_native_rounded_card(output_directory)
                : qualify_supersampled_rounded_cards(output_directory);
        }

        FrameBuffer jellyframe_surface(kWidth, kHeight, Color{0, 0, 0, 255});
        DisplayCommand command;
        command.type = is_fill_workload(workload) || workload == Workload::AlphaGrid
            ? DisplayCommandType::FillRect
            : DisplayCommandType::LinearGradient;
        command.rect = workload == Workload::AlphaGrid ? alpha_grid_rect(0) : workload_rect(workload);
        command.color = workload == Workload::AlphaGrid
            ? kAlphaColor
            : (is_fill_workload(workload) ? kFillColor : kGradientFirst);
        command.color2 = kGradientSecond;
        command.gradient_axis = workload == Workload::VerticalGradient
            ? GradientAxis::Vertical
            : GradientAxis::Horizontal;
        BitmapFontContext font_context{&benchmark::kClockFont, benchmark::kClockScale};
        TextPainter text_painter{bitmap_font_paint_callback, &font_context, nullptr, true};
        SoftwareRasterizer rasterizer(workload == Workload::BitmapText ? text_painter : TextPainter{});
        const auto text_commands = benchmark::clock_text_commands(kFillColor);
        const int operations_per_sample = workload_operations_per_sample(workload);
        std::vector<double> jellyframe_samples;
        const auto reset_jellyframe = [&] {
            std::fill(jellyframe_surface.pixels.begin(), jellyframe_surface.pixels.end(),
                      Color{0, 0, 0, 255});
        };
        const auto paint_jellyframe = [&](int operation) {
            command.rect = alpha_grid_rect(operation);
            rasterizer.rasterize(command, jellyframe_surface, command.rect);
        };
        if (workload != Workload::AlphaGrid && workload != Workload::BitmapText) {
            jellyframe_samples = measure(samples, operations_per_sample, [&] {
                rasterizer.rasterize(command, jellyframe_surface, command.rect);
            });
        }

        OutputComparison comparison;
        std::vector<double> reference_samples;
        std::string reference_name;
        std::string reference_version;
        std::string reference_file;
        if (backend == Backend::Gdi) {
            GdiSurface gdi_surface(workload == Workload::AlphaGrid);
            if (workload == Workload::BitmapText) {
                GdiSurface atlas;
                std::fill_n(atlas.pixels, static_cast<std::size_t>(kWidth) * kHeight * 4, 0);
                for (std::size_t index = 0; index < benchmark::kClockFont.glyph_count; ++index) {
                    const auto& glyph = benchmark::kClockGlyphs[index];
                    for (int y = 0; y < 7; ++y) {
                        for (int x = 0; x < 5; ++x) {
                            if ((glyph.rows[y] & (0x80U >> x)) == 0) continue;
                            auto* pixel = atlas.pixels + (y * kWidth + index * 5 + x) * 4;
                            pixel[0] = kFillColor.b;
                            pixel[1] = kFillColor.g;
                            pixel[2] = kFillColor.r;
                        }
                    }
                }
                auto pair = benchmark::measure_interleaved(samples, kWarmupIterations,
                    operations_per_sample, reset_jellyframe,
                    [&](int line) {
                        const auto& text = text_commands[static_cast<std::size_t>(line)];
                        rasterizer.rasterize(text, jellyframe_surface, text.rect);
                    }, [] {},
                    [&] { gdi_surface.reset_alpha_grid(); },
                    [&](int line) {
                        const auto& text = text_commands[static_cast<std::size_t>(line)];
                        const auto metrics = measure_bitmap_text(font_context, text.text, 14, 400);
                        int x = text.rect.x + (text.rect.width - metrics.width) / 2;
                        const int y = text.rect.y + (text.rect.height - metrics.line_height) / 2;
                        for (const unsigned char ch : text.text) {
                            const auto* glyph = find_bitmap_glyph(benchmark::kClockFont, ch);
                            if (glyph == nullptr) throw std::runtime_error("bitmap-text missing glyph");
                            const int atlas_x = static_cast<int>(glyph - benchmark::kClockGlyphs) * 5;
                            if (!TransparentBlt(gdi_surface.dc, x, y, 10, 14, atlas.dc,
                                                atlas_x, 0, 5, 7, RGB(0, 0, 0))) {
                                throw std::runtime_error("GDI bitmap glyph blit failed");
                            }
                            x += glyph->advance * benchmark::kClockScale;
                        }
                    }, [&] { gdi_surface.finish_alpha_grid(); });
                jellyframe_samples = std::move(pair.first);
                reference_samples = std::move(pair.second);
            } else if (workload == Workload::AlphaGrid) {
                auto pair = benchmark::measure_interleaved(samples, kWarmupIterations,
                    operations_per_sample, reset_jellyframe, paint_jellyframe, [] {},
                    [&] { gdi_surface.reset_alpha_grid(); },
                    [&](int operation) { gdi_surface.alpha_fill(alpha_grid_rect(operation)); },
                    [&] { gdi_surface.finish_alpha_grid(); });
                jellyframe_samples = std::move(pair.first);
                reference_samples = std::move(pair.second);
            } else {
                reference_samples = measure(samples, operations_per_sample, [&] {
                    if (is_fill_workload(workload)) gdi_surface.fill(workload_rect(workload));
                    else if (workload == Workload::HorizontalGradient) gdi_surface.horizontal_gradient();
                    else gdi_surface.vertical_gradient();
                });
            }
            comparison = compare_output(jellyframe_surface, gdi_surface.pixels, workload);
            reference_name = workload == Workload::BitmapText ? "windows-gdi-bitmap-glyph-blit" : "windows-gdi";
            reference_version = "system";
            reference_file = "gdi.json";
        } else {
            const std::filesystem::path library_path = argc == 6
                ? std::filesystem::path(argv[5])
                : std::filesystem::path(L"SDL2.dll");
            SdlSurface sdl_surface(library_path);
            if (workload == Workload::AlphaGrid) {
                auto pair = benchmark::measure_interleaved(samples, kWarmupIterations,
                    operations_per_sample, reset_jellyframe, paint_jellyframe, [] {},
                    [&] { sdl_surface.reset_alpha_grid(); },
                    [&](int operation) { sdl_surface.alpha_fill(alpha_grid_rect(operation)); },
                    [&] { sdl_surface.finish_alpha_grid(); });
                jellyframe_samples = std::move(pair.first);
                reference_samples = std::move(pair.second);
            } else {
                reference_samples = measure(samples, operations_per_sample, [&] {
                    sdl_surface.fill(workload_rect(workload));
                });
            }
            comparison = compare_output(jellyframe_surface, sdl_surface.pixels(), workload);
            reference_name = "sdl2-software-renderer";
            reference_version = sdl_surface.version;
            reference_file = "sdl2.json";
        }
        const double tolerance = is_fill_workload(workload) || workload == Workload::AlphaGrid ||
            workload == Workload::BitmapText ? 0.0 : 1.0;
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
