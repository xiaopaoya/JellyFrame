#include "render_core/css_parser.h"
#include "render_core/document_style.h"
#include "render_core/dom.h"
#include "render_core/html_parser.h"
#include "render_core/style.h"

#include "example_css_io.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

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

void print_color(const char* name, const Color& color) {
    std::cout << " " << name << "=rgba(" << static_cast<int>(color.r) << ","
              << static_cast<int>(color.g) << "," << static_cast<int>(color.b)
              << "," << static_cast<int>(color.a) << ")";
}

bool is_interesting_node(const Node& node) {
    if (node.type != NodeType::Element) {
        return false;
    }
    return node.tag_name == "form" || node.tag_name == "input" || node.tag_name == "button" ||
        node.tag_name == "dialog" || node.tag_name == "img" || node.tag_name == "article" ||
        node.tag_name == "main" || node.tag_name == "app-root" || node.tag_name == "nav";
}

void print_node_label(const Node& node) {
    std::cout << '<' << node.tag_name;
    if (!node.attribute("id").empty()) {
        std::cout << "#" << node.attribute("id");
    }
    if (!node.attribute("class").empty()) {
        std::cout << "." << node.attribute("class");
    }
    std::cout << '>';
}

void dump_styles(const Node& node, const StyleResolver& resolver, std::size_t depth = 0) {
    if (is_interesting_node(node)) {
        const Style style = resolver.resolve(node);
        for (std::size_t i = 0; i < depth; ++i) {
            std::cout << "  ";
        }
        print_node_label(node);
        std::cout << " display=" << display_name(style.display)
                  << " width=" << style.width
                  << " min-width=" << style.min_width
                  << " height=" << style.height
                  << " padding=" << style.padding.top
                  << " border=" << style.border_width.top
                  << " radius=" << style.border_radius;
        print_color("color", style.color);
        print_color("background", style.background_color);
        if (!style.box_shadow.empty()) {
            std::cout << " shadow=\"" << style.box_shadow << "\"";
        }
        std::cout << '\n';
    }

    for (const auto& child : node.children) {
        dump_styles(*child, resolver, depth + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_style_dump page.html style.css\n";
        return 0;
    }
    if (argc < 3) {
        std::cerr << "usage: jellyframe_style_dump page.html style.css\n";
        return 1;
    }

    try {
        const std::string html = read_file_limited(argv[1]);

        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(html);
        StyleResolver resolver(css_parser.parse(
            jellyframe_example::read_author_css_for_document(argv[2], *document, kMaxInputBytes)));
        dump_styles(*document, resolver);
    } catch (const std::exception& error) {
        std::cerr << "style dump failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
