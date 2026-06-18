#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/layout.h"
#include "core/pipeline_statistics.h"
#include "core/render_tree.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pipeline_statistics_reports_core_sizes() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>Hello</p><p>World</p></main></body>");
    StyleResolver resolver(css_parser.parse("main { padding: 4px; } p { margin: 0; }"));
    RenderTreeBuilder render_builder(resolver);
    MonotonicArena render_arena(256);
    auto render_tree = render_builder.build(*document, render_arena);
    LayoutEngine layout_engine(resolver);
    MonotonicArena layout_arena(256);
    auto layout_tree = layout_engine.layout(*render_tree, 240, layout_arena);
    LayerTreeBuilder layer_builder;
    MonotonicArena layer_arena(256);
    auto layer_tree = layer_builder.build(*layout_tree, layer_arena);
    DisplayList display_list = layer_builder.flatten(*layer_tree);
    FrameBuffer framebuffer = SoftwareCompositor().render(*layer_tree, 240, 160, Color{255, 255, 255, 255});

    const PipelineStatistics statistics = collect_pipeline_statistics(PipelineStatisticsInput{
        document.get(),
        render_tree.get(),
        layout_tree.get(),
        layer_tree.get(),
        &display_list,
        &framebuffer,
        &render_arena,
        &layout_arena,
        &layer_arena,
        1234,
    });

    check(statistics.dom.node_count > 0, "dom statistics collected");
    check(statistics.render_objects > 0, "render object count collected");
    check(statistics.layout_boxes > 0, "layout box count collected");
    check(statistics.layers > 0, "layer count collected");
    check(statistics.display_commands >= statistics.flattened_display_commands,
          "layer display commands are at least flattened display commands");
    check(statistics.framebuffer_bytes == framebuffer_byte_size(framebuffer),
          "framebuffer byte size matches framebuffer view");
    check(statistics.resource_bytes == 1234, "resource bytes are propagated");
    check(statistics.render_arena.used_bytes == render_arena.used_bytes(),
          "render arena bytes are reported");
    check(statistics.layout_arena.used_bytes == layout_arena.used_bytes(),
          "layout arena bytes are reported");
    check(statistics.layer_arena.used_bytes == layer_arena.used_bytes(),
          "layer arena bytes are reported");
    check(statistics.estimated_heap_bytes >= statistics.resource_bytes,
          "estimated footprint includes resources");
}

} // namespace

int main() {
    try {
        pipeline_statistics_reports_core_sizes();
    } catch (const std::exception& error) {
        std::cerr << "pipeline statistics test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "pipeline statistics tests passed\n";
    return 0;
}
