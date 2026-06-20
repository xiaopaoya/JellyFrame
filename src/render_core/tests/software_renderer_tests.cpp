#include "render_core/css_parser.h"
#include "render_core/bitmap_font.h"
#include "render_core/bitmap_font_resource.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool rejecting_text_painter(FrameBuffer&,
                            Rect,
                            Color,
                            const std::string&,
                            int,
                            int,
                            TextCommandAlign,
                            bool,
                            void*) {
    return false;
}

struct ImagePaintProbe {
    std::uint32_t expected_handle = 0;
    ObjectFit fit = ObjectFit::Fill;
    int calls = 0;
};

bool probe_image_painter(FrameBuffer& target,
                         Rect rect,
                         std::uint32_t image_handle,
                         ObjectFit object_fit,
                         void* raw_context) {
    auto* probe = static_cast<ImagePaintProbe*>(raw_context);
    if (probe == nullptr || image_handle != probe->expected_handle) {
        return false;
    }
    probe->fit = object_fit;
    ++probe->calls;
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        for (int x = rect.x; x < rect.x + rect.width; ++x) {
            if (target.contains(x, y)) {
                target.pixel(x, y) = Color{220, 38, 38, 255};
            }
        }
    }
    return true;
}

const LayoutBox* find_first_text_box(const LayoutBox& box) {
    if (box.node != nullptr && box.node->type == NodeType::Text) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_text_box(*child);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void fill_rect_rasterizes_pixels() {
    FrameBuffer frame_buffer(8, 8, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{2, 2, 3, 3};
    command.color = Color{0, 0, 0, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 8, 8});

    check(frame_buffer.pixel(2, 2).r == 0, "fill rect writes covered pixel");
    check(frame_buffer.pixel(0, 0).r == 255, "fill rect leaves outside pixel");
}

void rounded_stroke_keeps_corner_pixels_clear() {
    FrameBuffer frame_buffer(12, 12, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::StrokeRect;
    command.rect = Rect{1, 1, 10, 10};
    command.color = Color{0, 0, 0, 255};
    command.border_radius = 5;
    command.stroke_width = 2;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 12, 12});

    check(frame_buffer.pixel(1, 1).r == 255, "rounded stroke leaves outer corner clear");
    check(frame_buffer.pixel(6, 2).r == 0, "rounded stroke paints top edge");
}

void source_over_alpha_composites() {
    FrameBuffer frame_buffer(1, 1, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{0, 0, 1, 1};
    command.color = Color{0, 0, 0, 128};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 1, 1});

    const Color pixel = frame_buffer.pixel(0, 0);
    check(pixel.r >= 126 && pixel.r <= 128, "alpha blend red channel");
    check(pixel.g >= 126 && pixel.g <= 128, "alpha blend green channel");
    check(pixel.b >= 126 && pixel.b <= 128, "alpha blend blue channel");
    check(pixel.a == 255, "opaque destination remains opaque");
}

void clipping_limits_rasterization() {
    FrameBuffer frame_buffer(4, 4, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{0, 0, 4, 4};
    command.color = Color{0, 0, 0, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{1, 1, 2, 2});

    check(frame_buffer.pixel(0, 0).r == 255, "clip keeps outside pixel");
    check(frame_buffer.pixel(1, 1).r == 0, "clip paints inside pixel");
    check(frame_buffer.pixel(2, 2).r == 0, "clip paints opposite inside pixel");
    check(frame_buffer.pixel(3, 3).r == 255, "clip keeps far outside pixel");
}

void image_command_uses_injected_painter() {
    FrameBuffer frame_buffer(8, 8, Color{255, 255, 255, 255});
    ImagePaintProbe probe{42, ObjectFit::Fill, 0};
    SoftwareRasterizer rasterizer({}, ImagePainter{probe_image_painter, &probe});
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{1, 2, 3, 2};
    command.image_handle = 42;
    command.object_fit = ObjectFit::Contain;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 8, 8});

    check(probe.calls == 1, "image painter called once");
    check(probe.fit == ObjectFit::Contain, "image painter receives object-fit");
    check(frame_buffer.pixel(1, 2).r == 220 && frame_buffer.pixel(1, 2).g == 38,
          "image painter writes covered pixel");
    check(frame_buffer.pixel(0, 0).r == 255, "image painter leaves outside pixel");
}

void image_command_falls_back_without_painter() {
    VectorDiagnosticSink diagnostics;
    FrameBuffer frame_buffer(4, 4, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer({}, &diagnostics);
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{1, 1, 2, 2};
    command.image_handle = 9;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 4, 4});

    check(frame_buffer.pixel(1, 1).r == 226 && frame_buffer.pixel(1, 1).g == 232,
          "image fallback paints placeholder");
    check(has_diagnostic_code(diagnostics, "paint-image-fallback"), "image fallback diagnostic");
}

void compositor_renders_pipeline_non_empty() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='card'><h1>OK</h1><p>Paint</p></section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { background: white; padding: 4px; }"
        ".card { background: #e5e7eb; border: 1px solid #111827; padding: 8px; opacity: .8; }"
        "h1 { color: #111827; }"
        "p { color: #2563eb; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    SoftwareCompositor compositor;
    FrameBuffer frame_buffer = compositor.render(*layer_tree, 120, 96, Color{255, 255, 255, 255});

    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0, "pipeline renders non-background pixels");
}

void wrapped_text_layout_keeps_descent_padding() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p>Long wearable interface text wraps onto several compact display lines.</p></body>");
    StyleResolver resolver(css_parser.parse("p { font-size: 18px; width: 90px; margin: 0; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    const LayoutBox* text_box = find_first_text_box(*layout_tree);
    check(text_box != nullptr, "text layout box exists");
    check(text_box->rect.height > 44, "wrapped text keeps descent padding");
}

bool fixed_text_measure(const std::string&,
                        int,
                        int font_weight,
                        TextMetrics* metrics,
                        void*) {
    if (metrics == nullptr || font_weight < 600) {
        return false;
    }
    metrics->width = 32;
    metrics->line_height = 21;
    return true;
}

void layout_uses_injected_text_measurement() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p><strong>Measured</strong></p></body>");
    StyleResolver resolver(css_parser.parse("p { width: 120px; margin: 0; } strong { font-weight: 700; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver, TextMeasureProvider{fixed_text_measure, nullptr});
    auto layout_tree = layout_engine.layout(*render_tree, 160);

    const LayoutBox* text_box = find_first_text_box(*layout_tree);
    check(text_box != nullptr, "measured text box exists");
    check(text_box->rect.width == 33, "layout uses injected text width with paint safety pad");
    check(text_box->rect.height == 21, "layout uses injected line height");
}

void dirty_render_only_updates_requested_clip() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='panel'>A</section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } .panel { width: 80px; height: 40px; background: #000000; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 100);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);

    FrameBuffer frame_buffer(100, 60, Color{255, 255, 255, 255});
    SoftwareCompositor compositor;
    const Rect dirty{0, 0, 40, 60};
    compositor.render_into(*layer_tree, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1);

    check(frame_buffer.pixel(10, 10).r == 0, "dirty clip paints inside requested area");
    check(frame_buffer.pixel(70, 10).r == 255, "dirty clip leaves outside area untouched");
}

DisplayCommand black_fill(Rect rect) {
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = Color{0, 0, 0, 255};
    return command;
}

void compositor_degrades_oversized_offscreen_layers_without_crashing() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 2, 1};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->opacity = 0.5F;
    child->bounds = Rect{0, 0, 2, 1};
    child->display_list.push_back(black_fill(Rect{0, 0, 1, 1}));
    child->display_list.push_back(black_fill(Rect{0, 0, 1, 1}));
    root.children.push_back(std::move(child));

    const Color white{255, 255, 255, 255};
    const FrameBuffer precise = SoftwareCompositor().render(root, 2, 1, white);
    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options options;
    options.max_offscreen_pixels = 1;
    options.diagnostics = &diagnostics;
    const FrameBuffer degraded =
        SoftwareCompositor({}, options).render(root, 2, 1, white);

    check(precise.pixel(0, 0).r > degraded.pixel(0, 0).r,
          "offscreen budget fallback uses bounded direct compositing");
    check(degraded.pixel(1, 0).r == 255, "fallback keeps untouched pixels");
    check(has_diagnostic_code(diagnostics, "paint-offscreen-budget"), "offscreen fallback is reported");
}

void compositor_rejects_oversized_framebuffer_before_allocation() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 4, 4};
    root.display_list.push_back(black_fill(Rect{0, 0, 4, 4}));

    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options rejecting_options;
    rejecting_options.max_framebuffer_pixels = 3;
    rejecting_options.diagnostics = &diagnostics;
    const FrameBuffer rejected =
        SoftwareCompositor({}, rejecting_options).render(root, 4, 4, Color{255, 255, 255, 255});
    const FrameBuffer accepted =
        SoftwareCompositor({}, SoftwareCompositor::Options{16, 0}).render(root, 4, 4, Color{255, 255, 255, 255});

    check(rejected.width == 0 && rejected.height == 0 && rejected.pixels.empty(),
          "framebuffer budget rejects oversized render before allocation");
    check(accepted.width == 4 && accepted.height == 4, "framebuffer at budget renders normally");
    check(has_diagnostic_code(diagnostics, "paint-framebuffer-budget"), "framebuffer rejection is reported");
}

void rasterizer_reports_text_fallback() {
    VectorDiagnosticSink diagnostics;
    SoftwareRasterizer rasterizer({}, &diagnostics);
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 80, 20};
    command.color = Color{0, 0, 0, 255};
    command.text = "\xe4\xb8\xad\xe6\x96\x87";
    command.font_size = 14;
    command.text_single_line = true;
    FrameBuffer frame(80, 20, Color{255, 255, 255, 255});

    rasterizer.rasterize(command, frame, Rect{0, 0, 80, 20});

    check(has_diagnostic_code(diagnostics, "paint-non-ascii-fallback"), "non-ascii fallback is reported");

    VectorDiagnosticSink backend_diagnostics;
    SoftwareRasterizer rejecting_rasterizer(TextPainter{rejecting_text_painter, nullptr}, &backend_diagnostics);
    command.text = "ASCII";
    rejecting_rasterizer.rasterize(command, frame, Rect{0, 0, 80, 20});
    check(has_diagnostic_code(backend_diagnostics, "paint-text-backend-failed"),
          "text backend rejection is reported");
}

struct FrameSinkProbe {
    int width = 0;
    int height = 0;
    std::size_t dirty_count = 0;
};

bool probe_present(const HostFrameBufferView& frame,
                   const Rect*,
                   std::size_t dirty_count,
                   void* context) {
    auto* probe = static_cast<FrameSinkProbe*>(context);
    probe->width = frame.width;
    probe->height = frame.height;
    probe->dirty_count = dirty_count;
    return frame.pixels != nullptr;
}

void frame_sink_receives_framebuffer_view_and_dirty_rects() {
    FrameBuffer frame_buffer(8, 6, Color{255, 255, 255, 255});
    FrameSinkProbe probe;
    const HostFrameSink sink{probe_present, &probe};
    const Rect dirty{1, 1, 2, 2};

    check(present_frame(frame_buffer, sink, &dirty, 1), "frame sink present succeeds");
    check(probe.width == 8 && probe.height == 6, "frame sink receives dimensions");
    check(probe.dirty_count == 1, "frame sink receives dirty count");
}

void bitmap_font_backend_measures_and_paints() {
    static constexpr std::uint8_t rows_a[] = {
        0b01000000,
        0b10100000,
        0b11100000,
        0b10100000,
        0b10100000,
    };
    static constexpr std::uint8_t rows_cjk[] = {
        0b11111111, 0b10000000,
        0b00010000, 0b00000000,
        0b11111111, 0b10000000,
        0b00010000, 0b00000000,
        0b11111111, 0b10000000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 3, 5, 4, 1, rows_a},
        BitmapFontGlyph{0x4e2d, 9, 5, 10, 2, rows_cjk},
    };
    static constexpr BitmapFont font{glyphs, 2, 6, 4};
    BitmapFontContext context{&font, 2};

    TextMetrics metrics;
    check(bitmap_font_measure_callback("AA", 12, 400, &metrics, &context), "bitmap font measure callback succeeds");
    check(metrics.width == 16 && metrics.line_height == 12, "bitmap font metrics scale advances");
    check(bitmap_font_measure_callback("\xe4\xb8\xad?", 12, 400, &metrics, &context),
          "bitmap font measures utf-8 text");
    check(metrics.width == 28 && metrics.line_height == 12, "wide utf-8 glyph and fallback advance are stable");

    FrameBuffer frame_buffer(32, 16, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 32, 16};
    command.color = Color{0, 0, 0, 255};
    command.text = "A";
    command.font_size = 12;
    command.text_align = TextCommandAlign::Center;
    command.text_single_line = true;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 32, 16});

    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0,
          "bitmap font painter writes pixels");
    check(frame_buffer.pixel(0, 0).r == 255, "centered bitmap glyph leaves left edge empty");

    FrameBuffer utf8_frame_buffer(40, 16, Color{255, 255, 255, 255});
    DisplayCommand utf8_command;
    utf8_command.type = DisplayCommandType::Text;
    utf8_command.rect = Rect{0, 0, 40, 16};
    utf8_command.color = Color{0, 0, 0, 255};
    utf8_command.text = "\xe4\xb8\xad?";
    utf8_command.font_size = 12;
    utf8_command.text_single_line = true;
    rasterizer.rasterize(utf8_command, utf8_frame_buffer, Rect{0, 0, 40, 16});

    check(count_non_background_pixels(utf8_frame_buffer, Color{255, 255, 255, 255}) > 20,
          "wide utf-8 glyph and missing fallback draw visible pixels");
}

void bitmap_font_lookup_uses_sorted_codepoints() {
    static constexpr std::uint8_t row[] = {0b10000000};
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x20, 1, 1, 2, 1, row},
        BitmapFontGlyph{0x41, 1, 1, 3, 1, row},
        BitmapFontGlyph{0x4e2d, 1, 1, 4, 1, row},
        BitmapFontGlyph{0x1f600, 1, 1, 5, 1, row},
    };
    static constexpr BitmapFont font{glyphs, 4, 2, 1};

    const BitmapFontGlyph* ascii = find_bitmap_glyph(font, 0x41);
    const BitmapFontGlyph* cjk = find_bitmap_glyph(font, 0x4e2d);
    const BitmapFontGlyph* emoji = find_bitmap_glyph(font, 0x1f600);

    check(ascii != nullptr && ascii->advance == 3, "bitmap font lookup finds ASCII glyph");
    check(cjk != nullptr && cjk->advance == 4, "bitmap font lookup finds CJK glyph");
    check(emoji != nullptr && emoji->advance == 5, "bitmap font lookup finds high codepoint glyph");
    check(find_bitmap_glyph(font, 0x42) == nullptr, "bitmap font lookup reports missing glyph");
}

void bitmap_font_scaling_bold_and_missing_glyphs_are_stable() {
    static constexpr std::uint8_t rows_a[] = {
        0b10000000,
        0b10000000,
        0b10000000,
    };
    static constexpr std::uint8_t rows_wide[] = {
        0b11111111, 0b10000000,
        0b10000000, 0b10000000,
        0b11111111, 0b10000000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 1, 3, 2, 1, rows_a},
        BitmapFontGlyph{0xff0c, 9, 3, 10, 2, rows_wide},
    };
    static constexpr BitmapFont font{glyphs, 2, 4, 3};
    BitmapFontContext context{&font, 3};

    const TextMetrics metrics = measure_bitmap_text(context, "A\xef\xbc\x8c?", 18, 700);
    check(metrics.width == 46, "bitmap font metrics include scale, wide punctuation, fallback and bold stroke");
    check(metrics.line_height == 12, "bitmap font metrics scale line height");

    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand normal;
    normal.type = DisplayCommandType::Text;
    normal.rect = Rect{0, 0, 64, 24};
    normal.color = Color{0, 0, 0, 255};
    normal.text = "A";
    normal.font_size = 18;
    normal.font_weight = 400;
    normal.text_single_line = true;

    DisplayCommand bold = normal;
    bold.font_weight = 700;

    FrameBuffer normal_frame(64, 24, Color{255, 255, 255, 255});
    FrameBuffer bold_frame(64, 24, Color{255, 255, 255, 255});
    rasterizer.rasterize(normal, normal_frame, Rect{0, 0, 64, 24});
    rasterizer.rasterize(bold, bold_frame, Rect{0, 0, 64, 24});

    const int normal_pixels = count_non_background_pixels(normal_frame, Color{255, 255, 255, 255});
    const int bold_pixels = count_non_background_pixels(bold_frame, Color{255, 255, 255, 255});
    check(bold_pixels > normal_pixels, "bitmap font bold approximation paints an extra stroke");

    DisplayCommand missing = normal;
    missing.text = "\xf0\x9f\x98\x80";
    FrameBuffer missing_frame(64, 24, Color{255, 255, 255, 255});
    rasterizer.rasterize(missing, missing_frame, Rect{0, 0, 64, 24});
    check(count_non_background_pixels(missing_frame, Color{255, 255, 255, 255}) > 8,
          "missing high-codepoint glyph draws a visible fallback box");
}

void bitmap_font_bold_metrics_include_extra_stroke() {
    static constexpr std::uint8_t rows_w[] = {
        0b10001000,
        0b10001000,
        0b10101000,
        0b10101000,
        0b01010000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x57, 5, 5, 6, 1, rows_w},
    };
    static constexpr BitmapFont font{glyphs, 1, 6, 5};
    BitmapFontContext context{&font, 1};

    const TextMetrics normal = measure_bitmap_text(context, "W", 12, 400);
    const TextMetrics bold = measure_bitmap_text(context, "W", 12, 700);
    check(bold.width == normal.width + 1, "bold bitmap metrics reserve the synthetic stroke");

    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, bold.width, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "W";
    command.font_size = 12;
    command.font_weight = 700;
    command.text_single_line = true;

    FrameBuffer frame_buffer(bold.width, 10, Color{255, 255, 255, 255});
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, bold.width, 10});
    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0,
          "bold synthetic stroke paints inside measured text rect");
}

void jffont_resource_loads_bitmap_font_view() {
    const std::vector<std::uint8_t> bytes = {
        'J', 'F', 'F', 'O', 'N', 'T', '0', 0,
        0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x08, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0x05, 0x07, 0x06, 0x01,
        0x2d, 0x4e, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x01,
        0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
        0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10,
    };

    BitmapFontResource resource;
    check(resource.load_jffont(bytes.data(), bytes.size()), "jffont resource loads");
    check(resource.valid(), "jffont resource exposes valid font");
    BitmapFontContext context{&resource.font(), 1};

    const TextMetrics metrics = measure_bitmap_text(context, "A\xe4\xb8\xad?", 8, 400);
    check(metrics.width == 22 && metrics.line_height == 8, "jffont metrics reuse bitmap backend");

    FrameBuffer frame_buffer(32, 12, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 32, 12};
    command.color = Color{0, 0, 0, 255};
    command.text = "A\xe4\xb8\xad";
    command.font_size = 8;
    command.text_single_line = true;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 32, 12});
    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 10,
          "jffont glyphs paint through bitmap backend");

    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[0] = 0;
    check(!resource.load_jffont(corrupted.data(), corrupted.size()), "jffont rejects bad magic");
    check(!resource.valid(), "failed jffont load clears font view");

    corrupted = bytes;
    corrupted[40] = 0x01;
    corrupted[41] = 0x00;
    corrupted[42] = 0x00;
    corrupted[43] = 0x00;
    check(!resource.load_jffont(corrupted.data(), corrupted.size()), "jffont rejects short glyph row data");
}

} // namespace

int main() {
    try {
        fill_rect_rasterizes_pixels();
        rounded_stroke_keeps_corner_pixels_clear();
        source_over_alpha_composites();
        clipping_limits_rasterization();
        image_command_uses_injected_painter();
        image_command_falls_back_without_painter();
        compositor_renders_pipeline_non_empty();
        wrapped_text_layout_keeps_descent_padding();
        layout_uses_injected_text_measurement();
        dirty_render_only_updates_requested_clip();
        rasterizer_reports_text_fallback();
        compositor_degrades_oversized_offscreen_layers_without_crashing();
        compositor_rejects_oversized_framebuffer_before_allocation();
        frame_sink_receives_framebuffer_view_and_dirty_rects();
        bitmap_font_backend_measures_and_paints();
        bitmap_font_lookup_uses_sorted_codepoints();
        bitmap_font_scaling_bold_and_missing_glyphs_are_stable();
        bitmap_font_bold_metrics_include_extra_stroke();
        jffont_resource_loads_bitmap_font_view();
    } catch (const std::exception& error) {
        std::cerr << "software renderer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "software renderer tests passed\n";
    return 0;
}
