#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/style.h"

#include <iostream>

using namespace wearweb;

namespace {

void print_color(const Color& color) {
    std::cout << "rgba(" << static_cast<int>(color.r) << ", "
              << static_cast<int>(color.g) << ", "
              << static_cast<int>(color.b) << ", "
              << static_cast<int>(color.a) << ")";
}

} // namespace

int main() {
    const std::string html =
        "<body>"
        "  <section id='screen' class='panel'>"
        "    <h1>WearWeb</h1>"
        "    <p>A tiny HTML/CSS app runtime slice.</p>"
        "    <p style='color: blue'>Ready for framebuffer rendering.</p>"
        "  </section>"
        "</body>";

    const std::string css =
        "body { background: white; padding: 4; }"
        ".panel { background: #e5e7eb; padding: 8; margin: 2; }"
        "h1 { color: #111827; margin: 0; }"
        "p { color: #374151; margin: 4; font-size: 12; }";

    HtmlParser html_parser;
    CssParser css_parser;
    auto dom = html_parser.parse(html);
    auto stylesheet = css_parser.parse(css);

    StyleResolver style_resolver(std::move(stylesheet));
    LayoutEngine layout_engine(style_resolver);
    auto layout_tree = layout_engine.layout(*dom, 240);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList display_list = layer_tree_builder.flatten(*layer_tree);

    for (const DisplayCommand& command : display_list) {
        if (command.type == DisplayCommandType::FillRect) {
            std::cout << "fill_rect ";
        } else if (command.type == DisplayCommandType::LinearGradient) {
            std::cout << "gradient  ";
        } else if (command.type == DisplayCommandType::StrokeRect) {
            std::cout << "stroke    ";
        } else {
            std::cout << "text      ";
        }
        std::cout << "x=" << command.rect.x
                  << " y=" << command.rect.y
                  << " w=" << command.rect.width
                  << " h=" << command.rect.height
                  << " color=";
        print_color(command.color);
        if (!command.text.empty()) {
            std::cout << " value=\"" << command.text << "\"";
        }
        std::cout << '\n';
    }

    return 0;
}
