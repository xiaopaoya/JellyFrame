#include "core/bitmap_font.h"
#include "core/css_parser.h"
#include "core/embedded_framebuffer.h"
#include "core/form_control.h"
#include "core/html_parser.h"
#include "core/input.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace wearweb;

namespace {

constexpr int kViewportWidth = 128;
constexpr int kViewportHeight = 96;

constexpr char kHtml[] =
    "<body>"
    "<section class='app'>"
    "<h1>A</h1>"
    "<button id='button'>A</button>"
    "<input id='check' type='checkbox'>"
    "<select id='choice'><option>A</option><option>B</option></select>"
    "</section>"
    "</body>";

constexpr char kCss[] =
    "body { margin: 0; padding: 4px; background: #000000; color: #ffffff; }"
    ".app { display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px; }"
    "h1 { grid-column: span 2; margin: 0; font-size: 14px; text-align: center; }"
    "button, select { height: 24px; border-radius: 4px; border: 1px solid #ffffff;"
    "background: #1f2937; color: #ffffff; text-align: center; }"
    "input { width: 24px; height: 24px; }";

constexpr std::uint8_t kRowsA[] = {
    0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
};

constexpr std::uint8_t kRowsB[] = {
    0xf0, 0x88, 0x88, 0xf0, 0x88, 0x88, 0xf0,
};

constexpr BitmapFontGlyph kGlyphs[] = {
    BitmapFontGlyph{0x41, 5, 7, 6, 1, kRowsA},
    BitmapFontGlyph{0x42, 5, 7, 6, 1, kRowsB},
};

constexpr BitmapFont kFont{kGlyphs, 2, 8, 6};

struct FlushProbe {
    int count = 0;
    int total_area = 0;
};

bool flush_probe(Rect dirty, void* context) {
    auto* probe = static_cast<FlushProbe*>(context);
    ++probe->count;
    probe->total_area += dirty.width * dirty.height;
    return true;
}

Node* find_by_id(Node& node, const std::string& id) {
    if (node.attribute("id") == id) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_by_id(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

struct Pipeline {
    std::unique_ptr<Node> document;
    std::unique_ptr<StyleResolver> resolver;
    std::unique_ptr<RenderObject> render_tree;
    std::unique_ptr<LayoutBox> layout_tree;
    std::unique_ptr<LayerNode> layer_tree;
};

Pipeline build_pipeline(BitmapFontContext& font_context) {
    HtmlParser html_parser;
    CssParser css_parser;
    Pipeline pipeline;
    pipeline.document = html_parser.parse(kHtml);
    pipeline.resolver = std::make_unique<StyleResolver>(css_parser.parse(kCss));

    RenderTreeBuilder render_tree_builder(*pipeline.resolver);
    pipeline.render_tree = render_tree_builder.build(*pipeline.document);
    LayoutEngine layout_engine(*pipeline.resolver,
                               TextMeasureProvider{bitmap_font_measure_callback, &font_context});
    pipeline.layout_tree = layout_engine.layout(*pipeline.render_tree, kViewportWidth);
    LayerTreeBuilder layer_tree_builder;
    pipeline.layer_tree = layer_tree_builder.build(*pipeline.layout_tree);
    return pipeline;
}

FrameBuffer render_to_rgba(const LayerNode& layer_tree, BitmapFontContext& font_context) {
    SoftwareCompositor compositor(TextPainter{bitmap_font_paint_callback, &font_context});
    return compositor.render(layer_tree, kViewportWidth, kViewportHeight, Color{0, 0, 0, 255});
}

std::vector<std::uint8_t> make_rgb565_buffer() {
    return std::vector<std::uint8_t>(
        embedded_framebuffer_min_size(kViewportWidth, kViewportHeight, EmbeddedPixelFormat::Rgb565));
}

} // namespace

int main() {
    BitmapFontContext font_context{&kFont, 2};
    Pipeline pipeline = build_pipeline(font_context);

    Node* button = find_by_id(*pipeline.document, "button");
    Node* checkbox = find_by_id(*pipeline.document, "check");
    Node* select = find_by_id(*pipeline.document, "choice");
    if (button == nullptr || checkbox == nullptr || select == nullptr) {
        std::cerr << "embedded host demo failed: fixture nodes missing\n";
        return 1;
    }

    int button_clicks = 0;
    button->add_event_listener("click", [&](Event&) { ++button_clicks; });

    InputController input(*pipeline.layer_tree);
    input.focus_next();
    input.activate_focused();
    input.focus_next();
    input.activate_focused();
    input.focus_next();
    input.activate_focused();

    FrameBuffer rgba = render_to_rgba(*pipeline.layer_tree, font_context);
    std::vector<std::uint8_t> rgb565 = make_rgb565_buffer();
    FlushProbe flush_probe_state;
    EmbeddedFrameBufferSink sink;
    sink.target = EmbeddedFrameBufferTarget{
        kViewportWidth,
        kViewportHeight,
        EmbeddedPixelFormat::Rgb565,
        rgb565.data(),
        rgb565.size(),
        0,
    };
    sink.flush = flush_probe;
    sink.flush_context = &flush_probe_state;

    const Rect full_dirty{0, 0, kViewportWidth, kViewportHeight};
    if (!present_frame(rgba, embedded_frame_sink(sink), &full_dirty, 1)) {
        std::cerr << "embedded host demo failed: present_frame failed\n";
        return 1;
    }

    std::cout << "WearWeb embedded host demo\n";
    std::cout << "  viewport=" << kViewportWidth << "x" << kViewportHeight << '\n';
    std::cout << "  rgb565_bytes=" << rgb565.size() << '\n';
    std::cout << "  flush_count=" << flush_probe_state.count << '\n';
    std::cout << "  flush_area=" << flush_probe_state.total_area << '\n';
    std::cout << "  button_clicks=" << button_clicks << '\n';
    std::cout << "  checkbox_checked=" << (ensure_form_control_state(*checkbox).checked ? "true" : "false") << '\n';
    std::cout << "  select_value=" << form_control_display_text(*select) << '\n';
    std::cout << "  non_background_pixels=" << count_non_background_pixels(rgba, Color{0, 0, 0, 255}) << '\n';
    return 0;
}
