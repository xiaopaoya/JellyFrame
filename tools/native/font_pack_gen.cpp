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
    std::string output_binary_path;
    std::string symbol = "jellyframe_embedded_font";
    bool allow_missing = false;
    int coverage_bits = 1;
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
        } else if (arg == "--output-binary") {
            options.output_binary_path = require_value("--output-binary");
        } else if (arg == "--name") {
            options.symbol = sanitize_symbol(require_value("--name"));
        } else if (arg == "--coverage-bits") {
            options.coverage_bits = std::stoi(require_value("--coverage-bits"));
        } else if (arg == "--allow-missing") {
            options.allow_missing = true;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    if (options.bdf_path.empty() || options.chars_path.empty() ||
        (options.output_path.empty() && options.output_binary_path.empty())) {
        throw std::runtime_error("usage: jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt "
                                 "[--output font_pack.h] [--output-binary font.jffont] "
                                 "[--name symbol] [--coverage-bits 1|2|4] [--allow-missing]");
    }
    if (!(options.coverage_bits == 1 || options.coverage_bits == 2 || options.coverage_bits == 4)) {
        throw std::runtime_error("--coverage-bits must be 1, 2 or 4");
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
        if (codepoint >= 0x20U && codepoint != 0x7fU && codepoint != 0xfeffU) {
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

std::uint8_t checked_u8(int value, const char* field) {
    if (value < 0 || value > 255) {
        throw std::runtime_error(std::string("font ") + field + " is outside uint8 range");
    }
    return static_cast<std::uint8_t>(value);
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void validate_glyph_for_pack(const Glyph& glyph) {
    checked_u8(glyph.width, "width");
    checked_u8(glyph.height, "height");
    checked_u8(glyph.advance, "advance");
    checked_u8(glyph.bytes_per_row, "bytes_per_row");
}

bool glyph_bit(const Glyph& glyph, int row, int col) {
    if (row < 0 || col < 0 || row >= glyph.height || col >= glyph.width || glyph.rows.empty()) {
        return false;
    }
    const std::uint8_t byte = glyph.rows[static_cast<std::size_t>(row * glyph.bytes_per_row + col / 8)];
    return (byte & (1U << (7 - (col % 8)))) != 0U;
}

int coverage_level_for_pixel(const Glyph& glyph, int row, int col, int max_level) {
    if (glyph_bit(glyph, row, col)) {
        return max_level;
    }
    int neighbors = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (glyph_bit(glyph, row + dy, col + dx)) {
                ++neighbors;
            }
        }
    }
    if (neighbors == 0) {
        return 0;
    }
    if (max_level == 3) {
        return neighbors >= 3 ? 2 : 1;
    }
    return std::min(max_level - 1, std::max(1, (neighbors * max_level + 4) / 8));
}

std::vector<std::uint8_t> glyph_rows_for_output(const Glyph& glyph, int coverage_bits, int* bytes_per_row_out) {
    if (coverage_bits == 1) {
        if (bytes_per_row_out != nullptr) {
            *bytes_per_row_out = glyph.bytes_per_row;
        }
        return glyph.rows;
    }
    const int bytes_per_row = std::max(1, (glyph.width * coverage_bits + 7) / 8);
    const int max_level = (1 << coverage_bits) - 1;
    std::vector<std::uint8_t> rows(static_cast<std::size_t>(glyph.height * bytes_per_row), 0);
    for (int row = 0; row < glyph.height; ++row) {
        for (int col = 0; col < glyph.width; ++col) {
            const int level = coverage_level_for_pixel(glyph, row, col, max_level);
            if (level == 0) {
                continue;
            }
            const int bit_index = col * coverage_bits;
            const int byte_index = row * bytes_per_row + bit_index / 8;
            const int shift = 8 - coverage_bits - (bit_index % 8);
            rows[static_cast<std::size_t>(byte_index)] |=
                static_cast<std::uint8_t>(level << shift);
        }
    }
    if (bytes_per_row_out != nullptr) {
        *bytes_per_row_out = bytes_per_row;
    }
    return rows;
}

std::size_t selected_row_bytes(const BdfFont& font,
                               const std::vector<std::uint32_t>& selected,
                               int coverage_bits) {
    std::size_t row_bytes = 0;
    for (std::uint32_t codepoint : selected) {
        int bytes_per_row = 0;
        (void)glyph_rows_for_output(font.glyphs.at(codepoint), coverage_bits, &bytes_per_row);
        row_bytes += static_cast<std::size_t>(font.glyphs.at(codepoint).height * bytes_per_row);
    }
    return row_bytes;
}

void print_summary(const BdfFont& font,
                   const std::set<std::uint32_t>& requested,
                   const std::vector<std::uint32_t>& selected,
                   int coverage_bits) {
    constexpr std::size_t glyph_metadata_bytes = 16;
    const std::size_t row_bytes = selected_row_bytes(font, selected, coverage_bits);
    const std::size_t glyph_table_bytes = selected.size() * glyph_metadata_bytes;
    std::cerr << "requested=" << requested.size()
              << " emitted=" << selected.size()
              << " coverage_bits=" << coverage_bits
              << " rows_bytes=" << row_bytes
              << " glyph_table_estimated_bytes=" << glyph_table_bytes
              << " total_estimated_bytes=" << (row_bytes + glyph_table_bytes)
              << '\n';
}

void write_font_header(const Options& options,
                       const BdfFont& font,
                       const std::vector<std::uint32_t>& selected) {
    std::ofstream output(options.output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + options.output_path);
    }

    output << "#pragma once\n\n";
    output << "#include \"render_core/bitmap_font.h\"\n\n";
    output << "#include <cstdint>\n\n";
    output << "namespace jellyframe_generated {\n\n";
    for (std::uint32_t codepoint : selected) {
        const Glyph& glyph = font.glyphs.at(codepoint);
        validate_glyph_for_pack(glyph);
        int output_bytes_per_row = 0;
        const std::vector<std::uint8_t> rows = glyph_rows_for_output(glyph, options.coverage_bits, &output_bytes_per_row);
        output << "static constexpr std::uint8_t " << options.symbol << "_rows_"
               << codepoint_name(codepoint) << "[] = {";
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (index % 12 == 0) {
                output << "\n    ";
            }
            output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                   << static_cast<int>(rows[index]) << std::dec << ", ";
        }
        output << "\n};\n\n";
    }

    output << "static constexpr jellyframe::BitmapFontGlyph " << options.symbol << "_glyphs[] = {\n";
    for (std::uint32_t codepoint : selected) {
        const Glyph& glyph = font.glyphs.at(codepoint);
        int output_bytes_per_row = 0;
        (void)glyph_rows_for_output(glyph, options.coverage_bits, &output_bytes_per_row);
        output << "    jellyframe::BitmapFontGlyph{0x" << std::hex << std::uppercase << codepoint << std::dec
               << "U, " << glyph.width << ", " << glyph.height << ", " << glyph.advance << ", "
               << output_bytes_per_row << ", " << options.symbol << "_rows_" << codepoint_name(codepoint)
               << ", " << options.coverage_bits << "},\n";
    }
    output << "};\n\n";
    output << "static constexpr jellyframe::BitmapFont " << options.symbol << "{"
           << options.symbol << "_glyphs, " << selected.size() << ", "
           << std::max(1, font.line_height) << ", " << std::max(1, font.fallback_advance) << "};\n\n";
    output << "} // namespace jellyframe_generated\n";
}

void write_font_binary(const Options& options,
                       const BdfFont& font,
                       const std::vector<std::uint32_t>& selected) {
    constexpr std::uint16_t header_size = 32;
    const std::uint16_t version = options.coverage_bits == 1 ? 0 : 1;
    constexpr std::uint32_t glyph_entry_size = 16;
    const std::uint32_t glyph_count = static_cast<std::uint32_t>(selected.size());
    const std::uint32_t glyph_table_offset = header_size;
    const std::uint32_t row_data_offset = glyph_table_offset + glyph_count * glyph_entry_size;
    const std::uint32_t row_data_size = static_cast<std::uint32_t>(
        selected_row_bytes(font, selected, options.coverage_bits));

    std::vector<std::uint8_t> data;
    data.reserve(static_cast<std::size_t>(row_data_offset) + row_data_size);
    const char magic[8] = {'J', 'F', 'F', 'O', 'N', 'T', '0', '\0'};
    data.insert(data.end(), magic, magic + 8);
    append_u16(data, header_size);
    append_u16(data, version);
    append_u32(data, glyph_count);
    data.push_back(checked_u8(std::max(1, font.line_height), "line_height"));
    data.push_back(checked_u8(std::max(1, font.fallback_advance), "fallback_advance"));
    append_u16(data, static_cast<std::uint16_t>(options.coverage_bits == 1 ? 0 : options.coverage_bits));
    append_u32(data, glyph_table_offset);
    append_u32(data, row_data_offset);
    append_u32(data, row_data_size);

    std::uint32_t row_offset = 0;
    for (std::uint32_t codepoint : selected) {
        const Glyph& glyph = font.glyphs.at(codepoint);
        validate_glyph_for_pack(glyph);
        int output_bytes_per_row = 0;
        const std::vector<std::uint8_t> rows = glyph_rows_for_output(glyph, options.coverage_bits, &output_bytes_per_row);
        append_u32(data, codepoint);
        append_u32(data, row_offset);
        append_u32(data, static_cast<std::uint32_t>(rows.size()));
        data.push_back(static_cast<std::uint8_t>(glyph.width));
        data.push_back(static_cast<std::uint8_t>(glyph.height));
        data.push_back(static_cast<std::uint8_t>(glyph.advance));
        data.push_back(static_cast<std::uint8_t>(output_bytes_per_row));
        row_offset += static_cast<std::uint32_t>(rows.size());
    }
    for (std::uint32_t codepoint : selected) {
        int output_bytes_per_row = 0;
        const std::vector<std::uint8_t> rows =
            glyph_rows_for_output(font.glyphs.at(codepoint), options.coverage_bits, &output_bytes_per_row);
        data.insert(data.end(), rows.begin(), rows.end());
    }

    std::ofstream output(options.output_binary_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + options.output_binary_path);
    }
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_font_pack_gen --bdf font.bdf --chars used_chars.txt "
                     "[--output font_pack.h] [--output-binary font.jffont] "
                     "[--name symbol] [--coverage-bits 1|2|4] [--allow-missing]\n";
        return 0;
    }
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
        if (!options.output_path.empty()) {
            write_font_header(options, font, selected);
        }
        if (!options.output_binary_path.empty()) {
            write_font_binary(options, font, selected);
        }
        print_summary(font, requested, selected, options.coverage_bits);
        if (!missing.empty()) {
            std::cerr << "missing=" << missing.size() << " (allowed)\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "font pack generation failed: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
