#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/style.h"

#include "display_command_access.h"

#include <iostream>

using namespace jellyframe;

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
        "    <h1>JellyFrame</h1>"
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
        const auto type = native::display_command_type(command);
        if (type == DisplayCommandType::FillRect) {
            std::cout << "fill_rect ";
        } else if (type == DisplayCommandType::LinearGradient) {
            std::cout << "gradient  ";
        } else if (type == DisplayCommandType::StrokeRect) {
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
        if (type == DisplayCommandType::Text && !command.text.empty()) {
            std::cout << " value=\"" << command.text << "\"";
        } else if (native::is_prepared_text_command(type)) {
            std::cout << " prepared_packet_bytes=" << command.text.size();
        }
        std::cout << '\n';
    }

    return 0;
}
