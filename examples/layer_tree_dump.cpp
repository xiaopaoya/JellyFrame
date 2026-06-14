#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include "example_css_io.h"

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

const char* layer_type_name(LayerType type) {
    switch (type) {
    case LayerType::Root:
        return "RootLayer";
    case LayerType::Paint:
        return "PaintLayer";
    case LayerType::Clip:
        return "ClipLayer";
    case LayerType::Stacking:
        return "StackingLayer";
    case LayerType::Composited:
        return "CompositedLayer";
    }
    return "Layer";
}

void print_reasons(LayerReasons reasons) {
    bool first = true;
    const auto add = [&](LayerReason reason, const char* name) {
        if ((reasons & reason) == 0U) {
            return;
        }
        if (!first) {
            std::cout << ',';
        }
        std::cout << name;
        first = false;
    };
    add(LayerReasonRoot, "root");
    add(LayerReasonOverflowClip, "overflow");
    add(LayerReasonOpacity, "opacity");
    add(LayerReasonTransform, "transform");
    add(LayerReasonPositioned, "position");
    add(LayerReasonZIndex, "z-index");
    add(LayerReasonShadow, "shadow");
    add(LayerReasonRoundedClip, "rounded-clip");
    if (first) {
        std::cout << "none";
    }
}

void print_node_label(const LayerNode& layer) {
    if (layer.box == nullptr || layer.box->node == nullptr) {
        std::cout << "(anonymous)";
        return;
    }
    const Node& node = *layer.box->node;
    if (node.type == NodeType::Text) {
        std::cout << "#text";
        return;
    }
    std::cout << '<' << node.tag_name;
    if (!node.attribute("id").empty()) {
        std::cout << '#' << node.attribute("id");
    }
    if (!node.attribute("class").empty()) {
        std::cout << '.' << node.attribute("class");
    }
    std::cout << '>';
}

void dump_layer_tree(const LayerNode& layer, std::size_t depth = 0) {
    for (std::size_t i = 0; i < depth; ++i) {
        std::cout << "  ";
    }
    std::cout << layer_type_name(layer.type) << ' ';
    print_node_label(layer);
    std::cout << " bounds=" << layer.bounds.x << ',' << layer.bounds.y << ' '
              << layer.bounds.width << 'x' << layer.bounds.height
              << " commands=" << layer.display_list.size()
              << " z=" << layer.z_index
              << " opacity=" << layer.opacity
              << " clip=" << (layer.has_clip ? "yes" : "no")
              << " reasons=";
    print_reasons(layer.reasons);
    std::cout << '\n';

    for (const auto& child : layer.children) {
        dump_layer_tree(*child, depth + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: wearweb_layer_tree_dump page.html style.css [viewport_width]\n";
        return 1;
    }

    try {
        const int viewport_width = argc >= 4 ? std::stoi(argv[3]) : 360;
        const std::string html = read_file_limited(argv[1]);

        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html);
        StyleResolver resolver(css_parser.parse(
            wearweb_example::read_author_css_for_document(argv[2], *document, kMaxInputBytes)));
        RenderTreeBuilder render_tree_builder(resolver);
        auto render_tree = render_tree_builder.build(*document);
        LayoutEngine layout_engine(resolver);
        auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
        LayerTreeBuilder layer_tree_builder;
        auto layer_tree = layer_tree_builder.build(*layout_tree);
        DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

        std::cout << "Layer tree summary\n";
        std::cout << "  layers=" << count_layers(*layer_tree) << '\n';
        std::cout << "  flattened_commands=" << flattened.size() << '\n';
        std::cout << "  viewport_width=" << viewport_width << '\n';
        dump_layer_tree(*layer_tree);
    } catch (const std::exception& error) {
        std::cerr << "layer tree dump failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
