#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"

#include <iostream>
#include <stdexcept>

using namespace wearweb;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

} // namespace

int main() {
    try {
        fill_rect_rasterizes_pixels();
        source_over_alpha_composites();
        clipping_limits_rasterization();
        compositor_renders_pipeline_non_empty();
        wrapped_text_layout_keeps_descent_padding();
    } catch (const std::exception& error) {
        std::cerr << "software renderer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "software renderer tests passed\n";
    return 0;
}
