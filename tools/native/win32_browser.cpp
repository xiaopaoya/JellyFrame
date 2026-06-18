#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "core/budget.h"
#include "core/css_parser.h"
#include "core/display_invalidation.h"
#include "core/dirty_region.h"
#include "core/document_script.h"
#include "core/document_style.h"
#include "core/frame_update.h"
#include "core/html_parser.h"
#include "core/input.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "app_package.h"
#include "example_css_io.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

struct BrowserOptions {
    bool capture = false;
    std::string output_path;
    std::string html_path = "samples/pages/modern/app_shell.html";
    std::string css_path = "samples/pages/modern/app_shell.css";
    std::string script_path;
    std::string app_path;
    int viewport_width = 390;
    int viewport_height = 640;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
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

LoadedPage load_page(const BrowserOptions& options, const HostBudgets& budgets) {
    HtmlParser html_parser;
    CssParser css_parser;
    LoadedPage page;
    if (!options.app_path.empty()) {
        const auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
        page.package_mode = true;
        page.package_stats = {};
        page.package_context.root = package.root;
        page.package_context.base_url = package.manifest.entry;
        page.package_context.max_input_bytes = kMaxInputBytes;
        page.package_context.stats = &page.package_stats;
        std::string html;
        if (!jellyframe_example::load_package_resource(package.manifest.entry,
                                                       package.manifest.entry,
                                                       html,
                                                       &page.package_context)) {
            throw std::runtime_error("failed to load package entry: " + package.manifest.entry);
        }
        page.document = html_parser.parse(html, html_parser_options_from_budgets(budgets));
        const std::string css = combine_author_css(
            {}, *page.document, jellyframe_example::load_package_stylesheet, &page.package_context);
        page.stylesheet = css_parser.parse(css, css_parser_options_from_budgets(budgets));
        page.script_base_dir = package.root / std::filesystem::path(package.manifest.entry).parent_path().relative_path();
        return page;
    }

    page.document = html_parser.parse(read_file_limited(options.html_path), html_parser_options_from_budgets(budgets));
    page.stylesheet = css_parser.parse(
        jellyframe_example::read_author_css_for_document(options.css_path, *page.document, kMaxInputBytes),
        css_parser_options_from_budgets(budgets));
    const std::filesystem::path html_path(options.html_path);
    page.script_base_dir = html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
    return page;
}

FrameBuffer render_page_with_gdi_text(const BrowserOptions& options) {
    const HostBudgets budgets = desktop_browser_budgets();
    LoadedPage page = load_page(options, budgets);
    StyleResolver resolver(std::move(page.stylesheet));

    RenderTreeBuilder render_builder(resolver, render_tree_options_from_budgets(budgets));
    auto render_tree = render_builder.build(*page.document);
    LayoutEngine layout_engine(resolver,
                               TextMeasureProvider{measure_text_with_gdi, nullptr},
                               layout_engine_options_from_budgets(budgets));
    auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width);
    LayerTreeBuilder layer_builder(layer_tree_options_from_budgets(budgets));
    auto layer_tree = layer_builder.build(*layout_tree);

    SoftwareCompositor compositor(TextPainter{draw_text_with_gdi, nullptr});
    return compositor.render(*layer_tree,
                             options.viewport_width,
                             options.viewport_height,
                             page_background_color(*page.document, resolver));
}

FrameBuffer render_page_with_gdi_text(const std::string& html_path,
                                      const std::string& css_path,
                                      int viewport_width,
                                      int min_viewport_height) {
    BrowserOptions options;
    options.html_path = html_path;
    options.css_path = css_path;
    options.viewport_width = viewport_width;
    options.viewport_height = min_viewport_height;
    return render_page_with_gdi_text(options);
}

class BrowserApp {
public:
    explicit BrowserApp(BrowserOptions options)
        : options_(std::move(options)) {}

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
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            KillTimer(hwnd_, kScriptTimerId);
#endif
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

    void rebuild() {
        try {
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            KillTimer(hwnd_, kScriptTimerId);
            script_runtime_.reset();
#endif
            LoadedPage page = load_page(options_, budgets_);
            document_ = std::move(page.document);
            style_resolver_ = std::make_unique<StyleResolver>(std::move(page.stylesheet));
            page_background_ = page_background_color(*document_, *style_resolver_);
            render_tree_.reset();
            layout_tree_.reset();
            layer_tree_.reset();
            input_.reset();
            dirty_region_statistics_ = DirtyRegionStatistics{};

            document_->add_event_listener("click", [this](Event& event) {
                std::cout << "click target=" << describe_node(event.target()) << '\n';
                set_title("clicked " + describe_node(event.target()));
            });

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            jellyframe_example::ScriptLoadContext script_context;
            script_context.base_dir = page.script_base_dir.empty() ? std::filesystem::current_path() : page.script_base_dir;
            script_context.max_input_bytes = kMaxInputBytes;
            std::vector<DocumentScript> document_scripts =
                page.package_mode
                    ? collect_classic_scripts(
                          *document_, jellyframe_example::load_package_script, &page.package_context)
                    : collect_classic_scripts(*document_, jellyframe_example::load_linked_script, &script_context);
            if (!document_scripts.empty() || !options_.script_path.empty()) {
                script_runtime_ = std::make_unique<JerryScriptRuntime>(budgets_);
                script_runtime_->set_host_time_ms(GetTickCount64());
                script_runtime_->bind_document(*document_);
                for (const DocumentScript& script : document_scripts) {
                    const ScriptEvaluationResult result = script_runtime_->eval(script.source, script.name);
                    if (!result.ok) {
                        std::cerr << "document script failed: " << result.error << '\n';
                        set_title("script error: " + result.error);
                        break;
                    }
                }
                if (!options_.script_path.empty()) {
                    const ScriptEvaluationResult result =
                        script_runtime_->eval(read_file_limited(options_.script_path), options_.script_path);
                    if (!result.ok) {
                        std::cerr << "script failed: " << result.error << '\n';
                        set_title("script error: " + result.error);
                    }
                }
                SetTimer(hwnd_, kScriptTimerId, kScriptTimerPeriodMs, nullptr);
            }
#endif
            render_current(nullptr, nullptr, nullptr);
        } catch (const std::exception& error) {
            std::cerr << "rebuild failed: " << error.what() << '\n';
            set_title(std::string("error: ") + error.what());
        }
    }

    void render_current(const Node* hovered_node, const Node* active_node, const Node* focused_node) {
        if (document_ == nullptr || style_resolver_ == nullptr) {
            return;
        }
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

        LayerTreeBuilder layer_builder(layer_tree_options_from_budgets(budgets_));
        SoftwareCompositor compositor(TextPainter{draw_text_with_gdi, nullptr});
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
        RenderTreeBuilder render_builder(*style_resolver_, render_tree_options_from_budgets(budgets_));
        auto next_render_tree = render_builder.build(*document_);
        LayoutEngine layout_engine(*style_resolver_,
                                   TextMeasureProvider{measure_text_with_gdi, nullptr},
                                   layout_engine_options_from_budgets(budgets_));
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
        if (!input_) {
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
        if (!input_) {
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
        if (!input_) {
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
        set_title("up " + describe_node(target));
    }

    void handle_wheel(WPARAM wparam, LPARAM lparam) {
        if (!input_) {
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
        if (!input_ || wparam < 0x20 || wparam == 0x7f) {
            return;
        }
        const Node* focus = input_->focused_node();
        if (input_->text_input(wide_char_to_utf8(static_cast<wchar_t>(wparam)))) {
            rerender_if_dirty(focus);
        }
    }

    void handle_key_down(WPARAM wparam) {
        if (!input_) {
            return;
        }
        KeyInput key;
        if (wparam == VK_BACK) {
            key.code = KeyCode::Backspace;
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
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ == nullptr) {
            return;
        }
        const std::size_t callbacks = script_runtime_->pump_timers(GetTickCount64(), 8);
        if (callbacks != 0) {
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
    options.html_path = "samples/pages/modern/app_shell.html";
    options.css_path = "samples/pages/modern/app_shell.css";
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
        positional.push_back(arg);
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
                FrameBuffer frame_buffer = render_page_with_gdi_text(options);
                write_image(frame_buffer, options.output_path);
                std::cout << "JellyFrame Win32 browser capture\n"
                          << "  output=" << options.output_path << '\n'
                          << "  viewport_width=" << options.viewport_width << '\n'
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
            FrameBuffer frame_buffer = render_page_with_gdi_text(options.html_path, options.css_path, options.viewport_width,
                                                                  options.viewport_height);
            write_image(frame_buffer, options.output_path);
            std::cout << "JellyFrame Win32 browser capture\n"
                      << "  output=" << options.output_path << '\n'
                      << "  viewport_width=" << options.viewport_width << '\n'
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
