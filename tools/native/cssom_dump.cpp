#include "render_core/css_parser.h"
#include "render_core/style.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

constexpr std::size_t kMaxInputBytes = 512 * 1024;

std::string sample_css() {
    return "/* JellyFrame CSSOM demo */"
           "@layer base {"
           "  body { color: #111; background: #fff; }"
           "  #search.box { color: #333; color: oklch(50% 0.2 30); padding: 8px; }"
           "}"
           "@supports (backdrop-filter: blur(8px)) {"
           "  #search { background: color-mix(in srgb, white, transparent); }"
           "}"
           "@media screen { .box { background: #e5e7eb; } }";
}

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

std::string clipped(std::string value, std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    value.resize(max_bytes);
    value += "...";
    return value;
}

void print_rule(const CssRule& rule, std::size_t index) {
    std::cout << "[" << index << "] style " << rule.selector
              << " specificity=(" << rule.specificity.ids << ","
              << rule.specificity.classes << "," << rule.specificity.elements << ")"
              << " order=" << rule.source_order << '\n';
    for (const CssDeclaration& declaration : rule.declarations) {
        std::cout << "  " << declaration.property << ": "
                  << clipped(declaration.value, 96);
        if (declaration.important) {
            std::cout << " !important";
        }
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_cssom_dump [style.css]\n";
        return 0;
    }
    try {
        const std::string input = argc > 1 ? read_file_limited(argv[1]) : sample_css();
        CssParser parser;
        const Stylesheet stylesheet = parser.parse(input);

        std::cout << "CSSOM rules=" << stylesheet.size() << '\n';
        std::size_t index = 0;
        for (const CssRule& rule : stylesheet) {
            print_rule(rule, index++);
            if (index >= 120) {
                std::cout << "... clipped rule output ...\n";
                break;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "cssom dump failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
