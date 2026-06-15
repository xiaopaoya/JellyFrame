#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string bdf_path;
    std::string chars_path;
    std::string output_path;
    std::string symbol = "jellyframe_embedded_font";
    bool allow_missing = false;
};

struct Glyph {
    std::uint32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int advance = 0;
    int bytes_per_row = 0;
    std::vector<std::uint8_t> rows;
};

struct BdfFont {
    int line_height = 0;
    int fallback_advance = 0;
    std::map<std::uint32_t, Glyph> glyphs;
};

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::string sanitize_symbol(std::string value) {
    if (value.empty()) {
        return "jellyframe_embedded_font";
    }
    for (char& ch : value) {
        const unsigned char raw = static_cast<unsigned char>(ch);
        if (!std::isalnum(raw) && ch != '_') {
            ch = '_';
        }
    }
    if (std::isdigit(static_cast<unsigned char>(value.front()))) {
        value.insert(value.begin(), '_');
    }
    return value;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        const auto require_value = [&](const char* name) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string(name) + " requires a value");
            }
            return argv[++index];
        };
        if (arg == "--bdf") {
            options.bdf_path = require_value("--bdf");
        } else if (arg == "--chars") {
            options.chars_path = require_value("--chars");
        } else if (arg == "--output") {
            options.output_path = require_value("--output");
        } else if (arg == "--name") {
            options.symbol = sanitize_symbol(require_value("--name"));
        } else if (arg == "--allow-missing") {
            options.allow_missing = true;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    if (options.bdf_path.empty() || options.chars_path.empty() || options.output_path.empty()) {
        throw std::runtime_error("usage: jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt "
                                 "--output font_pack.h [--name symbol] [--allow-missing]");
    }
    return options;
}

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool starts_with(const std::string& value, const char* prefix) {
    const std::string needle(prefix);
    return value.size() >= needle.size() && value.compare(0, needle.size(), needle) == 0;
}

std::uint32_t consume_utf8_codepoint(const std::string& text, std::size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U && index + 1 < text.size()) {
        width = 2;
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
    } else if ((lead & 0xf0U) == 0xe0U && index + 2 < text.size()) {
        width = 3;
        codepoint = ((lead & 0x0fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
    } else if ((lead & 0xf8U) == 0xf0U && index + 3 < text.size()) {
        width = 4;
        codepoint = ((lead & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
    }
    index += std::min(width, text.size() - index);
    return codepoint;
}

std::set<std::uint32_t> load_chars(const std::string& path) {
    std::set<std::uint32_t> chars;
    const std::string source = read_file(path);
    for (std::size_t index = 0; index < source.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(source, index);
        if (codepoint >= 0x20U && codepoint != 0x7fU) {
            chars.insert(codepoint);
        }
    }
    return chars;
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return 0;
}

std::vector<std::uint8_t> parse_hex_row(const std::string& text, int bytes_per_row) {
    std::vector<std::uint8_t> row(static_cast<std::size_t>(bytes_per_row), 0);
    for (int byte = 0; byte < bytes_per_row; ++byte) {
        const std::size_t offset = static_cast<std::size_t>(byte * 2);
        if (offset + 1 >= text.size()) {
            break;
        }
        row[static_cast<std::size_t>(byte)] =
            static_cast<std::uint8_t>((hex_digit(text[offset]) << 4) | hex_digit(text[offset + 1]));
    }
    return row;
}

BdfFont parse_bdf(const std::string& path) {
    BdfFont font;
    std::vector<std::string> lines = split_lines(read_file(path));
    Glyph current;
    bool in_glyph = false;
    bool in_bitmap = false;
    int bitmap_rows = 0;

    for (const std::string& line : lines) {
        if (starts_with(line, "FONTBOUNDINGBOX ")) {
            std::istringstream input(line.substr(16));
            int width = 0;
            input >> width >> font.line_height;
            font.fallback_advance = width;
            continue;
        }
        if (starts_with(line, "STARTCHAR ")) {
            current = Glyph{};
            in_glyph = true;
            in_bitmap = false;
            bitmap_rows = 0;
            continue;
        }
        if (!in_glyph) {
            continue;
        }
        if (starts_with(line, "ENCODING ")) {
            current.codepoint = static_cast<std::uint32_t>(std::max(0, std::stoi(line.substr(9))));
        } else if (starts_with(line, "DWIDTH ")) {
            std::istringstream input(line.substr(7));
            input >> current.advance;
        } else if (starts_with(line, "BBX ")) {
            std::istringstream input(line.substr(4));
            int x_offset = 0;
            int y_offset = 0;
            input >> current.width >> current.height >> x_offset >> y_offset;
            current.bytes_per_row = std::max(1, (current.width + 7) / 8);
        } else if (line == "BITMAP") {
            in_bitmap = true;
            current.rows.clear();
            current.rows.reserve(static_cast<std::size_t>(current.height * current.bytes_per_row));
        } else if (line == "ENDCHAR") {
            if (current.codepoint > 0 && current.width > 0 && current.height > 0 && !current.rows.empty()) {
                if (current.advance <= 0) {
                    current.advance = current.width;
                }
                if (font.line_height <= 0) {
                    font.line_height = current.height;
                }
                if (font.fallback_advance <= 0) {
                    font.fallback_advance = current.advance;
                }
                font.glyphs[current.codepoint] = current;
            }
            in_glyph = false;
            in_bitmap = false;
        } else if (in_bitmap && bitmap_rows < current.height) {
            std::vector<std::uint8_t> row = parse_hex_row(line, current.bytes_per_row);
            current.rows.insert(current.rows.end(), row.begin(), row.end());
            ++bitmap_rows;
        }
    }
    return font;
}

std::string codepoint_name(std::uint32_t codepoint) {
    std::ostringstream output;
    output << "u" << std::uppercase << std::hex << codepoint;
    return output.str();
}

void write_font_pack(const Options& options,
                     const BdfFont& font,
                     const std::set<std::uint32_t>& requested,
                     const std::vector<std::uint32_t>& selected) {
    std::ofstream output(options.output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + options.output_path);
    }

    output << "#pragma once\n\n";
    output << "#include \"core/bitmap_font.h\"\n\n";
    output << "#include <cstdint>\n\n";
    output << "namespace jellyframe_generated {\n\n";
    for (std::uint32_t codepoint : selected) {
        const Glyph& glyph = font.glyphs.at(codepoint);
        output << "static constexpr std::uint8_t " << options.symbol << "_rows_"
               << codepoint_name(codepoint) << "[] = {";
        for (std::size_t index = 0; index < glyph.rows.size(); ++index) {
            if (index % 12 == 0) {
                output << "\n    ";
            }
            output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                   << static_cast<int>(glyph.rows[index]) << std::dec << ", ";
        }
        output << "\n};\n\n";
    }

    output << "static constexpr jellyframe::BitmapFontGlyph " << options.symbol << "_glyphs[] = {\n";
    for (std::uint32_t codepoint : selected) {
        const Glyph& glyph = font.glyphs.at(codepoint);
        output << "    jellyframe::BitmapFontGlyph{0x" << std::hex << std::uppercase << codepoint << std::dec
               << "U, " << glyph.width << ", " << glyph.height << ", " << glyph.advance << ", "
               << glyph.bytes_per_row << ", " << options.symbol << "_rows_" << codepoint_name(codepoint)
               << "},\n";
    }
    output << "};\n\n";
    output << "static constexpr jellyframe::BitmapFont " << options.symbol << "{"
           << options.symbol << "_glyphs, " << selected.size() << ", "
           << std::max(1, font.line_height) << ", " << std::max(1, font.fallback_advance) << "};\n\n";
    output << "} // namespace jellyframe_generated\n";

    std::cerr << "requested=" << requested.size() << " emitted=" << selected.size() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::set<std::uint32_t> requested = load_chars(options.chars_path);
        const BdfFont font = parse_bdf(options.bdf_path);
        std::vector<std::uint32_t> selected;
        std::vector<std::uint32_t> missing;
        for (std::uint32_t codepoint : requested) {
            if (font.glyphs.find(codepoint) != font.glyphs.end()) {
                selected.push_back(codepoint);
            } else {
                missing.push_back(codepoint);
            }
        }
        if (!missing.empty() && !options.allow_missing) {
            std::cerr << "missing glyphs:";
            for (std::uint32_t codepoint : missing) {
                std::cerr << " U+" << std::uppercase << std::hex << codepoint << std::dec;
            }
            std::cerr << '\n';
            return 1;
        }
        write_font_pack(options, font, requested, selected);
        if (!missing.empty()) {
            std::cerr << "missing=" << missing.size() << " (allowed)\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "font pack generation failed: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
