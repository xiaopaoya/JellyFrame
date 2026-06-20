#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_runtime/app_host.h"
#include "app_runtime/app_services.h"
#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/display_invalidation.h"
#include "render_core/diagnostics.h"
#include "render_core/dirty_region.h"
#include "render_core/document_script.h"
#include "render_core/document_style.h"
#include "render_core/frame_update.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/style.h"

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "app_registry.h"
#include "app_package.h"
#include "example_css_io.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {

constexpr std::size_t kMaxInputBytes = 1024 * 1024;
constexpr wchar_t kWindowClassName[] = L"JellyFrameWin32Browser";
constexpr UINT_PTR kScriptTimerId = 1;
constexpr UINT kScriptTimerPeriodMs = 16;
constexpr int kIncrementalDirtyAreaLimitPercent = 70;
constexpr const char* kDefaultLauncherAppPath = "samples/apps/system/sample_launcher";
constexpr const char* kLauncherStatusMarker = "<!-- JELLYFRAME_STATUS -->";
constexpr const char* kLauncherAppListMarker = "<!-- JELLYFRAME_APP_LIST -->";
constexpr int kMaxDebugPackageImageWidth = 256;
constexpr int kMaxDebugPackageImageHeight = 256;
constexpr std::size_t kMaxDebugPackageImageDecodedBytes =
    static_cast<std::size_t>(kMaxDebugPackageImageWidth) * kMaxDebugPackageImageHeight * 4U;

std::uint16_t pack_rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void append_rgb565(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

std::vector<std::uint8_t> make_debug_rgb565_surface(int width, int height, Color top, Color bottom) {
    std::vector<std::uint8_t> pixels;
    if (width <= 0 || height <= 0) {
        return pixels;
    }
    pixels.reserve(static_cast<std::size_t>(width * height * 2));
    const int denom = std::max(1, height - 1);
    for (int y = 0; y < height; ++y) {
        const int t = (y * 255) / denom;
        const auto lerp = [&](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>((static_cast<int>(a) * (255 - t) + static_cast<int>(b) * t + 127) / 255);
        };
        const std::uint16_t color = pack_rgb565(lerp(top.r, bottom.r), lerp(top.g, bottom.g), lerp(top.b, bottom.b));
        for (int x = 0; x < width; ++x) {
            append_rgb565(pixels, color);
        }
    }
    return pixels;
}

void add_debug_image_fixtures(ImageDecodeMock& images) {
    images.add_fixture(ImageDecodeFixture{
        "app://icon",
        48,
        48,
        48,
        HostPixelFormat::Rgb565,
        make_debug_rgb565_surface(48, 48, Color{37, 99, 235, 255}, Color{14, 165, 233, 255}),
    });
    images.add_fixture(ImageDecodeFixture{
        "app://photo",
        120,
        80,
        120,
        HostPixelFormat::Rgb565,
        make_debug_rgb565_surface(120, 80, Color{251, 191, 36, 255}, Color{244, 63, 94, 255}),
    });
}

std::int32_t read_i32_le(const std::uint8_t* data) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U));
}

bool decode_bmp_to_fixture(const std::string& url,
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
    if (dib_size < 40 || size < 14U + dib_size || pixel_offset >= size) {
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
                          "Only uncompressed 24-bit and 32-bit BMP package images are supported in the Win32 debug shell",
                          url);
        return false;
    }
    const int height = std::abs(signed_height);
    if (width > kMaxDebugPackageImageWidth || height > kMaxDebugPackageImageHeight) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP exceeds the Win32 debug shell image budget",
                          url);
        return false;
    }
    const std::size_t decoded_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (decoded_bytes > kMaxDebugPackageImageDecodedBytes) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP decoded bytes exceed the Win32 debug shell image budget",
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

void collect_image_sources(const Node& node, std::vector<std::string>& sources) {
    if (node.type == NodeType::Element && node.tag_name == "img") {
        const std::string& src = node.attribute("src");
        if (!src.empty() && std::find(sources.begin(), sources.end(), src) == sources.end()) {
            sources.push_back(src);
        }
    }
    for (const auto& child : node.children) {
        collect_image_sources(*child, sources);
    }
}

void add_package_image_fixtures(const Node& document,
                                jellyframe_example::PackageResourceContext& package_context,
                                ImageDecodeMock& images,
                                DiagnosticSink* diagnostics) {
    std::vector<std::string> sources;
    collect_image_sources(document, sources);
    for (const std::string& source : sources) {
        std::string bytes;
        if (!jellyframe_example::load_package_resource(source, package_context.base_url, bytes, &package_context)) {
            continue;
        }
        ImageDecodeFixture fixture;
        if (decode_bmp_to_fixture(source, bytes, fixture, diagnostics)) {
            images.add_fixture(std::move(fixture));
        }
    }
}

void collect_image_handles(const LayerNode& layer, std::vector<std::uint32_t>& handles) {
    for (const DisplayCommand& command : layer.display_list) {
        if (command.type != DisplayCommandType::Image || command.image_handle == 0) {
            continue;
        }
        if (std::find(handles.begin(), handles.end(), command.image_handle) == handles.end()) {
            handles.push_back(command.image_handle);
        }
    }
    for (const auto& child : layer.children) {
        collect_image_handles(*child, handles);
    }
}

const char* app_submit_status_name(AppServiceSubmitStatus status) {
    switch (status) {
    case AppServiceSubmitStatus::Accepted:
        return "accepted";
    case AppServiceSubmitStatus::EmptyInstance:
        return "empty-instance";
    case AppServiceSubmitStatus::CapabilityDenied:
        return "capability-denied";
    case AppServiceSubmitStatus::InvalidInput:
        return "invalid-input";
    case AppServiceSubmitStatus::QueueFull:
        return "queue-full";
    case AppServiceSubmitStatus::BudgetExceeded:
        return "budget-exceeded";
    }
    return "unknown";
}

const char* host_service_status_name(HostServiceStatus status) {
    switch (status) {
    case HostServiceStatus::Completed:
        return "completed";
    case HostServiceStatus::Failed:
        return "failed";
    case HostServiceStatus::Cancelled:
        return "cancelled";
    case HostServiceStatus::Unsupported:
        return "unsupported";
    case HostServiceStatus::BudgetExceeded:
        return "budget-exceeded";
    case HostServiceStatus::Timeout:
        return "timeout";
    }
    return "unknown";
}

std::string image_diagnostic_detail(const std::string& src,
                                    const char* status_kind,
                                    const char* status,
                                    HostServiceStatus host_status = HostServiceStatus::Completed,
                                    std::uint32_t error_code = 0) {
    std::ostringstream stream;
    stream << "src=" << src << "; " << status_kind << '=' << status;
    if (host_status != HostServiceStatus::Completed) {
        stream << "; host=" << host_service_status_name(host_status);
    }
    if (error_code != 0) {
        stream << "; error=" << error_code;
    }
    return stream.str();
}

void report_image_request_failure(DiagnosticSink* diagnostics,
                                  const std::string& src,
                                  AppServiceSubmitStatus submit_status,
                                  HostServiceStatus host_status) {
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Warning,
                      "image-decode-request",
                      "Image decode request was rejected",
                      image_diagnostic_detail(src,
                                              "submit",
                                              app_submit_status_name(submit_status),
                                              host_status));
}

void report_image_completion_failure(DiagnosticSink* diagnostics,
                                     const HostServiceCompletion& completion,
                                     const std::string& src) {
    if (completion.kind != HostServiceJobKind::ImageDecode ||
        completion.status == HostServiceStatus::Completed) {
        return;
    }
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Warning,
                      "image-decode-completion",
                      "Image decode did not produce a drawable surface",
                      image_diagnostic_detail(src.empty() ? "unknown" : src,
                                              "status",
                                              host_service_status_name(completion.status),
                                              completion.status,
                                              completion.error_code));
}

struct BrowserOptions {
    bool capture = false;
    std::string output_path;
    std::string html_path = "src/render_core/samples/pages/modern/app_shell.html";
    std::string css_path = "src/render_core/samples/pages/modern/app_shell.css";
    std::string inline_html;
    std::string inline_css;
    std::string script_path;
    std::string app_path;
    std::string registry_store_path;
    std::string launcher_app_path = kDefaultLauncherAppPath;
    std::string install_bundle_path;
    std::string launch_app_id;
    std::string remove_app_id;
    std::string startup_status;
    int viewport_width = 390;
    int viewport_height = 640;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
    bool use_app_fonts = false;
};

struct LoadedPage {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    std::filesystem::path script_base_dir;
    jellyframe_example::PackageResourceContext package_context;
    jellyframe_example::PackageResourceStats package_stats;
    bool package_mode = false;

    LoadedPage() = default;
    LoadedPage(const LoadedPage&) = delete;
    LoadedPage& operator=(const LoadedPage&) = delete;

    LoadedPage(LoadedPage&& other) noexcept
        : document(std::move(other.document)),
          stylesheet(std::move(other.stylesheet)),
          script_base_dir(std::move(other.script_base_dir)),
          package_context(std::move(other.package_context)),
          package_stats(other.package_stats),
          package_mode(other.package_mode) {
        if (package_context.stats == &other.package_stats) {
            package_context.stats = &package_stats;
        }
        other.package_context.stats = nullptr;
    }

    LoadedPage& operator=(LoadedPage&& other) noexcept {
        if (this != &other) {
            document = std::move(other.document);
            stylesheet = std::move(other.stylesheet);
            script_base_dir = std::move(other.script_base_dir);
            package_context = std::move(other.package_context);
            package_stats = other.package_stats;
            package_mode = other.package_mode;
            if (package_context.stats == &other.package_stats) {
                package_context.stats = &package_stats;
            }
            other.package_context.stats = nullptr;
        }
        return *this;
    }
};

HostBudgets desktop_browser_budgets() {
    HostBudgets budgets;
    budgets.max_resource_bytes = kMaxInputBytes;
    budgets.max_input_events_per_frame = 64;
    budgets.max_timer_callbacks_per_frame = 32;
    budgets.max_framebuffer_pixels = 2400 * 2400;
    return budgets;
}

std::string read_file_limited(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < kMaxInputBytes) {
        const std::size_t remaining = kMaxInputBytes - total;
        const std::size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        file.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize read = file.gcount();
        if (read <= 0) {
            break;
        }
        output.write(buffer, read);
        total += static_cast<std::size_t>(read);
    }
    return output.str();
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        wide.data(), required);
    return wide;
}

std::string wide_char_to_utf8(wchar_t ch) {
    const int required = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::string describe_node(const Node* node) {
    if (node == nullptr) {
        return "(none)";
    }
    if (node->type == NodeType::Text) {
        return "#text";
    }

    std::string label = node->tag_name.empty() ? "(element)" : node->tag_name;
    const std::string& id = node->attribute("id");
    if (!id.empty()) {
        label += "#" + id;
    }
    const std::string& classes = node->attribute("class");
    if (!classes.empty()) {
        label += "." + classes;
    }
    return label;
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

void blend_pixel(FrameBuffer& target, int x, int y, Color source) {
    if (!target.contains(x, y) || source.a == 0) {
        return;
    }
    Color& destination = target.pixel(x, y);
    if (source.a == 255) {
        destination = source;
        return;
    }

    const int src_a = source.a;
    const int dst_a = destination.a;
    const int inv_src_a = 255 - src_a;
    const int out_a = src_a + ((dst_a * inv_src_a + 127) / 255);
    if (out_a == 0) {
        destination = Color{0, 0, 0, 0};
        return;
    }

    const auto blend_channel = [&](std::uint8_t src, std::uint8_t dst) {
        const int premul = src * src_a + ((dst * dst_a * inv_src_a + 127) / 255);
        return clamp_u8((premul + out_a / 2) / out_a);
    };

    destination = Color{
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        clamp_u8(out_a),
    };
}

bool draw_text_with_gdi(FrameBuffer& target,
                        Rect rect,
                        Color color,
                        const std::string& text,
                        int font_size,
                        int font_weight,
                        TextCommandAlign align,
                        bool single_line,
                        void*) {
    const std::wstring wide = utf8_to_wide(text);
    if (wide.empty() || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return false;
    }

    const int bitmap_width = rect.width + std::max(4, rect.height / 8);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bitmap_width;
    info.bmiHeader.biHeight = -rect.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (memory_dc == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        return false;
    }

    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    RECT bounds{0, 0, bitmap_width, rect.height};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory_dc, &bounds, black);
    DeleteObject(black);

    const int font_height = -std::max(8, font_size);
    const int gdi_weight = font_weight >= 600 ? FW_BOLD : FW_NORMAL;
    HFONT font = CreateFontW(font_height, 0, 0, 0, gdi_weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    HGDIOBJ old_font = font != nullptr ? SelectObject(memory_dc, font) : nullptr;
    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, RGB(255, 255, 255));
    UINT flags = DT_NOPREFIX;
    if (align == TextCommandAlign::Center) {
        flags |= DT_CENTER;
    } else if (align == TextCommandAlign::End) {
        flags |= DT_RIGHT;
    } else {
        flags |= DT_LEFT;
    }
    flags |= single_line ? (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) : DT_WORDBREAK;
    DrawTextW(memory_dc, wide.c_str(), static_cast<int>(wide.size()), &bounds, flags);

    const auto* pixels = static_cast<const std::uint32_t*>(bits);
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            const std::uint32_t pixel = pixels[static_cast<std::size_t>(y * bitmap_width + x)];
            const int blue = static_cast<int>(pixel & 0xffU);
            const int green = static_cast<int>((pixel >> 8U) & 0xffU);
            const int red = static_cast<int>((pixel >> 16U) & 0xffU);
            const int coverage = std::max(red, std::max(green, blue));
            if (coverage == 0) {
                continue;
            }
            Color source = color;
            source.a = clamp_u8((static_cast<int>(source.a) * coverage + 127) / 255);
            blend_pixel(target, rect.x + x, rect.y + y, source);
        }
    }

    if (old_font != nullptr) {
        SelectObject(memory_dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    return true;
}

struct BrowserImageContext {
    ImageDecodeMock* images = nullptr;
};

struct BrowserImageResolveContext {
    AppRuntimeHost* runtime = nullptr;
    ImageDecodeMock* images = nullptr;
    AppImageSurfaceCache* cache = nullptr;
    DiagnosticSink* diagnostics = nullptr;
};

bool resolve_browser_image_handle(const Node& node, std::uint32_t& handle, void* raw_context) {
    auto* context = static_cast<BrowserImageResolveContext*>(raw_context);
    if (context == nullptr || context->runtime == nullptr || context->images == nullptr ||
        context->cache == nullptr || node.type != NodeType::Element || node.tag_name != "img") {
        return false;
    }
    const std::string src = node.attribute("src");
    const AppImageSurfaceState previous_state = context->cache->state_for_url(src);
    const bool resolved = context->cache->resolve_or_request(*context->runtime, *context->images, src, &handle);
    const AppServiceSubmitStatus submit_status = context->cache->last_submit_status_for_url(src);
    const AppImageSurfaceState current_state = context->cache->state_for_url(src);
    if (!resolved && submit_status != AppServiceSubmitStatus::Accepted &&
        previous_state != AppImageSurfaceState::Failed &&
        current_state != AppImageSurfaceState::Pending) {
        report_image_request_failure(context->diagnostics,
                                     src,
                                     submit_status,
                                     context->cache->last_host_status_for_url(src));
    }
    return resolved;
}

Color read_surface_pixel(const AppDecodedSurfaceRecord& surface, int x, int y) {
    if (x < 0 || y < 0 || x >= surface.width || y >= surface.height ||
        surface.stride_pixels < surface.width) {
        return Color{0, 0, 0, 0};
    }
    const std::size_t index = static_cast<std::size_t>(y * surface.stride_pixels + x);
    if (surface.pixel_format == HostPixelFormat::Rgb565) {
        const std::size_t byte_index = index * 2;
        if (byte_index + 1 >= surface.pixels.size()) {
            return Color{0, 0, 0, 0};
        }
        const std::uint16_t packed =
            static_cast<std::uint16_t>(surface.pixels[byte_index] |
                                      (static_cast<std::uint16_t>(surface.pixels[byte_index + 1]) << 8U));
        const std::uint8_t r5 = static_cast<std::uint8_t>((packed >> 11U) & 0x1fU);
        const std::uint8_t g6 = static_cast<std::uint8_t>((packed >> 5U) & 0x3fU);
        const std::uint8_t b5 = static_cast<std::uint8_t>(packed & 0x1fU);
        return Color{
            static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U)),
            static_cast<std::uint8_t>((g6 << 2U) | (g6 >> 4U)),
            static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U)),
            255,
        };
    }
    if (surface.pixel_format == HostPixelFormat::Rgba8888) {
        const std::size_t byte_index = index * 4;
        if (byte_index + 3 >= surface.pixels.size()) {
            return Color{0, 0, 0, 0};
        }
        return Color{
            surface.pixels[byte_index],
            surface.pixels[byte_index + 1],
            surface.pixels[byte_index + 2],
            surface.pixels[byte_index + 3],
        };
    }
    return Color{0, 0, 0, 0};
}

struct ImageDrawMapping {
    Rect dst;
    Rect src;
};

ImageDrawMapping map_image_draw_rect(Rect rect, int source_width, int source_height, ObjectFit fit) {
    if (rect.width <= 0 || rect.height <= 0 || source_width <= 0 || source_height <= 0) {
        return ImageDrawMapping{};
    }
    if (fit == ObjectFit::Fill) {
        return ImageDrawMapping{rect, Rect{0, 0, source_width, source_height}};
    }

    auto centered = [](Rect outer, int width, int height) {
        return Rect{
            outer.x + (outer.width - width) / 2,
            outer.y + (outer.height - height) / 2,
            width,
            height,
        };
    };

    if (fit == ObjectFit::None ||
        (fit == ObjectFit::ScaleDown && source_width <= rect.width && source_height <= rect.height)) {
        const int dst_width = std::min(source_width, rect.width);
        const int dst_height = std::min(source_height, rect.height);
        const int src_x = std::max(0, (source_width - dst_width) / 2);
        const int src_y = std::max(0, (source_height - dst_height) / 2);
        return ImageDrawMapping{
            centered(rect, dst_width, dst_height),
            Rect{src_x, src_y, dst_width, dst_height},
        };
    }

    const bool contain = fit == ObjectFit::Contain || fit == ObjectFit::ScaleDown;
    const long long lhs = static_cast<long long>(rect.width) * source_height;
    const long long rhs = static_cast<long long>(rect.height) * source_width;
    const bool limit_by_width = contain ? lhs <= rhs : lhs >= rhs;
    if (limit_by_width) {
        const int dst_width = rect.width;
        const int dst_height = std::max(1, static_cast<int>(
            (static_cast<long long>(source_height) * rect.width + source_width / 2) / source_width));
        return ImageDrawMapping{
            centered(rect, dst_width, dst_height),
            Rect{0, 0, source_width, source_height},
        };
    }
    const int dst_height = rect.height;
    const int dst_width = std::max(1, static_cast<int>(
        (static_cast<long long>(source_width) * rect.height + source_height / 2) / source_height));
    return ImageDrawMapping{
        centered(rect, dst_width, dst_height),
        Rect{0, 0, source_width, source_height},
    };
}

bool paint_image_surface(FrameBuffer& target,
                         Rect rect,
                         std::uint32_t image_handle,
                         ObjectFit object_fit,
                         void* raw_context) {
    auto* context = static_cast<BrowserImageContext*>(raw_context);
    if (context == nullptr || context->images == nullptr || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const AppDecodedSurfaceRecord* surface = context->images->surface(image_handle);
    if (surface == nullptr || surface->width <= 0 || surface->height <= 0 || surface->pixels.empty()) {
        return false;
    }
    const ImageDrawMapping mapping = map_image_draw_rect(rect, surface->width, surface->height, object_fit);
    if (mapping.dst.width <= 0 || mapping.dst.height <= 0 || mapping.src.width <= 0 || mapping.src.height <= 0) {
        return false;
    }
    const Rect clip{
        std::max(rect.x, mapping.dst.x),
        std::max(rect.y, mapping.dst.y),
        std::min(rect.x + rect.width, mapping.dst.x + mapping.dst.width) - std::max(rect.x, mapping.dst.x),
        std::min(rect.y + rect.height, mapping.dst.y + mapping.dst.height) - std::max(rect.y, mapping.dst.y),
    };
    if (clip.width <= 0 || clip.height <= 0) {
        return true;
    }
    for (int y = 0; y < clip.height; ++y) {
        const int dst_y = clip.y + y;
        const int local_y = dst_y - mapping.dst.y;
        const int src_y = std::min(surface->height - 1,
                                   mapping.src.y + (local_y * mapping.src.height) / std::max(1, mapping.dst.height));
        for (int x = 0; x < clip.width; ++x) {
            const int dst_x = clip.x + x;
            if (!target.contains(dst_x, dst_y)) {
                continue;
            }
            const int local_x = dst_x - mapping.dst.x;
            const int src_x = std::min(surface->width - 1,
                                       mapping.src.x + (local_x * mapping.src.width) / std::max(1, mapping.dst.width));
            blend_pixel(target, dst_x, dst_y, read_surface_pixel(*surface, src_x, src_y));
        }
    }
    return true;
}

HFONT create_gdi_text_font(int font_size, int font_weight) {
    const int font_height = -std::max(8, font_size);
    const int gdi_weight = font_weight >= 600 ? FW_BOLD : FW_NORMAL;
    return CreateFontW(font_height, 0, 0, 0, gdi_weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

bool measure_text_with_gdi(const std::string& text,
                           int font_size,
                           int font_weight,
                           TextMetrics* metrics,
                           void*) {
    if (metrics == nullptr) {
        return false;
    }
    const std::wstring wide = utf8_to_wide(text);
    if (!text.empty() && wide.empty()) {
        return false;
    }

    HDC dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }
    HFONT font = create_gdi_text_font(font_size, font_weight);
    HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;

    SIZE size{0, 0};
    bool ok = true;
    if (!wide.empty() &&
        GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &size) == 0) {
        ok = false;
    }
    TEXTMETRICW text_metric{};
    if (GetTextMetricsW(dc, &text_metric) == 0) {
        ok = false;
    }

    if (ok) {
        metrics->width = std::max(0L, size.cx) + (wide.empty() ? 0 : std::max(2, font_size / 4));
        metrics->line_height = std::max(1L, text_metric.tmHeight + text_metric.tmExternalLeading) +
            std::max(2, font_size / 6);
    }

    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    ReleaseDC(nullptr, dc);
    return ok;
}

struct BrowserTextBackend {
    TextMeasureProvider measure;
    TextPainter painter;
};

BrowserTextBackend make_browser_text_backend(const BrowserOptions& options, AppRuntimeHost* runtime) {
    if (options.use_app_fonts && runtime != nullptr && runtime->fonts().primary_font() != nullptr) {
        return BrowserTextBackend{runtime->fonts().measure_provider(), runtime->fonts().painter()};
    }
    return BrowserTextBackend{
        TextMeasureProvider{measure_text_with_gdi, nullptr},
        TextPainter{draw_text_with_gdi, nullptr},
    };
}

InputModifiers modifiers_from_keys(WPARAM wparam) {
    InputModifiers modifiers;
    modifiers.shift = (wparam & MK_SHIFT) != 0;
    modifiers.ctrl = (wparam & MK_CONTROL) != 0;
    modifiers.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return modifiers;
}

int buttons_from_keys(WPARAM wparam) {
    int buttons = 0;
    if ((wparam & MK_LBUTTON) != 0) {
        buttons |= 1;
    }
    if ((wparam & MK_RBUTTON) != 0) {
        buttons |= 2;
    }
    if ((wparam & MK_MBUTTON) != 0) {
        buttons |= 4;
    }
    return buttons;
}

std::uint32_t color_to_bgrx(Color pixel) {
    if (pixel.a != 255) {
        pixel = Color{
            clamp_u8((pixel.r * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.g * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.b * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            255,
        };
    }
    return (static_cast<std::uint32_t>(pixel.r) << 16U) |
           (static_cast<std::uint32_t>(pixel.g) << 8U) |
           static_cast<std::uint32_t>(pixel.b);
}

int parse_int_arg(const char* value, int fallback) {
    try {
        return std::max(1, std::stoi(value));
    } catch (...) {
        return fallback;
    }
}

const Node* find_first_element(const Node& node, const char* tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        const Node* found = find_first_element(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Color page_background_color(const Node& document, const StyleResolver& resolver) {
    const Node* body = find_first_element(document, "body");
    if (body != nullptr) {
        const Style style = resolver.resolve(*body);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    const Node* html = find_first_element(document, "html");
    if (html != nullptr) {
        const Style style = resolver.resolve(*html);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    return Color{255, 255, 255, 255};
}

void print_diagnostics(const VectorDiagnosticSink& diagnostics) {
    if (diagnostics.empty()) {
        std::cout << "diagnostics: 0\n";
        return;
    }
    std::cout << "diagnostics: " << diagnostics.size() << '\n';
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cout << "  [" << diagnostic_severity_name(diagnostic.severity) << "] "
                  << diagnostic_stage_name(diagnostic.stage) << "::" << diagnostic.code
                  << " - " << diagnostic.message;
        if (!diagnostic.detail.empty()) {
            std::cout << " (" << diagnostic.detail << ')';
        }
        std::cout << '\n';
    }
}

std::string html_escape_text(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

bool replace_once(std::string& text, std::string_view marker, std::string_view replacement) {
    const std::size_t pos = text.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    text.replace(pos, marker.size(), replacement);
    return true;
}

std::string build_launcher_app_list_html(const std::filesystem::path& registry_store) {
    const jellyframe_example::InstalledAppRegistry registry =
        jellyframe_example::load_installed_app_registry(registry_store);
    std::ostringstream html;
    if (registry.apps.empty()) {
        html << "<section class='empty'><p>No installed apps.</p>"
             << "<p class='hint'>Use --install-bundle app.jfapp with --registry-store.</p></section>";
    }
    for (const jellyframe_example::InstalledAppEntry& app : registry.apps) {
        html << "<article class='app'>"
             << "<h2 class='name'>" << html_escape_text(app.name) << "</h2>"
             << "<p class='meta'>" << html_escape_text(app.id) << " - v"
             << html_escape_text(app.version_name) << " - "
             << app.bundle_size << " bytes</p>"
             << "<div class='actions'>"
             << "<button class='primary' data-action='launch' data-app-id='" << html_escape_text(app.id) << "'>Launch</button>"
             << "<button class='danger' data-action='delete' data-app-id='" << html_escape_text(app.id) << "'>Delete</button>"
             << "</div></article>";
    }
    return html.str();
}

std::string build_launcher_status_html(const std::string& status) {
    if (status.empty()) {
        return {};
    }
    return "<p class='status'>" + html_escape_text(status) + "</p>";
}

std::string load_launcher_resource(const jellyframe_example::AppPackage& package,
                                   const std::string& resource_path,
                                   jellyframe::DiagnosticSink* diagnostics = nullptr) {
    jellyframe_example::PackageResourceStats stats;
    jellyframe_example::PackageResourceContext context;
    context.root = package.root;
    context.base_url = package.manifest.entry;
    context.max_input_bytes = kMaxInputBytes;
    context.stats = &stats;
    context.diagnostics = diagnostics;
    context.bundle_bytes = package.bundle_bytes;
    context.bundle_entries = package.bundle_entries;
    context.bundle_payload_offset = package.bundle_payload_offset;
    std::string text;
    if (!jellyframe_example::load_package_resource(resource_path, package.manifest.entry, text, &context)) {
        return {};
    }
    return text;
}

std::string load_launcher_entry_html(const jellyframe_example::AppPackage& package) {
    std::string html = load_launcher_resource(package, package.manifest.entry);
    if (html.empty()) {
        throw std::runtime_error("failed to load launcher entry: " + package.manifest.entry);
    }
    return html;
}

void inject_launcher_markup(std::string& html, const std::string& app_list_html, const std::string& status_html) {
    if (!replace_once(html, kLauncherStatusMarker, status_html) && !status_html.empty()) {
        const std::size_t main_end = html.find("</main>");
        html.insert(main_end == std::string::npos ? html.size() : main_end, status_html);
    }
    if (!replace_once(html, kLauncherAppListMarker, app_list_html)) {
        const std::size_t main_end = html.find("</main>");
        html.insert(main_end == std::string::npos ? html.size() : main_end, app_list_html);
    }
}

std::string build_system_shell_html(const std::filesystem::path& launcher_app_path,
                                    const std::filesystem::path& registry_store,
                                    const std::string& status) {
    const auto package = jellyframe_example::load_app_package(launcher_app_path, kMaxInputBytes);
    std::string html = load_launcher_entry_html(package);
    inject_launcher_markup(html, build_launcher_app_list_html(registry_store), build_launcher_status_html(status));
    return html;
}

std::string load_system_shell_css(const std::filesystem::path& launcher_app_path) {
    const auto package = jellyframe_example::load_app_package(launcher_app_path, kMaxInputBytes);
    return load_launcher_resource(package, "/styles/app.css");
}

std::string emergency_launcher_error_html(const std::string& message) {
    return "<body><main class='launcher'><section class='empty'><h1>Launcher unavailable</h1><p>" +
        html_escape_text(message) + "</p></section></main></body>";
}

std::string emergency_launcher_error_css() {
    return "body{margin:0;padding:18px;background:#101418;color:#f8fafc;font-size:14px}"
           ".empty{padding:12px;background:#171d24;border:1px solid #ef4444;border-radius:8px}"
           "h1{margin:0 0 8px 0;font-size:20px;color:#ffffff}p{margin:0;color:#fecaca}";
}

const Node* find_shell_action_node(const Node* node) {
    for (const Node* current = node; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element && !current->attribute("data-action").empty() &&
            !current->attribute("data-app-id").empty()) {
            return current;
        }
    }
    return nullptr;
}

void load_package_fonts_into_runtime(const jellyframe_example::AppPackageManifest& manifest,
                                     jellyframe_example::PackageResourceContext& package_context,
                                     AppRuntimeHost* app_runtime,
                                     DiagnosticSink* diagnostics) {
    if (app_runtime == nullptr || manifest.font_sources.empty()) {
        return;
    }
    app_runtime->clear_current_fonts();
    for (const std::string& source : manifest.font_sources) {
        std::string bytes;
        if (!jellyframe_example::load_package_resource(source, manifest.entry, bytes, &package_context)) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Warning,
                              "app-font-resource-missing",
                              "Manifest font resource could not be loaded",
                              source);
            continue;
        }
        const AppFontLoadResult result = app_runtime->load_current_jffont(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        if (!result.loaded()) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Warning,
                              "app-font-resource-invalid",
                              "Manifest font resource is not a supported .jffont supplement",
                              source);
        }
    }
}

LoadedPage load_page(const BrowserOptions& options,
                     const HostBudgets& budgets,
                     DiagnosticSink* diagnostics,
                     AppRuntimeHost* app_runtime = nullptr) {
    HtmlParser html_parser;
    CssParser css_parser;
    HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
    html_options.diagnostics = diagnostics;
    CssParserOptions css_options = css_parser_options_from_budgets(budgets);
    css_options.diagnostics = diagnostics;
    LoadedPage page;
    if (!options.inline_html.empty()) {
        page.document = html_parser.parse(options.inline_html, html_options);
        page.stylesheet = css_parser.parse(options.inline_css, css_options);
        page.script_base_dir = std::filesystem::current_path();
        return page;
    }
    if (!options.app_path.empty()) {
        auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
        page.package_mode = true;
        page.package_stats = {};
        page.package_context.root = package.root;
        page.package_context.base_url = package.manifest.entry;
        page.package_context.max_input_bytes = kMaxInputBytes;
        page.package_context.stats = &page.package_stats;
        page.package_context.diagnostics = diagnostics;
        page.package_context.bundle_bytes = std::move(package.bundle_bytes);
        page.package_context.bundle_entries = std::move(package.bundle_entries);
        page.package_context.bundle_payload_offset = package.bundle_payload_offset;
        load_package_fonts_into_runtime(package.manifest, page.package_context, app_runtime, diagnostics);
        std::string html;
        if (!jellyframe_example::load_package_resource(package.manifest.entry,
                                                       package.manifest.entry,
                                                       html,
                                                       &page.package_context)) {
            throw std::runtime_error("failed to load package entry: " + package.manifest.entry);
        }
        page.document = html_parser.parse(html, html_options);
        const std::string css = combine_author_css(
            {}, *page.document, jellyframe_example::load_package_stylesheet, &page.package_context);
        page.stylesheet = css_parser.parse(css, css_options);
        page.script_base_dir = package.root / std::filesystem::path(package.manifest.entry).parent_path().relative_path();
        return page;
    }

    page.document = html_parser.parse(read_file_limited(options.html_path), html_options);
    page.stylesheet = css_parser.parse(
        [&]() {
            jellyframe_example::StylesheetLoadContext stylesheet_context;
            const std::filesystem::path css_path(options.css_path);
            stylesheet_context.base_dir =
                css_path.has_parent_path() ? css_path.parent_path() : std::filesystem::current_path();
            stylesheet_context.max_input_bytes = kMaxInputBytes;
            stylesheet_context.diagnostics = diagnostics;
            return combine_author_css(jellyframe_example::read_file_limited(options.css_path, kMaxInputBytes),
                                      *page.document,
                                      jellyframe_example::load_linked_stylesheet,
                                      &stylesheet_context);
        }(),
        css_options);
    const std::filesystem::path html_path(options.html_path);
    page.script_base_dir = html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
    return page;
}

FrameBuffer render_page_with_browser_text(const BrowserOptions& options) {
    const HostBudgets budgets = desktop_browser_budgets();
    VectorDiagnosticSink diagnostics;
    AppRuntimeHost app_runtime{AppRuntimeHostOptions{64, 32, 64, 1024 * 1024, 4}};
    AppRuntimeHost* runtime = nullptr;
    if (!options.app_path.empty()) {
        app_runtime.launch(options.app_path, AppRole::App);
        runtime = &app_runtime;
    } else {
        app_runtime.launch("org.jellyframe.debug.capture", AppRole::App);
        runtime = &app_runtime;
    }
    LoadedPage page = load_page(options, budgets, &diagnostics, runtime);
    BrowserTextBackend text_backend = make_browser_text_backend(options, runtime);
    ImageDecodeMock debug_images(ImageDecodePolicy{true, 1024, 256, 256, 256 * 256 * 4, 4});
    add_debug_image_fixtures(debug_images);
    if (page.package_mode) {
        add_package_image_fixtures(*page.document, page.package_context, debug_images, &diagnostics);
    }
    AppImageSurfaceCache image_cache(AppImageSurfaceCacheOptions{8, 512 * 1024});
    BrowserImageResolveContext image_resolve_context{&app_runtime, &debug_images, &image_cache, &diagnostics};
    BrowserImageContext image_context{&debug_images};
    StyleResolverOptions style_options;
    style_options.diagnostics = &diagnostics;
    StyleResolver resolver(std::move(page.stylesheet), style_options);

    RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
    render_options.diagnostics = &diagnostics;
    RenderTreeBuilder render_builder(resolver, render_options);
    auto render_tree = render_builder.build(*page.document);
    LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
    layout_options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, text_backend.measure, layout_options);
    auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width);
    LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
    layer_options.diagnostics = &diagnostics;
    layer_options.image_resolver = ImageHandleResolver{resolve_browser_image_handle, &image_resolve_context};
    LayerTreeBuilder layer_builder(layer_options);
    auto layer_tree = layer_builder.build(*layout_tree);
    for (int pass = 0; pass < 4 && debug_images.complete_next(app_runtime); ++pass) {
        std::vector<HostServiceCompletion> accepted;
        app_runtime.pump_frame_completions(accepted);
        bool image_ready = false;
        for (const HostServiceCompletion& completion : accepted) {
            const std::string image_src = image_cache.url_for_job(completion.job_id);
            report_image_completion_failure(&diagnostics, completion, image_src);
            image_ready = image_cache.handle_completion(completion) || image_ready;
        }
        if (image_ready) {
            layer_tree = layer_builder.build(*layout_tree);
            std::vector<std::uint32_t> protected_handles;
            collect_image_handles(*layer_tree, protected_handles);
            image_cache.evict_unreferenced(app_runtime,
                                           debug_images,
                                           protected_handles.data(),
                                           protected_handles.size());
        }
    }

    SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets);
    compositor_options.diagnostics = &diagnostics;
    SoftwareCompositor compositor(text_backend.painter, ImagePainter{paint_image_surface, &image_context}, compositor_options);
    FrameBuffer frame_buffer = compositor.render(*layer_tree,
                                                 options.viewport_width,
                                                 options.viewport_height,
                                                 page_background_color(*page.document, resolver));
    if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
        throw std::runtime_error("framebuffer budget exceeded");
    }
    print_diagnostics(diagnostics);
    return frame_buffer;
}

FrameBuffer render_page_with_browser_text(const std::string& html_path,
                                          const std::string& css_path,
                                          int viewport_width,
                                          int min_viewport_height) {
    BrowserOptions options;
    options.html_path = html_path;
    options.css_path = css_path;
    options.viewport_width = viewport_width;
    options.viewport_height = min_viewport_height;
    return render_page_with_browser_text(options);
}

class BrowserApp {
public:
    explicit BrowserApp(BrowserOptions options)
        : options_(std::move(options)),
          active_app_id_(options_.launch_app_id) {
        if (!options_.launch_app_id.empty()) {
            app_runtime_.launch(options_.launch_app_id, AppRole::App);
        } else if (!options_.app_path.empty()) {
            app_runtime_.launch(options_.app_path, AppRole::App);
        } else if (!options_.registry_store_path.empty()) {
            app_runtime_.launch("org.jellyframe.system.launcher", AppRole::Launcher);
        } else {
            app_runtime_.launch("org.jellyframe.debug.page", AppRole::App);
        }
        debug_network_.add_fixture(NetworkFetchFixture{"app://ping", 200, "text/plain", "pong"});
        debug_network_.add_fixture(NetworkFetchFixture{"app://weather", 200, "application/json", "{\"temp\":21}"});
        add_debug_image_fixtures(debug_images_);
    }

    bool initialize(HINSTANCE instance, int show_command) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = BrowserApp::window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClassName;
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&window_class);

        RECT window_rect{0, 0, options_.viewport_width, options_.viewport_height};
        AdjustWindowRectEx(&window_rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        hwnd_ = CreateWindowExW(0,
                                kWindowClassName,
                                L"JellyFrame Win32 Browser",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                std::max<LONG>(1, window_rect.right - window_rect.left),
                                std::max<LONG>(1, window_rect.bottom - window_rect.top),
                                nullptr,
                                nullptr,
                                instance,
                                this);
        if (hwnd_ == nullptr) {
            return false;
        }

        ShowWindow(hwnd_, show_command);
        UpdateWindow(hwnd_);
        return true;
    }

    int run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    HWND hwnd_ = nullptr;
    BrowserOptions options_;
    int viewport_width_ = 1;
    int viewport_height_ = 1;
    int scroll_y_ = 0;
    HostBudgets budgets_ = desktop_browser_budgets();

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
    std::unique_ptr<JerryScriptRuntime> script_runtime_;
    std::uint32_t script_runtime_instance_id_ = 0;
#endif
    std::unique_ptr<Node> document_;
    std::unique_ptr<StyleResolver> style_resolver_;
    RenderObjectPtr render_tree_;
    LayoutBoxPtr layout_tree_;
    LayerNodePtr layer_tree_;
    std::unique_ptr<InputController> input_;
    FrameBuffer frame_buffer_;
    Color page_background_{255, 255, 255, 255};
    std::vector<std::uint32_t> blit_pixels_;
    DirtyRegionMode last_dirty_region_mode_ = DirtyRegionMode::Clean;
    DirtyRegionFallbackReason last_dirty_region_reason_ = DirtyRegionFallbackReason::None;
    std::size_t last_dirty_rect_count_ = 0;
    int last_dirty_area_percent_ = 0;
    DisplayInvalidationResult last_display_invalidation_;
    DirtyRegionStatistics dirty_region_statistics_;
    VectorDiagnosticSink diagnostics_;
    bool system_shell_mode_ = false;
    std::string active_app_id_;
    AppRuntimeHost app_runtime_{AppRuntimeHostOptions{64, 32, 64, 1024 * 1024, 4}};
    NetworkFetchMock debug_network_{NetworkFetchPolicy{true, 1024, 64 * 1024}};
    ImageDecodeMock debug_images_{ImageDecodePolicy{true, 1024, 256, 256, 256 * 256 * 4, 4}};
    AppImageSurfaceCache image_cache_{AppImageSurfaceCacheOptions{8, 512 * 1024}};
    BrowserImageContext image_context_{&debug_images_};
    AppLocalStorageShadow debug_local_storage_{AppPrivateKvPolicy{true, 64, 2048, 64, 32 * 1024}};
    std::uint32_t debug_local_storage_instance_id_ = 0;
    std::string pending_shell_action_;
    std::string pending_shell_app_id_;

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        BrowserApp* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            app = static_cast<BrowserApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        } else {
            app = reinterpret_cast<BrowserApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        return app->handle_message(message, wparam, lparam);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            resize_to_client();
            if (!options_.registry_store_path.empty() && options_.app_path.empty() && options_.inline_html.empty()) {
                configure_system_shell(options_.startup_status);
            }
            rebuild();
            return 0;
        case WM_SIZE:
            resize_to_client();
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_MOUSEMOVE:
            handle_pointer_move(wparam, lparam);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            handle_pointer_down(wparam, lparam);
            return 0;
        case WM_LBUTTONUP:
            handle_pointer_up(wparam, lparam);
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            handle_wheel(wparam, lparam);
            return 0;
        case WM_CHAR:
            handle_char(wparam);
            return 0;
        case WM_KEYDOWN:
            handle_key_down(wparam);
            return 0;
        case WM_TIMER:
            handle_timer(wparam);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kScriptTimerId);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
        }
    }

    void resize_to_client() {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        viewport_width_ = std::max(1L, rect.right - rect.left);
        viewport_height_ = std::max(1L, rect.bottom - rect.top);
    }

    bool runtime_controls_page() const {
        return !options_.app_path.empty() || !options_.registry_store_path.empty();
    }

    bool accepts_ui_events() const {
        return !runtime_controls_page() || app_runtime_.current().state == AppLifecycleState::Foreground;
    }

    void reset_image_services() {
        image_cache_.release_all(app_runtime_, debug_images_);
        debug_images_.clear();
        add_debug_image_fixtures(debug_images_);
    }

    bool drain_host_completions() {
        std::vector<HostServiceCompletion> accepted;
        const AppCompletionPumpResult result = app_runtime_.pump_frame_completions(accepted);
        if (result.consumed == 0) {
            return false;
        }
        std::size_t image_handled = 0;
        for (const HostServiceCompletion& completion : accepted) {
            const std::string image_src = image_cache_.url_for_job(completion.job_id);
            report_image_completion_failure(&diagnostics_, completion, image_src);
            if (image_cache_.handle_completion(completion)) {
                ++image_handled;
            } else if (completion.kind == HostServiceJobKind::ImageDecode && completion.handle != 0) {
                debug_images_.release_surface(app_runtime_, completion.handle);
            }
        }
        if (image_handled != 0 && document_ != nullptr) {
            mark_dirty(*document_, DomDirtyPaint);
        }
        std::size_t script_handled = 0;
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ != nullptr &&
            script_runtime_instance_id_ == app_runtime_.current_app_instance_id()) {
            for (const HostServiceCompletion& completion : accepted) {
                if (script_runtime_->handle_host_completion(completion)) {
                    ++script_handled;
                }
            }
        }
#endif
        std::cout << "host completions consumed=" << result.consumed
                  << " accepted=" << result.accepted
                  << " stale=" << result.stale
                  << " released_stale_handles=" << result.released_stale_handles
                  << " image_handled=" << image_handled
                  << " script_handled=" << script_handled << '\n';
        return script_handled != 0 || image_handled != 0;
    }

    void evict_unused_image_surfaces() {
        if (layer_tree_ == nullptr) {
            image_cache_.evict_unreferenced(app_runtime_, debug_images_);
            return;
        }
        std::vector<std::uint32_t> protected_handles;
        collect_image_handles(*layer_tree_, protected_handles);
        image_cache_.evict_unreferenced(app_runtime_,
                                        debug_images_,
                                        protected_handles.data(),
                                        protected_handles.size());
    }

    void configure_system_shell(std::string status) {
        if (options_.registry_store_path.empty()) {
            return;
        }
        system_shell_mode_ = true;
        active_app_id_.clear();
        reset_image_services();
        app_runtime_.launch("org.jellyframe.system.launcher", AppRole::Launcher);
        options_.app_path.clear();
        options_.script_path.clear();
        try {
            options_.inline_html =
                build_system_shell_html(options_.launcher_app_path, options_.registry_store_path, status);
            options_.inline_css = load_system_shell_css(options_.launcher_app_path);
        } catch (const std::exception& error) {
            options_.inline_html = emergency_launcher_error_html(error.what());
            options_.inline_css = emergency_launcher_error_css();
        }
        scroll_y_ = 0;
    }

    void launch_installed_app(const std::string& app_id) {
        try {
            const std::filesystem::path bundle_path =
                jellyframe_example::find_installed_app_bundle_path(options_.registry_store_path, app_id);
            auto package = jellyframe_example::load_app_package(bundle_path, kMaxInputBytes);
            options_.app_path = bundle_path.string();
            options_.inline_html.clear();
            options_.inline_css.clear();
            active_app_id_ = app_id;
            reset_image_services();
            app_runtime_.launch(app_id, AppRole::App);
            system_shell_mode_ = false;
            scroll_y_ = 0;
            if (!options_.viewport_width_set && package.manifest.viewport_width > 0) {
                options_.viewport_width = package.manifest.viewport_width;
            }
            if (!options_.viewport_height_set && package.manifest.viewport_height > 0) {
                options_.viewport_height = package.manifest.viewport_height;
            }
            if (!rebuild()) {
                return;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            set_title("launched " + app_id);
        } catch (const std::exception& error) {
            configure_system_shell(std::string("Launch failed: ") + error.what());
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void delete_installed_app(const std::string& app_id) {
        try {
            if (!active_app_id_.empty() && active_app_id_ == app_id) {
                reset_image_services();
                app_runtime_.exit_current();
                configure_system_shell("Cannot delete the active app; returned to shell first.");
            }
            const auto removed = jellyframe_example::remove_bundle_from_registry(options_.registry_store_path, app_id);
            configure_system_shell("Deleted " + removed.name + ".");
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } catch (const std::exception& error) {
            configure_system_shell(std::string("Delete failed: ") + error.what());
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    bool process_shell_action_if_needed() {
        if (pending_shell_action_.empty() || pending_shell_app_id_.empty()) {
            return false;
        }
        const std::string action = std::move(pending_shell_action_);
        const std::string app_id = std::move(pending_shell_app_id_);
        pending_shell_action_.clear();
        pending_shell_app_id_.clear();
        if (action == "launch") {
            launch_installed_app(app_id);
        } else if (action == "delete") {
            delete_installed_app(app_id);
        }
        return true;
    }

    void recover_active_app_after_failure(const std::exception& error) {
        std::cerr << "rebuild failed: " << error.what() << '\n';
        if (!options_.registry_store_path.empty() && !system_shell_mode_) {
            reset_image_services();
            const AppTeardownResult teardown = app_runtime_.crash_current();
            const std::string crashed_app = active_app_id_.empty() ? "app" : active_app_id_;
            configure_system_shell(
                "Recovered from " + crashed_app + " after an error; released instance " +
                std::to_string(teardown.app_instance_id) + ".");
            try {
                rebuild();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } catch (const std::exception& shell_error) {
                std::cerr << "system shell recovery failed: " << shell_error.what() << '\n';
                set_title(std::string("error: ") + shell_error.what());
            }
            return;
        }
        set_title(std::string("error: ") + error.what());
    }

    bool rebuild() {
        try {
            diagnostics_.clear();
            reset_image_services();
            KillTimer(hwnd_, kScriptTimerId);
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            script_runtime_.reset();
            script_runtime_instance_id_ = 0;
#endif
            LoadedPage page = load_page(options_, budgets_, &diagnostics_, &app_runtime_);
            if (page.package_mode) {
                add_package_image_fixtures(*page.document, page.package_context, debug_images_, &diagnostics_);
            }
            document_ = std::move(page.document);
            StyleResolverOptions style_options;
            style_options.diagnostics = &diagnostics_;
            style_resolver_ = std::make_unique<StyleResolver>(std::move(page.stylesheet), style_options);
            page_background_ = page_background_color(*document_, *style_resolver_);
            render_tree_.reset();
            layout_tree_.reset();
            layer_tree_.reset();
            input_.reset();
            pending_shell_action_.clear();
            pending_shell_app_id_.clear();
            dirty_region_statistics_ = DirtyRegionStatistics{};

            if (system_shell_mode_) {
                document_->add_event_listener("click", [this](Event& event) {
                    const Node* action_node = find_shell_action_node(event.target());
                    if (action_node == nullptr) {
                        return;
                    }
                    pending_shell_action_ = action_node->attribute("data-action");
                    pending_shell_app_id_ = action_node->attribute("data-app-id");
                    event.prevent_default();
                });
            } else {
                document_->add_event_listener("click", [this](Event& event) {
                    std::cout << "click target=" << describe_node(event.target()) << '\n';
                    set_title("clicked " + describe_node(event.target()));
                });
            }

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            jellyframe_example::ScriptLoadContext script_context;
            script_context.base_dir = page.script_base_dir.empty() ? std::filesystem::current_path() : page.script_base_dir;
            script_context.max_input_bytes = kMaxInputBytes;
            script_context.diagnostics = &diagnostics_;
            std::vector<DocumentScript> document_scripts =
                page.package_mode
                    ? collect_classic_scripts(
                          *document_, jellyframe_example::load_package_script, &page.package_context, &diagnostics_)
                    : collect_classic_scripts(
                          *document_, jellyframe_example::load_linked_script, &script_context, &diagnostics_);
            if (!document_scripts.empty() || !options_.script_path.empty()) {
                script_runtime_ = std::make_unique<JerryScriptRuntime>(budgets_);
                script_runtime_instance_id_ = app_runtime_.current_app_instance_id();
                if (debug_local_storage_instance_id_ != script_runtime_instance_id_) {
                    debug_local_storage_.clear();
                    debug_local_storage_instance_id_ = script_runtime_instance_id_;
                }
                script_runtime_->bind_app_services(app_runtime_, debug_network_);
                script_runtime_->bind_local_storage(debug_local_storage_);
                script_runtime_->set_host_time_ms(GetTickCount64());
                script_runtime_->bind_document(*document_);
                for (const DocumentScript& script : document_scripts) {
                    const ScriptEvaluationResult result = script_runtime_->eval(script.source, script.name);
                    if (!result.ok) {
                        report_diagnostic(&diagnostics_,
                                          DiagnosticStage::Script,
                                          DiagnosticSeverity::Error,
                                          "script-evaluation-failed",
                                          "Document script evaluation failed",
                                          script.name + ": " + result.error);
                        std::cerr << "document script failed: " << result.error << '\n';
                        set_title("script error: " + result.error);
                        break;
                    }
                }
                if (!options_.script_path.empty()) {
                    const ScriptEvaluationResult result =
                        script_runtime_->eval(read_file_limited(options_.script_path), options_.script_path);
                    if (!result.ok) {
                        report_diagnostic(&diagnostics_,
                                          DiagnosticStage::Script,
                                          DiagnosticSeverity::Error,
                                          "script-evaluation-failed",
                                          "Standalone script evaluation failed",
                                          options_.script_path + ": " + result.error);
                        std::cerr << "script failed: " << result.error << '\n';
                        set_title("script error: " + result.error);
                    }
                }
            }
#endif
            render_current(nullptr, nullptr, nullptr);
            SetTimer(hwnd_, kScriptTimerId, kScriptTimerPeriodMs, nullptr);
            print_diagnostics(diagnostics_);
            return true;
        } catch (const std::exception& error) {
            recover_active_app_after_failure(error);
            return false;
        }
    }

    void render_current(const Node* hovered_node, const Node* active_node, const Node* focused_node) {
        if (document_ == nullptr || style_resolver_ == nullptr) {
            return;
        }
        drain_host_completions();
        style_resolver_->set_interaction_state(hovered_node, active_node, focused_node);
        const DomDirtyFlags dirty_flags = document_->dirty_flags;
        const int current_content_height = layout_tree_ != nullptr
            ? std::max(viewport_height_, layout_tree_->rect.height)
            : frame_buffer_.height;
        FramePipelineCacheState cache_state;
        cache_state.has_render_tree = render_tree_ != nullptr;
        cache_state.has_layout_tree = layout_tree_ != nullptr;
        cache_state.has_layer_tree = layer_tree_ != nullptr;
        cache_state.has_framebuffer = frame_buffer_.width > 0 && frame_buffer_.height > 0;
        cache_state.framebuffer_width = frame_buffer_.width;
        cache_state.framebuffer_height = frame_buffer_.height;
        cache_state.viewport = Rect{0, 0, viewport_width_, viewport_height_};
        cache_state.content_height = current_content_height;
        const FrameUpdateState update_state = make_frame_update_state(dirty_flags, cache_state);
        const FrameUpdatePlan update_plan = plan_frame_update(update_state);
        if (update_plan.action == FrameUpdateAction::None) {
            record_dirty_region(DirtyRegionResult{});
            return;
        }

        LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets_);
        layer_options.diagnostics = &diagnostics_;
        BrowserImageResolveContext image_resolve_context{&app_runtime_, &debug_images_, &image_cache_, &diagnostics_};
        layer_options.image_resolver = ImageHandleResolver{resolve_browser_image_handle, &image_resolve_context};
        LayerTreeBuilder layer_builder(layer_options);
        SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets_);
        compositor_options.diagnostics = &diagnostics_;
        BrowserTextBackend text_backend = make_browser_text_backend(options_, &app_runtime_);
        SoftwareCompositor compositor(text_backend.painter,
                                      ImagePainter{paint_image_surface, &image_context_},
                                      compositor_options);
        if (update_plan.action == FrameUpdateAction::RepaintExisting &&
            update_plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout &&
            layout_tree_ != nullptr) {
            const int content_height = std::max(viewport_height_, layout_tree_->rect.height);
            auto next_layer_tree = layer_builder.build(*layout_tree_);
            const DirtyRegionResult dirty_region = compute_dirty_region(
                *document_,
                layout_tree_.get(),
                layout_tree_.get(),
                dirty_region_options_from_budgets(budgets_, Rect{0, 0, viewport_width_, content_height}, 3));
            const std::vector<Rect>& dirty_rects = dirty_region.rects;
            layer_tree_ = std::move(next_layer_tree);
            evict_unused_image_surfaces();
            if (!dirty_rects.empty() &&
                dirty_region_should_repaint_incrementally(dirty_region,
                                                          Rect{0, 0, viewport_width_, content_height},
                                                          kIncrementalDirtyAreaLimitPercent)) {
                compositor.render_into(*layer_tree_,
                                       frame_buffer_,
                                       page_background_,
                                       dirty_rects.data(),
                                       dirty_rects.size());
                last_display_invalidation_ =
                    analyze_display_invalidation(*layer_tree_, dirty_rects.data(), dirty_rects.size());
                record_dirty_region(dirty_region);
            } else {
                render_full_frame(compositor, dirty_region, dirty_rects.empty(), content_height);
            }
            input_ = std::make_unique<InputController>(*layer_tree_);
            input_->set_interaction_state(hovered_node, active_node, focused_node);
            update_blit_pixels();
            clear_dirty_flags(*document_);
            return;
        }

        const DomDirtyFlags rebuild_dirty_flags = document_->dirty_flags;
        LayoutBoxPtr previous_layout = update_plan.needs_previous_layout ? std::move(layout_tree_) : LayoutBoxPtr{};
        RenderTreeOptions render_options = render_tree_options_from_budgets(budgets_);
        render_options.diagnostics = &diagnostics_;
        RenderTreeBuilder render_builder(*style_resolver_, render_options);
        auto next_render_tree = render_builder.build(*document_);
        LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets_);
        layout_options.diagnostics = &diagnostics_;
        LayoutEngine layout_engine(*style_resolver_, text_backend.measure, layout_options);
        auto next_layout_tree = layout_engine.layout(*next_render_tree, viewport_width_);
        auto next_layer_tree = layer_builder.build(*next_layout_tree);

        const int content_height = std::max(viewport_height_, next_layout_tree->rect.height);
        const FrameRepaintPlan repaint_plan = plan_frame_repaint(update_state, update_plan, content_height);
        scroll_y_ = std::max(0, std::min(scroll_y_, std::max(0, content_height - viewport_height_)));
        std::vector<Rect> dirty_rects;
        DirtyRegionResult dirty_region;
        const bool can_repaint_incrementally = repaint_plan.can_repaint_dirty_rects &&
            repaint_plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout &&
            previous_layout != nullptr;
        if (can_repaint_incrementally && rebuild_dirty_flags != DomDirtyNone) {
            dirty_region = compute_dirty_region(*document_,
                                                previous_layout.get(),
                                                next_layout_tree.get(),
                                                dirty_region_options_from_budgets(
                                                    budgets_, Rect{0, 0, viewport_width_, content_height}, 3));
            dirty_rects = dirty_region.rects;
        }

        render_tree_ = std::move(next_render_tree);
        layout_tree_ = std::move(next_layout_tree);
        layer_tree_ = std::move(next_layer_tree);
        evict_unused_image_surfaces();

        if (can_repaint_incrementally && !dirty_rects.empty() &&
            dirty_region_should_repaint_incrementally(dirty_region,
                                                      Rect{0, 0, viewport_width_, content_height},
                                                      kIncrementalDirtyAreaLimitPercent)) {
            compositor.render_into(*layer_tree_,
                                   frame_buffer_,
                                   page_background_,
                                   dirty_rects.data(),
                                   dirty_rects.size());
            last_display_invalidation_ =
                analyze_display_invalidation(*layer_tree_, dirty_rects.data(), dirty_rects.size());
            record_dirty_region(dirty_region);
        } else {
            render_full_frame(compositor, dirty_region, dirty_rects.empty(), content_height);
        }
        input_ = std::make_unique<InputController>(*layer_tree_);
        input_->set_interaction_state(hovered_node, active_node, focused_node);
        update_blit_pixels();
        clear_dirty_flags(*document_);
    }

    int max_scroll_y() const {
        return std::max(0, frame_buffer_.height - viewport_height_);
    }

    void render_full_frame(SoftwareCompositor& compositor,
                           const DirtyRegionResult& attempted_region,
                           bool had_no_dirty_rects,
                           int content_height) {
        frame_buffer_ = compositor.render(*layer_tree_, viewport_width_, content_height, page_background_);
        DirtyRegionResult full_region;
        full_region.mode = DirtyRegionMode::FullFrame;
        full_region.fallback_reason = had_no_dirty_rects
            ? attempted_region.fallback_reason
            : DirtyRegionFallbackReason::DirtyAreaTooLarge;
        full_region.rects.push_back(Rect{0, 0, viewport_width_, content_height});
        last_display_invalidation_ =
            analyze_display_invalidation(*layer_tree_, full_region.rects.data(), full_region.rects.size());
        record_dirty_region(full_region);
    }

    int clamp_scroll_y(int value) const {
        const int content_height = layout_tree_ != nullptr
            ? std::max(viewport_height_, layout_tree_->rect.height)
            : frame_buffer_.height;
        return std::max(0, std::min(value, std::max(0, content_height - viewport_height_)));
    }

    bool scroll_by(int delta_y) {
        const int previous = scroll_y_;
        scroll_y_ = std::max(0, std::min(scroll_y_ + delta_y, max_scroll_y()));
        if (scroll_y_ == previous) {
            return false;
        }
        update_blit_pixels();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const LayoutBox* find_layout_by_id(const LayoutBox& box, const std::string& id) const {
        if (box.node != nullptr && box.node->attribute("id") == id) {
            return &box;
        }
        for (const auto& child : box.children) {
            const LayoutBox* found = find_layout_by_id(*child, id);
            if (found != nullptr) {
                return found;
            }
        }
        return nullptr;
    }

    bool scroll_to_y(int y) {
        const int previous = scroll_y_;
        scroll_y_ = clamp_scroll_y(y);
        if (scroll_y_ == previous) {
            return false;
        }
        update_blit_pixels();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    bool follow_hash_anchor(const Node* node) {
        if (node == nullptr || layout_tree_ == nullptr || node->type != NodeType::Element ||
            node->tag_name != "a") {
            return false;
        }
        const std::string& href = node->attribute("href");
        if (href.size() <= 1 || href.front() != '#') {
            return false;
        }
        const LayoutBox* target = find_layout_by_id(*layout_tree_, href.substr(1));
        return target != nullptr && scroll_to_y(target->rect.y);
    }

    void update_blit_pixels() {
        blit_pixels_.assign(static_cast<std::size_t>(viewport_width_) * static_cast<std::size_t>(viewport_height_),
                            color_to_bgrx(page_background_));
        if (frame_buffer_.width <= 0 || frame_buffer_.height <= 0) {
            return;
        }
        scroll_y_ = std::max(0, std::min(scroll_y_, max_scroll_y()));
        const int copy_width = std::min(viewport_width_, frame_buffer_.width);
        const int copy_height = std::min(viewport_height_, frame_buffer_.height - scroll_y_);
        for (int y = 0; y < copy_height; ++y) {
            for (int x = 0; x < copy_width; ++x) {
                blit_pixels_[static_cast<std::size_t>(y * viewport_width_ + x)] =
                    color_to_bgrx(frame_buffer_.pixel(x, scroll_y_ + y));
            }
        }
    }

    void paint() {
        PAINTSTRUCT paint_struct{};
        HDC dc = BeginPaint(hwnd_, &paint_struct);
        if (!blit_pixels_.empty() && viewport_width_ > 0 && viewport_height_ > 0) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = viewport_width_;
            info.bmiHeader.biHeight = -viewport_height_;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc,
                              0,
                              0,
                              static_cast<DWORD>(viewport_width_),
                              static_cast<DWORD>(viewport_height_),
                              0,
                              0,
                              0,
                              static_cast<UINT>(viewport_height_),
                              blit_pixels_.data(),
                              &info,
                              DIB_RGB_COLORS);
        }
        EndPaint(hwnd_, &paint_struct);
    }

    void handle_pointer_move(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.buttons = buttons_from_keys(wparam);
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_move(input);
        rerender_if_dirty(input_->focused_node());
        set_title("hover " + describe_node(target));
    }

    void handle_pointer_down(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.button = PointerButton::Primary;
        input.buttons = buttons_from_keys(wparam) | 1;
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_down(input);
        rerender_if_dirty(input_->focused_node());
        set_title("active " + describe_node(target));
    }

    void handle_pointer_up(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.button = PointerButton::Primary;
        input.buttons = buttons_from_keys(wparam) & ~1;
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_up(input);
        rerender_if_dirty(input_->focused_node());
        follow_hash_anchor(target);
        if (process_shell_action_if_needed()) {
            return;
        }
        set_title("up " + describe_node(target));
    }

    void handle_wheel(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);

        WheelInput input;
        input.x = point.x;
        input.y = point.y + scroll_y_;
        input.delta_y = GET_WHEEL_DELTA_WPARAM(wparam);
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->wheel(input);
        rerender_if_dirty(input_->focused_node());
        const int scroll_delta = -input.delta_y;
        scroll_by(scroll_delta);
        set_title("wheel " + describe_node(target) + " scrollY=" + std::to_string(scroll_y_));
    }

    void handle_char(WPARAM wparam) {
        if (!input_ || !accepts_ui_events() || wparam < 0x20 || wparam == 0x7f) {
            return;
        }
        const Node* focus = input_->focused_node();
        if (input_->text_input(wide_char_to_utf8(static_cast<wchar_t>(wparam)))) {
            rerender_if_dirty(focus);
        }
    }

    void handle_key_down(WPARAM wparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        KeyInput key;
        if (wparam == VK_BACK) {
            key.code = KeyCode::Backspace;
        } else if (wparam == VK_ESCAPE && !options_.registry_store_path.empty() && !system_shell_mode_) {
            configure_system_shell("Returned from " + active_app_id_ + ".");
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if (wparam == VK_RETURN) {
            key.code = KeyCode::Enter;
        } else if (wparam == VK_SPACE) {
            key.code = KeyCode::Space;
        } else if (wparam == VK_TAB) {
            key.code = KeyCode::Tab;
        } else if (wparam == VK_UP) {
            key.code = KeyCode::ArrowUp;
        } else if (wparam == VK_DOWN) {
            key.code = KeyCode::ArrowDown;
        } else {
            return;
        }
        const Node* focus = input_->focused_node();
        if (input_->key_down(key)) {
            rerender_if_dirty(focus);
        }
    }

    void handle_timer(WPARAM timer_id) {
        if (timer_id != kScriptTimerId) {
            return;
        }
        const bool completed_image = debug_images_.complete_next(app_runtime_);
        bool handled_completion = drain_host_completions();
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ == nullptr || !accepts_ui_events() ||
            script_runtime_instance_id_ != app_runtime_.current_app_instance_id()) {
            if (completed_image || handled_completion) {
                rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
            }
            return;
        }
        const bool completed_network = debug_network_.complete_next(app_runtime_);
        handled_completion = drain_host_completions() || handled_completion;
        const std::size_t callbacks = script_runtime_->pump_timers(GetTickCount64(), 8);
        if (callbacks != 0 || completed_network || completed_image || handled_completion) {
            rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
        }
#else
        if (completed_image || handled_completion) {
            rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
        }
#endif
    }

    void rerender_if_dirty(const Node* focused_node) {
        if (document_ == nullptr || subtree_dirty_flags(*document_) == DomDirtyNone) {
            return;
        }
        const Node* hovered_node = input_ != nullptr ? input_->hovered_node() : nullptr;
        const Node* active_node = input_ != nullptr ? input_->active_node() : nullptr;
        render_current(hovered_node, active_node, focused_node);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void record_dirty_region(const DirtyRegionResult& region) {
        last_dirty_region_mode_ = region.mode;
        last_dirty_region_reason_ = region.fallback_reason;
        last_dirty_rect_count_ = region.rects.size();
        last_dirty_area_percent_ =
            dirty_region_area_percent(region, Rect{0, 0, frame_buffer_.width, frame_buffer_.height});
        if (region.rects.empty()) {
            last_display_invalidation_ = DisplayInvalidationResult{};
        }
        record_dirty_region_result(dirty_region_statistics_, region);
        if (hwnd_ != nullptr) {
            std::ostringstream status;
            status << "dirty=" << dirty_region_mode_name(last_dirty_region_mode_)
                   << " rects=" << last_dirty_rect_count_
                   << " area=" << last_dirty_area_percent_ << "%"
                   << " cmds=" << last_display_invalidation_.commands_intersecting
                   << "/" << last_display_invalidation_.commands_visited
                   << " local=" << dirty_region_statistics_.dirty_rect_frames
                   << " full=" << dirty_region_statistics_.full_frame_frames
                   << " clean=" << dirty_region_statistics_.clean_frames;
            if (last_dirty_region_reason_ != DirtyRegionFallbackReason::None) {
                status << " reason=" << dirty_region_fallback_reason_name(last_dirty_region_reason_);
            }
            set_title(status.str());
        }
    }

    void set_title(const std::string& status) {
        SetWindowTextW(hwnd_, utf8_to_wide("JellyFrame Win32 Browser - " + status).c_str());
    }
};

} // namespace

int main(int argc, char** argv) {
    BrowserOptions options;
    options.html_path = "src/render_core/samples/pages/modern/app_shell.html";
    options.css_path = "src/render_core/samples/pages/modern/app_shell.css";
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--capture") {
            options.capture = true;
            if (i + 1 >= argc) {
                std::cerr << "--capture requires an output file path\n";
                return 1;
            }
            options.output_path = argv[++i];
            continue;
        }
        if (arg == "--app") {
            if (i + 1 >= argc) {
                std::cerr << "--app requires a package directory\n";
                return 1;
            }
            options.app_path = argv[++i];
            continue;
        }
        if (arg == "--registry-store") {
            if (i + 1 >= argc) {
                std::cerr << "--registry-store requires a directory\n";
                return 1;
            }
            options.registry_store_path = argv[++i];
            continue;
        }
        if (arg == "--launcher-app") {
            if (i + 1 >= argc) {
                std::cerr << "--launcher-app requires a JellyFrame app package directory or .jfapp file\n";
                return 1;
            }
            options.launcher_app_path = argv[++i];
            continue;
        }
        if (arg == "--install-bundle") {
            if (i + 1 >= argc) {
                std::cerr << "--install-bundle requires a .jfapp file\n";
                return 1;
            }
            options.install_bundle_path = argv[++i];
            continue;
        }
        if (arg == "--launch-app") {
            if (i + 1 >= argc) {
                std::cerr << "--launch-app requires an installed app id\n";
                return 1;
            }
            options.launch_app_id = argv[++i];
            continue;
        }
        if (arg == "--remove-app") {
            if (i + 1 >= argc) {
                std::cerr << "--remove-app requires an installed app id\n";
                return 1;
            }
            options.remove_app_id = argv[++i];
            continue;
        }
        if (arg == "--script" || arg == "-s") {
            if (i + 1 >= argc) {
                std::cerr << "--script requires a script file path\n";
                return 1;
            }
            options.script_path = argv[++i];
            continue;
        }
        if (arg == "--viewport-width") {
            if (i + 1 >= argc) {
                std::cerr << "--viewport-width requires a number\n";
                return 1;
            }
            options.viewport_width = parse_int_arg(argv[++i], options.viewport_width);
            options.viewport_width_set = true;
            continue;
        }
        if (arg == "--viewport-height") {
            if (i + 1 >= argc) {
                std::cerr << "--viewport-height requires a number\n";
                return 1;
            }
            options.viewport_height = parse_int_arg(argv[++i], options.viewport_height);
            options.viewport_height_set = true;
            continue;
        }
        if (arg == "--use-app-fonts") {
            options.use_app_fonts = true;
            continue;
        }
        positional.push_back(arg);
    }

    if (options.registry_store_path.empty() && options.app_path.empty() && positional.empty()) {
        options.app_path = "samples/apps/packages/watch_weather";
    }

    if (!options.install_bundle_path.empty() || !options.remove_app_id.empty() || !options.launch_app_id.empty()) {
        if (options.registry_store_path.empty()) {
            std::cerr << "--registry-store is required for install/remove/launch app manager commands\n";
            return 1;
        }
    }

    try {
        if (!options.install_bundle_path.empty()) {
            const auto installed = jellyframe_example::install_bundle_into_registry(
                options.registry_store_path, options.install_bundle_path, kMaxInputBytes);
            options.startup_status = "Installed " + installed.name + ".";
            std::cout << "installed " << installed.id << " " << installed.version_name << '\n';
        }
        if (!options.remove_app_id.empty()) {
            const auto removed =
                jellyframe_example::remove_bundle_from_registry(options.registry_store_path, options.remove_app_id);
            options.startup_status = "Removed " + removed.name + ".";
            std::cout << "removed " << removed.id << '\n';
        }
        if (!options.launch_app_id.empty()) {
            options.app_path =
                jellyframe_example::find_installed_app_bundle_path(options.registry_store_path, options.launch_app_id).string();
        }
    } catch (const std::exception& error) {
        std::cerr << "app manager command failed: " << error.what() << '\n';
            return 1;
    }

    if (!options.registry_store_path.empty() && options.app_path.empty() && positional.empty()) {
        try {
            options.inline_html =
                build_system_shell_html(options.launcher_app_path, options.registry_store_path, options.startup_status);
            options.inline_css = load_system_shell_css(options.launcher_app_path);
        } catch (const std::exception& error) {
            std::cerr << "launcher app load failed: " << error.what() << '\n';
            return 1;
        }
    }

    if (!options.app_path.empty()) {
        try {
            const auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
            options.viewport_width = options.viewport_width_set ? options.viewport_width : package.manifest.viewport_width;
            options.viewport_height = options.viewport_height_set ? options.viewport_height : package.manifest.viewport_height;
            if (options.viewport_width <= 0) {
                options.viewport_width = 390;
            }
            if (options.viewport_height <= 0) {
                options.viewport_height = 640;
            }
            if (options.capture) {
                options.html_path.clear();
                options.css_path.clear();
                FrameBuffer frame_buffer = render_page_with_browser_text(options);
                write_image(frame_buffer, options.output_path);
                std::cout << "JellyFrame Win32 browser capture\n"
                          << "  output=" << options.output_path << '\n'
                          << "  viewport_width=" << options.viewport_width << '\n'
                          << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                          << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                          << "  non_background_pixels="
                          << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
                return 0;
            }
        } catch (const std::exception& error) {
            std::cerr << "app load failed: " << error.what() << '\n';
            return 1;
        }
    } else if (options.capture) {
        if (!options.inline_html.empty()) {
            try {
                FrameBuffer frame_buffer = render_page_with_browser_text(options);
                write_image(frame_buffer, options.output_path);
                std::cout << "JellyFrame Win32 browser capture\n"
                          << "  output=" << options.output_path << '\n'
                          << "  viewport_width=" << options.viewport_width << '\n'
                          << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                          << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                          << "  non_background_pixels="
                          << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
                return 0;
            } catch (const std::exception& error) {
                std::cerr << "capture failed: " << error.what() << '\n';
                return 1;
            }
        }
        if (positional.size() >= 1) {
            options.html_path = positional[0];
        }
        if (positional.size() >= 2) {
            options.css_path = positional[1];
        }
        if (positional.size() >= 3) {
            options.viewport_width = parse_int_arg(positional[2].c_str(), options.viewport_width);
        }
        if (positional.size() >= 4) {
            options.viewport_height = parse_int_arg(positional[3].c_str(), options.viewport_height);
        }
        try {
            FrameBuffer frame_buffer = render_page_with_browser_text(options.html_path,
                                                                      options.css_path,
                                                                      options.viewport_width,
                                                                      options.viewport_height);
            write_image(frame_buffer, options.output_path);
            std::cout << "JellyFrame Win32 browser capture\n"
                      << "  output=" << options.output_path << '\n'
                      << "  viewport_width=" << options.viewport_width << '\n'
                      << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                      << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                      << "  non_background_pixels="
                      << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "capture failed: " << error.what() << '\n';
            return 1;
        }
    }

#if !defined(JELLYFRAME_ENABLE_SCRIPTING)
    if (!options.script_path.empty()) {
        std::cerr << "this build was compiled without JELLYFRAME_BUILD_SCRIPTING=ON\n";
        return 1;
    }
#endif

    if (options.app_path.empty()) {
        if (!positional.empty()) {
            options.html_path = positional[0];
        }
        if (positional.size() >= 2) {
            options.css_path = positional[1];
        }
        if (positional.size() >= 3) {
            options.viewport_width = parse_int_arg(positional[2].c_str(), options.viewport_width);
        }
        if (positional.size() >= 4) {
            options.viewport_height = parse_int_arg(positional[3].c_str(), options.viewport_height);
        }
    }

    BrowserApp app(std::move(options));
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!app.initialize(instance, SW_SHOWNORMAL)) {
        std::cerr << "failed to create Win32 browser window\n";
        return 1;
    }
    return app.run();
}
