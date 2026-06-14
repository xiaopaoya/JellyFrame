#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/html_parser.h"
#include "core/render_tree.h"

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

const char* object_type_name(RenderObjectType type) {
    switch (type) {
    case RenderObjectType::View:
        return "RenderView";
    case RenderObjectType::Block:
        return "RenderBlock";
    case RenderObjectType::Inline:
        return "RenderInline";
    case RenderObjectType::Text:
        return "RenderText";
    }
    return "RenderObject";
}

const char* display_name(Display display) {
    switch (display) {
    case Display::Block:
        return "block";
    case Display::Inline:
        return "inline";
    case Display::InlineBlock:
        return "inline-block";
    case Display::Flex:
        return "flex";
    case Display::Grid:
        return "grid";
    case Display::None:
        return "none";
    }
    return "unknown";
}

std::string clipped(std::string value, std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    value.resize(max_bytes);
    value += "...";
    return value;
}

void print_node_label(const RenderObject& object) {
    if (object.node == nullptr) {
        std::cout << "(null)";
        return;
    }
    if (object.node->type == NodeType::Text) {
        std::cout << "#text \"" << clipped(object.node->text, 48) << "\"";
        return;
    }
    std::cout << '<' << object.node->tag_name;
    if (!object.node->attribute("id").empty()) {
        std::cout << "#" << object.node->attribute("id");
    }
    if (!object.node->attribute("class").empty()) {
        std::cout << "." << object.node->attribute("class");
    }
    std::cout << '>';
}

void dump_render_tree(const RenderObject& object, std::size_t depth = 0) {
    for (std::size_t i = 0; i < depth; ++i) {
        std::cout << "  ";
    }
    std::cout << object_type_name(object.type) << " ";
    print_node_label(object);
    std::cout << " display=" << display_name(object.style.display)
              << " width=" << object.style.width
              << " min-width=" << object.style.min_width
              << " padding=" << object.style.padding.top
              << " border=" << object.style.border_width.top << '\n';

    for (const auto& child : object.children) {
        dump_render_tree(*child, depth + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: wearweb_render_tree_dump page.html style.css\n";
        return 1;
    }

    try {
        const std::string html = read_file_limited(argv[1]);
        const std::string css = read_file_limited(argv[2]);
        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html);
        StyleResolver resolver(css_parser.parse(combine_author_css(css, *document)));
        RenderTreeBuilder builder(resolver);
        auto render_tree = builder.build(*document);
        dump_render_tree(*render_tree);
    } catch (const std::exception& error) {
        std::cerr << "render tree dump failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
