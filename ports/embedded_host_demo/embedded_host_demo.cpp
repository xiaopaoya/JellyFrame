#include "render_core/text_system.h"
#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/pipeline_statistics.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/style.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

constexpr int kViewportWidth = 128;
constexpr int kViewportHeight = 96;

HostBudgets embedded_demo_budgets() {
    HostBudgets budgets;
    budgets.max_dom_nodes = 64;
    budgets.max_css_rules = 32;
    budgets.max_render_objects = 64;
    budgets.max_layout_boxes = 64;
    budgets.max_layers = 16;
    budgets.max_display_commands = 128;
    budgets.max_dirty_rects = 4;
    budgets.max_timers = 4;
    budgets.max_input_events_per_frame = 4;
    budgets.max_timer_callbacks_per_frame = 2;
    budgets.max_event_listeners = 16;
    budgets.max_resource_bytes = 8 * 1024;
    budgets.max_framebuffer_pixels = kViewportWidth * kViewportHeight;
    return budgets;
}

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
    "h1 { grid-column: span 2; margin: 0; font-size: 14px; font-weight: 400; color: #00ff00; text-align: left; }"
    "button, select { height: 24px; border-radius: 4px; border: 1px solid #ffffff;"
    "background: #1f2937; color: #ffffff; text-align: center; }"
    "input { width: 24px; height: 24px; }";

// Complete JFFONT2: space and hand-authored 5x7 A/B coverage strikes.
constexpr std::uint8_t kFontResource[] = {
    0x4a, 0x46, 0x46, 0x4f, 0x4e, 0x54, 0x32, 0x00, 0x02, 0x00, 0x80, 0x00, 0x28, 0x00, 0x01, 0x00,
    0x3e, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00,
    0x46, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x08, 0x00, 0x90, 0x01, 0x08, 0x00, 0x6b, 0x03, 0x00, 0x00,
    0x7d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x92, 0x67, 0x1c, 0x73, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0xee, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf9, 0xff, 0xff, 0xff, 0x05, 0x00, 0x07, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x42, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xee, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf9, 0xff, 0xff, 0xff, 0x05, 0x00, 0x07, 0x00, 0x05, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00,
    0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
};

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
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
    LayerNodePtr layer_tree;
};

Pipeline build_pipeline(const TextSystem& text_system, const HostBudgets& budgets) {
    HtmlParser html_parser;
    CssParser css_parser;
    Pipeline pipeline;
    pipeline.document = html_parser.parse(kHtml, html_parser_options_from_budgets(budgets));
    pipeline.resolver = std::make_unique<StyleResolver>(
        css_parser.parse(kCss, css_parser_options_from_budgets(budgets)));

    RenderTreeBuilder render_tree_builder(*pipeline.resolver, render_tree_options_from_budgets(budgets));
    pipeline.render_tree = render_tree_builder.build(*pipeline.document);
    auto options = layout_engine_options_from_budgets(budgets);
    options.text_system = &text_system;
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    LayoutEngine layout_engine(*pipeline.resolver, options);
#else
    LayoutEngine layout_engine(*pipeline.resolver, TextMeasureProvider{}, options);
#endif
    pipeline.layout_tree = layout_engine.layout(*pipeline.render_tree, kViewportWidth);
    LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
    layer_options.text_system = &text_system;
    LayerTreeBuilder layer_tree_builder(layer_options);
    pipeline.layer_tree = layer_tree_builder.build(*pipeline.layout_tree);
    return pipeline;
}

FrameBuffer render_to_rgba(const LayerNode& layer_tree, const TextSystem& text_system, const HostBudgets& budgets) {
    auto options = software_compositor_options_from_budgets(budgets);
    options.text_system = &text_system;
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    SoftwareCompositor compositor(ImagePainter{}, options);
#else
    SoftwareCompositor compositor(TextPainter{}, ImagePainter{}, options);
#endif
    return compositor.render(layer_tree, kViewportWidth, kViewportHeight, Color{0, 0, 0, 255});
}

std::vector<std::uint8_t> make_rgb565_buffer() {
    return std::vector<std::uint8_t>(
        embedded_framebuffer_min_size(kViewportWidth, kViewportHeight, EmbeddedPixelFormat::Rgb565));
}

} // namespace

int run_embedded_host_demo() {
    // Borrowed resource bytes and registration outlive layout, layers and paint.
    TextFontFace face;
    face.resource_id = 1;
    TextSystem text_system;
    if (face.resource.load(kFontResource, sizeof(kFontResource)) != FontResourceStatus::Loaded ||
        text_system.bind(&face, 1) != TextSystemStatus::Ok) {
        std::cerr << "embedded host demo failed: Core font binding failed\n";
        return 1;
    }
    const HostBudgets budgets = embedded_demo_budgets();
    Pipeline pipeline = build_pipeline(text_system, budgets);

    Node* button = find_by_id(*pipeline.document, "button");
    Node* checkbox = find_by_id(*pipeline.document, "check");
    Node* select = find_by_id(*pipeline.document, "choice");
    if (button == nullptr || checkbox == nullptr || select == nullptr) {
        std::cerr << "embedded host demo failed: fixture nodes missing\n";
        return 1;
    }

    int button_clicks = 0;
    button->add_event_listener("click", [&](Event&) { ++button_clicks; });

    FrameBuffer rgba = render_to_rgba(*pipeline.layer_tree, text_system, budgets);
    if (rgba.width <= 0 || rgba.height <= 0) {
        std::cerr << "embedded host demo failed: framebuffer budget exceeded\n";
        return 1;
    }
    InputController input(*pipeline.layer_tree);
    input.focus_next();
    input.activate_focused();
    input.focus_next();
    input.activate_focused();
    input.focus_next();
    input.activate_focused();

    // Green belongs only to heading ink, so controls/backgrounds cannot pass.
    std::size_t visible_glyph_pixels = 0;
    for (const auto& pixel : rgba.pixels)
        if (pixel.g > 128 && pixel.r == 0 && pixel.b == 0) ++visible_glyph_pixels;
    // Advanced forms opens the popup; the cropped base control cycles options.
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    constexpr const char* expected_select = "A";
#else
    constexpr const char* expected_select = "B";
#endif
    if (visible_glyph_pixels < 20 || button_clicks != 1 ||
        !ensure_form_control_state(*checkbox).checked || form_control_display_text(*select) != expected_select) {
        std::cerr << "embedded host demo failed: visible glyph/input proof failed: ink="
                  << visible_glyph_pixels << " clicks=" << button_clicks
                  << " checked=" << ensure_form_control_state(*checkbox).checked
                  << " select=" << form_control_display_text(*select) << "\n";
        return 1;
    }
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

    if (flush_probe_state.count != 1 || flush_probe_state.total_area != kViewportWidth * kViewportHeight ||
        rgb565.size() != static_cast<std::size_t>(kViewportWidth * kViewportHeight * 2)) {
        std::cerr << "embedded host demo failed: framebuffer/flush proof failed\n";
        return 1;
    }
    std::cout << "  visible_glyph_pixels=" << visible_glyph_pixels << '\n';
    std::cout << "JellyFrame embedded host demo\n";
    std::cout << "  viewport=" << kViewportWidth << "x" << kViewportHeight << '\n';
    std::cout << "  rgb565_bytes=" << rgb565.size() << '\n';
    std::cout << "  flush_count=" << flush_probe_state.count << '\n';
    std::cout << "  flush_area=" << flush_probe_state.total_area << '\n';
    const PipelineStatistics pipeline_statistics = collect_pipeline_statistics(PipelineStatisticsInput{
        pipeline.document.get(),
        pipeline.render_tree.get(),
        pipeline.layout_tree.get(),
        pipeline.layer_tree.get(),
        nullptr,
        &rgba,
        nullptr,
        nullptr,
        nullptr,
        sizeof(kHtml) + sizeof(kCss) + sizeof(kFontResource),
    });
    std::cout << "  estimated_pipeline_bytes=" << pipeline_statistics.estimated_heap_bytes << '\n';
    std::cout << "  framebuffer_bytes=" << pipeline_statistics.framebuffer_bytes << '\n';
    std::cout << "  resource_bytes=" << pipeline_statistics.resource_bytes << '\n';
    std::cout << "  button_clicks=" << button_clicks << '\n';
    std::cout << "  checkbox_checked=" << (ensure_form_control_state(*checkbox).checked ? "true" : "false") << '\n';
    std::cout << "  select_value=" << form_control_display_text(*select) << '\n';
    std::cout << "  non_background_pixels=" << count_non_background_pixels(rgba, Color{0, 0, 0, 255}) << '\n';
    return 0;
}

int main() {
    try {
        return run_embedded_host_demo();
    } catch (const std::exception& error) {
        std::cerr << "embedded host demo failed: " << error.what() << '\n';
        return 1;
    }
}
