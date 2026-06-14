#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/input.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#if defined(WEARWEB_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "example_css_io.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace wearweb;

namespace {

constexpr std::size_t kMaxInputBytes = 1024 * 1024;
constexpr wchar_t kWindowClassName[] = L"WearWebWin32Browser";
constexpr UINT_PTR kScriptTimerId = 1;
constexpr UINT kScriptTimerPeriodMs = 16;

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
    HFONT font = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    HGDIOBJ old_font = font != nullptr ? SelectObject(memory_dc, font) : nullptr;
    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, RGB(255, 255, 255));
    const UINT flags = rect.height > 24
        ? (DT_LEFT | DT_WORDBREAK | DT_NOPREFIX)
        : (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

FrameBuffer render_page_with_gdi_text(const std::string& html_path,
                                      const std::string& css_path,
                                      int viewport_width,
                                      int min_viewport_height) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(read_file_limited(html_path));
    Stylesheet stylesheet = css_parser.parse(
        wearweb_example::read_author_css_for_document(css_path, *document, kMaxInputBytes));
    StyleResolver resolver(std::move(stylesheet));

    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
    LayerTreeBuilder layer_builder;
    auto layer_tree = layer_builder.build(*layout_tree);

    const int output_height = std::max(min_viewport_height, layout_tree->rect.height);
    SoftwareCompositor compositor(TextPainter{draw_text_with_gdi, nullptr});
    return compositor.render(*layer_tree, viewport_width, output_height, Color{255, 255, 255, 255});
}

class BrowserApp {
public:
    BrowserApp(std::string html_path, std::string css_path, std::string script_path)
        : html_path_(std::move(html_path)),
          css_path_(std::move(css_path)),
          script_path_(std::move(script_path)) {}

    bool initialize(HINSTANCE instance, int show_command) {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = BrowserApp::window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClassName;
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&window_class);

        hwnd_ = CreateWindowExW(0,
                                kWindowClassName,
                                L"WearWeb Win32 Browser",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                420,
                                320,
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
    std::string html_path_;
    std::string css_path_;
    std::string script_path_;
    int viewport_width_ = 1;
    int viewport_height_ = 1;
    int scroll_y_ = 0;

#if defined(WEARWEB_ENABLE_SCRIPTING)
    std::unique_ptr<JerryScriptRuntime> script_runtime_;
#endif
    std::unique_ptr<Node> document_;
    std::unique_ptr<StyleResolver> style_resolver_;
    std::unique_ptr<RenderObject> render_tree_;
    std::unique_ptr<LayoutBox> layout_tree_;
    std::unique_ptr<LayerNode> layer_tree_;
    std::unique_ptr<InputController> input_;
    FrameBuffer frame_buffer_;
    std::vector<std::uint32_t> blit_pixels_;

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
#if defined(WEARWEB_ENABLE_SCRIPTING)
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
#if defined(WEARWEB_ENABLE_SCRIPTING)
            KillTimer(hwnd_, kScriptTimerId);
            script_runtime_.reset();
#endif
            HtmlParser html_parser;
            CssParser css_parser;
            document_ = html_parser.parse(read_file_limited(html_path_));
            Stylesheet stylesheet = css_parser.parse(
                wearweb_example::read_author_css_for_document(css_path_, *document_, kMaxInputBytes));
            style_resolver_ = std::make_unique<StyleResolver>(std::move(stylesheet));

            document_->add_event_listener("click", [this](Event& event) {
                std::cout << "click target=" << describe_node(event.target()) << '\n';
                set_title("clicked " + describe_node(event.target()));
            });

#if defined(WEARWEB_ENABLE_SCRIPTING)
            if (!script_path_.empty()) {
                script_runtime_ = std::make_unique<JerryScriptRuntime>();
                script_runtime_->set_host_time_ms(GetTickCount64());
                script_runtime_->bind_document(*document_);
                const ScriptEvaluationResult result = script_runtime_->eval(read_file_limited(script_path_), script_path_);
                if (!result.ok) {
                    std::cerr << "script failed: " << result.error << '\n';
                    set_title("script error: " + result.error);
                }
                SetTimer(hwnd_, kScriptTimerId, kScriptTimerPeriodMs, nullptr);
            }
#endif
            render_current(nullptr);
        } catch (const std::exception& error) {
            std::cerr << "rebuild failed: " << error.what() << '\n';
            set_title(std::string("error: ") + error.what());
        }
    }

    void render_current(const Node* focused_node) {
        if (document_ == nullptr || style_resolver_ == nullptr) {
            return;
        }
        RenderTreeBuilder render_builder(*style_resolver_);
        render_tree_ = render_builder.build(*document_);
        LayoutEngine layout_engine(*style_resolver_);
        layout_tree_ = layout_engine.layout(*render_tree_, viewport_width_);
        LayerTreeBuilder layer_builder;
        layer_tree_ = layer_builder.build(*layout_tree_);

        scroll_y_ = clamp_scroll_y(scroll_y_);
        const int content_height = std::max(viewport_height_, layout_tree_->rect.height);
        SoftwareCompositor compositor(TextPainter{draw_text_with_gdi, nullptr});
        frame_buffer_ = compositor.render(*layer_tree_,
                                          viewport_width_,
                                          content_height,
                                          Color{255, 255, 255, 255});
        input_ = std::make_unique<InputController>(*layer_tree_);
        input_->set_focused_node(focused_node);
        update_blit_pixels();
        clear_dirty_flags(*document_);
    }

    int max_scroll_y() const {
        return std::max(0, frame_buffer_.height - viewport_height_);
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

    void update_blit_pixels() {
        blit_pixels_.assign(static_cast<std::size_t>(viewport_width_) * static_cast<std::size_t>(viewport_height_),
                            color_to_bgrx(Color{255, 255, 255, 255}));
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
#if defined(WEARWEB_ENABLE_SCRIPTING)
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
        render_current(focused_node);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void set_title(const std::string& status) {
        SetWindowTextW(hwnd_, utf8_to_wide("WearWeb Win32 Browser - " + status).c_str());
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--capture") {
        const std::string output_path = argv[2];
        const std::string html_path = argc >= 4 ? argv[3] : "examples/modern_cases/app_shell.html";
        const std::string css_path = argc >= 5 ? argv[4] : "examples/modern_cases/app_shell.css";
        const int viewport_width = argc >= 6 ? parse_int_arg(argv[5], 390) : 390;
        const int min_height = argc >= 7 ? parse_int_arg(argv[6], 640) : 640;
        try {
            FrameBuffer frame_buffer = render_page_with_gdi_text(html_path, css_path, viewport_width, min_height);
            write_image(frame_buffer, output_path);
            std::cout << "WearWeb Win32 browser capture\n"
                      << "  output=" << output_path << '\n'
                      << "  viewport_width=" << viewport_width << '\n'
                      << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                      << "  non_background_pixels="
                      << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "capture failed: " << error.what() << '\n';
            return 1;
        }
    }

    std::string script_path;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--script" || arg == "-s") {
            if (i + 1 >= argc) {
                std::cerr << "--script requires a script file path\n";
                return 1;
            }
            script_path = argv[++i];
            continue;
        }
        positional.push_back(arg);
    }

#if !defined(WEARWEB_ENABLE_SCRIPTING)
    if (!script_path.empty()) {
        std::cerr << "this build was compiled without WEARWEB_BUILD_SCRIPTING=ON\n";
        return 1;
    }
#endif

    const std::string html_path = !positional.empty() ? positional[0] : "examples/modern_cases/app_shell.html";
    const std::string css_path = positional.size() >= 2 ? positional[1] : "examples/modern_cases/app_shell.css";

    BrowserApp app(html_path, css_path, script_path);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!app.initialize(instance, SW_SHOWNORMAL)) {
        std::cerr << "failed to create Win32 browser window\n";
        return 1;
    }
    return app.run();
}
