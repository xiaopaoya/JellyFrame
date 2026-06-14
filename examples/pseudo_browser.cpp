#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"
#include "core/style.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace wearweb;

namespace {

constexpr std::size_t kMaxInputBytes = 512 * 1024;

std::string read_file_limited(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file");
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

std::size_t count_dom_nodes(const Node& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += count_dom_nodes(*child);
    }
    return count;
}

std::size_t count_render_objects(const RenderObject& object) {
    std::size_t count = 1;
    for (const auto& child : object.children) {
        count += count_render_objects(*child);
    }
    return count;
}

std::size_t count_layout_boxes(const LayoutBox& box) {
    std::size_t count = 1;
    for (const auto& child : box.children) {
        count += count_layout_boxes(*child);
    }
    return count;
}

int parse_int_arg(const char* value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: wearweb_pseudo_browser page.html style.css output.ppm [viewport_width] [viewport_height]\n";
        return 1;
    }

    try {
        const int viewport_width = argc >= 5 ? parse_int_arg(argv[4], 360) : 360;
        const int viewport_height = argc >= 6 ? parse_int_arg(argv[5], 240) : 240;
        const std::string html = read_file_limited(argv[1]);
        const std::string css = read_file_limited(argv[2]);

        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html);
        Stylesheet stylesheet = css_parser.parse(combine_author_css(css, *document));
        StyleResolver resolver(std::move(stylesheet));

        RenderTreeBuilder render_tree_builder(resolver);
        auto render_tree = render_tree_builder.build(*document);
        LayoutEngine layout_engine(resolver);
        auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
        LayerTreeBuilder layer_tree_builder;
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);

        SoftwareCompositor compositor;
        FrameBuffer frame_buffer = compositor.render(*layer_tree, viewport_width, viewport_height, Color{255, 255, 255, 255});
        write_image(frame_buffer, argv[3]);

        std::cout << "WearWeb pseudo browser\n";
        std::cout << "  output=" << argv[3] << '\n';
        std::cout << "  viewport=" << viewport_width << "x" << viewport_height << '\n';
        std::cout << "  dom_nodes=" << count_dom_nodes(*document) << '\n';
        std::cout << "  render_objects=" << count_render_objects(*render_tree) << '\n';
        std::cout << "  layout_boxes=" << count_layout_boxes(*layout_tree) << '\n';
        std::cout << "  layers=" << count_layers(*layer_tree) << '\n';
        std::cout << "  display_commands=" << display_list.size() << '\n';
        std::cout << "  non_background_pixels="
                  << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
    } catch (const std::exception& error) {
        std::cerr << "pseudo browser failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
