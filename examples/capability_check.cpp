#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Finding {
    std::string file;
    std::string level;
    std::string feature;
    std::string detail;
};

struct Options {
    std::vector<std::string> files;
    std::string font_coverage_path;
    std::string emit_used_chars_path;
    int budget_glyph_width = 0;
    int budget_glyph_height = 0;
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

bool append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

std::string utf8_for(std::uint32_t codepoint) {
    std::string output;
    append_utf8(output, codepoint);
    return output;
}

std::string hex_codepoint(std::uint32_t codepoint) {
    std::ostringstream output;
    output << "U+";
    output << std::uppercase << std::hex;
    if (codepoint <= 0xffffU) {
        output.width(4);
    }
    output.fill('0');
    output << codepoint;
    return output.str();
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

bool parse_numeric_entity(std::string_view source, std::size_t ampersand, std::uint32_t& output, std::size_t& end) {
    if (ampersand + 3 >= source.size() || source[ampersand] != '&' || source[ampersand + 1] != '#') {
        return false;
    }
    bool hex = false;
    std::size_t index = ampersand + 2;
    if (index < source.size() && (source[index] == 'x' || source[index] == 'X')) {
        hex = true;
        ++index;
    }
    std::uint32_t value = 0;
    bool any = false;
    for (; index < source.size() && source[index] != ';'; ++index) {
        const unsigned char ch = static_cast<unsigned char>(source[index]);
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (hex && ch >= 'a' && ch <= 'f') {
            digit = 10 + ch - 'a';
        } else if (hex && ch >= 'A' && ch <= 'F') {
            digit = 10 + ch - 'A';
        } else {
            return false;
        }
        value = value * (hex ? 16U : 10U) + static_cast<std::uint32_t>(digit);
        any = true;
    }
    if (!any || index >= source.size() || source[index] != ';') {
        return false;
    }
    output = value;
    end = index + 1;
    return true;
}

bool parse_named_entity(std::string_view source, std::size_t ampersand, std::uint32_t& output, std::size_t& end) {
    struct Entity {
        const char* name;
        std::uint32_t codepoint;
    };
    static constexpr Entity entities[] = {
        {"&times;", 0x00d7U},
        {"&divide;", 0x00f7U},
        {"&plusmn;", 0x00b1U},
        {"&minus;", 0x2212U},
        {"&nbsp;", 0x0020U},
    };
    for (const Entity& entity : entities) {
        const std::string_view name(entity.name);
        if (source.substr(ampersand, name.size()) == name) {
            output = entity.codepoint;
            end = ampersand + name.size();
            return true;
        }
    }
    return false;
}

void collect_used_codepoints(const std::string& source, std::set<std::uint32_t>& output) {
    for (std::size_t index = 0; index < source.size();) {
        std::uint32_t codepoint = 0;
        std::size_t entity_end = index;
        if (parse_numeric_entity(source, index, codepoint, entity_end) ||
            parse_named_entity(source, index, codepoint, entity_end)) {
            if (codepoint >= 0x20U && codepoint != 0x7fU && codepoint != 0xfeffU) {
                output.insert(codepoint);
            }
            index = entity_end;
            continue;
        }
        codepoint = consume_utf8_codepoint(source, index);
        if (codepoint >= 0x20U && codepoint != 0x7fU && codepoint != 0xfeffU) {
            output.insert(codepoint);
        }
    }
}

std::set<std::uint32_t> load_font_coverage(const std::string& path) {
    std::set<std::uint32_t> coverage;
    const std::string source = read_file(path);
    collect_used_codepoints(source, coverage);
    return coverage;
}

void write_used_chars(const std::string& path, const std::set<std::uint32_t>& used) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path);
    }
    for (std::uint32_t codepoint : used) {
        if (codepoint < 0x80U) {
            continue;
        }
        std::string encoded;
        append_utf8(encoded, codepoint);
        output << encoded << '\n';
    }
}

std::size_t count_non_ascii(const std::set<std::uint32_t>& codepoints) {
    std::size_t count = 0;
    for (std::uint32_t codepoint : codepoints) {
        if (codepoint >= 0x80U) {
            ++count;
        }
    }
    return count;
}

struct FontProfileStats {
    std::size_t ascii_printable = 0;
    std::size_t non_ascii = 0;
    std::size_t cjk_unified = 0;
    std::size_t cjk_symbols = 0;
    std::size_t latin_extended = 0;
    std::size_t greek = 0;
    std::size_t cyrillic = 0;
    std::size_t kana = 0;
    std::size_t hangul = 0;
    std::size_t symbols = 0;
    std::size_t other = 0;
};

bool in_range(std::uint32_t codepoint, std::uint32_t first, std::uint32_t last) {
    return codepoint >= first && codepoint <= last;
}

bool is_cjk_unified(std::uint32_t codepoint) {
    return in_range(codepoint, 0x4e00U, 0x9fffU) ||
        in_range(codepoint, 0x3400U, 0x4dbfU) ||
        in_range(codepoint, 0x20000U, 0x2a6dfU) ||
        in_range(codepoint, 0x2a700U, 0x2b73fU) ||
        in_range(codepoint, 0x2b740U, 0x2b81fU) ||
        in_range(codepoint, 0x2b820U, 0x2ceafU);
}

bool is_cjk_symbol(std::uint32_t codepoint) {
    return in_range(codepoint, 0x3000U, 0x303fU) ||
        in_range(codepoint, 0xfe10U, 0xfe6fU) ||
        in_range(codepoint, 0xff00U, 0xffefU);
}

bool is_common_symbol(std::uint32_t codepoint) {
    return in_range(codepoint, 0x00a0U, 0x00bfU) ||
        in_range(codepoint, 0x2000U, 0x206fU) ||
        in_range(codepoint, 0x20a0U, 0x20cfU) ||
        in_range(codepoint, 0x2100U, 0x214fU) ||
        in_range(codepoint, 0x2190U, 0x21ffU) ||
        in_range(codepoint, 0x2500U, 0x257fU) ||
        in_range(codepoint, 0x25a0U, 0x25ffU) ||
        in_range(codepoint, 0x2600U, 0x27bfU);
}

FontProfileStats summarize_font_profile(const std::set<std::uint32_t>& codepoints) {
    FontProfileStats stats;
    for (std::uint32_t codepoint : codepoints) {
        if (codepoint >= 0x20U && codepoint < 0x7fU) {
            ++stats.ascii_printable;
            continue;
        }
        if (codepoint < 0x80U) {
            continue;
        }
        ++stats.non_ascii;
        if (is_cjk_unified(codepoint)) {
            ++stats.cjk_unified;
        } else if (is_cjk_symbol(codepoint)) {
            ++stats.cjk_symbols;
        } else if (in_range(codepoint, 0x00c0U, 0x024fU)) {
            ++stats.latin_extended;
        } else if (in_range(codepoint, 0x0370U, 0x03ffU)) {
            ++stats.greek;
        } else if (in_range(codepoint, 0x0400U, 0x052fU)) {
            ++stats.cyrillic;
        } else if (in_range(codepoint, 0x3040U, 0x30ffU)) {
            ++stats.kana;
        } else if (in_range(codepoint, 0xac00U, 0xd7afU) || in_range(codepoint, 0x1100U, 0x11ffU)) {
            ++stats.hangul;
        } else if (is_common_symbol(codepoint)) {
            ++stats.symbols;
        } else {
            ++stats.other;
        }
    }
    return stats;
}

std::string recommended_font_profile(const FontProfileStats& stats) {
    if (stats.non_ascii == 0) {
        return "tiny";
    }
    const std::size_t global_scripts =
        stats.latin_extended + stats.greek + stats.cyrillic + stats.kana + stats.hangul + stats.other;
    if (stats.cjk_unified > 0 && global_scripts == 0) {
        return stats.non_ascii <= 128 ? "app-subset-cn" : "cn-standard";
    }
    if (stats.cjk_unified == 0 && global_scripts == 0) {
        return "tiny-plus-symbols";
    }
    return "global-product";
}

std::string font_profile_detail(const std::string& recommendation) {
    if (recommendation == "tiny") {
        return "ASCII, digits and basic UI symbols are enough for the scanned sources.";
    }
    if (recommendation == "tiny-plus-symbols") {
        return "Add the scanned symbols to the tiny bring-up font; no broad language pack is implied.";
    }
    if (recommendation == "app-subset-cn") {
        return "The page uses a small Chinese subset; generate an app-specific pack, or use CN standard for broader domestic firmware.";
    }
    if (recommendation == "cn-standard") {
        return "For domestic Chinese devices, ASCII + common symbols + GB2312 level-1 Chinese is the recommended reusable profile.";
    }
    return "Mixed or non-Chinese scripts were detected; choose per-market global subsets instead of a single oversized universal font.";
}

std::size_t estimated_bitmap_font_bytes(std::size_t glyph_count, int glyph_width, int glyph_height) {
    if (glyph_count == 0 || glyph_width <= 0 || glyph_height <= 0) {
        return 0;
    }
    const std::size_t bytes_per_row = static_cast<std::size_t>((glyph_width + 7) / 8);
    constexpr std::size_t glyph_metadata_bytes = 16;
    return glyph_count * (bytes_per_row * static_cast<std::size_t>(glyph_height) + glyph_metadata_bytes);
}

bool parse_font_budget(std::string_view value, int& width, int& height) {
    const std::size_t separator = value.find('x');
    if (separator == std::string_view::npos) {
        return false;
    }
    width = std::stoi(std::string(value.substr(0, separator)));
    height = std::stoi(std::string(value.substr(separator + 1)));
    return width > 0 && height > 0;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

void add(std::vector<Finding>& findings,
         const std::string& file,
         const char* level,
         const char* feature,
         const char* detail) {
    findings.push_back(Finding{file, level, feature, detail});
}

void add(std::vector<Finding>& findings,
         const std::string& file,
         const char* level,
         const char* feature,
         const std::string& detail) {
    findings.push_back(Finding{file, level, feature, detail});
}

void scan_html_css_js(const std::string& file, const std::string& source, std::vector<Finding>& findings) {
    const std::string lower = lowercase(source);

    if (contains(lower, "queryselector(") || contains(lower, "queryselectorall(")) {
        add(findings, file, "unsupported", "querySelector", "Use stable id lookups or event.target.closest(simple-selector).");
    }
    if (contains(lower, ".innerhtml") || contains(lower, "innerhtml")) {
        add(findings, file, "unsupported", "innerHTML", "Use createElement/createTextNode/appendChild.");
    }
    if (contains(lower, "fetch(") || contains(lower, "xmlhttprequest") || contains(lower, "localstorage")) {
        add(findings, file, "unsupported", "network/storage", "Provide data through host callbacks; no browser network/storage exists.");
    }
    if (contains(lower, "type=\"module\"") || contains(lower, "type='module'") || contains(lower, "import(")) {
        add(findings, file, "unsupported", "ES modules", "Only classic scripts are collected by scripting shells.");
    }
    if (contains(lower, "<canvas") || contains(lower, "<svg")) {
        add(findings, file, "unsupported", "canvas/svg", "Use HTML controls and CSS boxes; no canvas/SVG renderer exists.");
    }
    if (contains(lower, "@container")) {
        add(findings, file, "deferred", "@container", "Container queries are intentionally deferred.");
    }
    if (contains(lower, "@media")) {
        add(findings,
            file,
            "supported-subset",
            "@media",
            "Only screen/all plus min/max width/height conditions are evaluated; complex media queries are skipped.");
    }
    if (contains(lower, "@supports")) {
        add(findings,
            file,
            "supported-subset",
            "@supports",
            "Declaration conditions, not/and/or and parentheses are evaluated conservatively; selector() and unknown features are skipped.");
    }
    if (contains(lower, "var(")) {
        add(findings,
            file,
            "supported-subset",
            "CSS var()",
            "Direct var(--token) and var(--token, fallback) resolution is supported; full dependency graph semantics are not.");
    }
    if (contains(lower, "object-fit")) {
        add(findings, file, "deferred", "object-fit", "Image decode/object fitting is not implemented yet.");
    }
    if (contains(lower, ":is(") || contains(lower, ":where(")) {
        add(findings,
            file,
            "supported-subset",
            ":is/:where",
            ":is() matches with max-argument specificity; :where() matches with zero specificity.");
    }
    if (contains(lower, ":has(")) {
        add(findings, file, "deferred", ":has", "Relational selector matching is intentionally deferred.");
    }
    if (contains(lower, ":hover") || contains(lower, ":focus") || contains(lower, ":active") ||
        contains(lower, ":checked") || contains(lower, ":disabled") || contains(lower, ":focus-within")) {
        add(findings,
            file,
            "supported-subset",
            "dynamic pseudo-classes",
            ":hover/:active/:focus/:focus-within use input state; :checked/:disabled use form-control state.");
    }
    if (contains(lower, "filter:") || contains(lower, "backdrop-filter") || contains(lower, "mix-blend-mode")) {
        add(findings, file, "degraded", "filters/blending", "Only normal source-over and cheap shadows are supported.");
    }
    if (contains(lower, "display: grid") && contains(lower, "repeat(")) {
        add(findings, file, "supported-subset", "grid repeat", "repeat(N, 1fr) and repeat(auto-fit, minmax(length, 1fr)) are supported subsets.");
    }
    if (contains(lower, "data-")) {
        add(findings, file, "supported", "dataset", "Existing data-* attributes are exposed through element.dataset.");
    }
    if (contains(lower, ".closest(") || contains(lower, ".matches(")) {
        add(findings, file, "supported-subset", "matches/closest", "Simple tag/class/id/attribute selectors are supported.");
    }
    if (contains(lower, ".style.")) {
        add(findings, file, "supported-subset", "element.style", "display/color/background/backgroundColor/textAlign/fontWeight/width/height are supported.");
    }
    if (contains(lower, "pointerdown") || contains(lower, "touchstart")) {
        add(findings, file, "supported", "pointer/touch events", "pointerdown/pointerup and touchstart/touchend are exposed as mouse-like events.");
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--font-coverage") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--font-coverage requires a text file");
            }
            options.font_coverage_path = argv[++index];
            continue;
        }
        if (arg == "--emit-used-chars") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--emit-used-chars requires an output file");
            }
            options.emit_used_chars_path = argv[++index];
            continue;
        }
        if (arg == "--font-budget") {
            if (index + 1 >= argc ||
                !parse_font_budget(argv[++index], options.budget_glyph_width, options.budget_glyph_height)) {
                throw std::runtime_error("--font-budget requires a WxH glyph size, for example 16x16");
            }
            continue;
        }
        options.files.push_back(arg);
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "capability check failed: " << error.what() << '\n';
        return 2;
    }
    if (options.files.empty()) {
        std::cerr << "usage: jellyframe_capability_check [--font-coverage chars.txt] "
                     "[--emit-used-chars used_chars.txt] [--font-budget WxH] "
                     "<file.html> [style.css] [script.js...]\n";
        return 2;
    }

    std::vector<Finding> findings;
    std::set<std::uint32_t> all_used_codepoints;
    std::set<std::uint32_t> font_coverage;
    if (!options.font_coverage_path.empty()) {
        try {
            font_coverage = load_font_coverage(options.font_coverage_path);
        } catch (const std::exception& error) {
            add(findings, options.font_coverage_path, "error", "font coverage", error.what());
        }
    }

    for (const std::string& file : options.files) {
        try {
            const std::string source = read_file(file);
            scan_html_css_js(file, source, findings);

            std::set<std::uint32_t> file_codepoints;
            collect_used_codepoints(source, file_codepoints);
            all_used_codepoints.insert(file_codepoints.begin(), file_codepoints.end());
            if (!font_coverage.empty()) {
                int missing = 0;
                std::string examples;
                for (std::uint32_t codepoint : file_codepoints) {
                    if (codepoint < 0x80U || font_coverage.find(codepoint) != font_coverage.end()) {
                        continue;
                    }
                    if (missing < 8) {
                        if (!examples.empty()) {
                            examples += ", ";
                        }
                        examples += hex_codepoint(codepoint) + " '" + utf8_for(codepoint) + "'";
                    }
                    ++missing;
                }
                if (missing > 0) {
                    add(findings,
                        file,
                        "unsupported",
                        "font coverage",
                        "font pack misses " + std::to_string(missing) +
                            " non-ASCII codepoints used by this source; examples: " + examples);
                }
            } else {
                int non_ascii = 0;
                for (std::uint32_t codepoint : file_codepoints) {
                    if (codepoint >= 0x80U) {
                        ++non_ascii;
                    }
                }
                if (non_ascii > 0) {
                    add(findings,
                        file,
                        "font-subset",
                        "font coverage",
                        "source uses " + std::to_string(non_ascii) +
                            " non-ASCII codepoints; pass --font-coverage to verify an embedded font pack.");
                }
            }
        } catch (const std::exception& error) {
            add(findings, file, "error", "file", error.what());
        }
    }
    if (!options.emit_used_chars_path.empty()) {
        try {
            write_used_chars(options.emit_used_chars_path, all_used_codepoints);
        } catch (const std::exception& error) {
            add(findings, options.emit_used_chars_path, "error", "used chars", error.what());
        }
    }

    std::size_t unsupported = 0;
    std::cout << "JellyFrame capability check\n";
    for (const Finding& finding : findings) {
        if (finding.level == "unsupported" || finding.level == "error") {
            ++unsupported;
        }
        std::cout << "  [" << finding.level << "] " << finding.file << " :: "
                  << finding.feature << " - " << finding.detail << '\n';
    }
    std::cout << "summary: findings=" << findings.size()
              << " unsupported_or_errors=" << unsupported << '\n';
    if (!options.emit_used_chars_path.empty()) {
        std::cout << "used_chars=" << options.emit_used_chars_path
                  << " non_ascii_count=" << count_non_ascii(all_used_codepoints) << '\n';
    }
    const std::size_t non_ascii_count = count_non_ascii(all_used_codepoints);
    const FontProfileStats font_stats = summarize_font_profile(all_used_codepoints);
    const std::string recommendation = recommended_font_profile(font_stats);
    std::cout << "font_profile recommendation=" << recommendation
              << " detail=\"" << font_profile_detail(recommendation) << "\"\n";
    std::cout << "font_profile_counts ascii_printable=" << font_stats.ascii_printable
              << " non_ascii=" << font_stats.non_ascii
              << " cjk=" << font_stats.cjk_unified
              << " cjk_symbols=" << font_stats.cjk_symbols
              << " latin_extended=" << font_stats.latin_extended
              << " greek=" << font_stats.greek
              << " cyrillic=" << font_stats.cyrillic
              << " kana=" << font_stats.kana
              << " hangul=" << font_stats.hangul
              << " symbols=" << font_stats.symbols
              << " other=" << font_stats.other << '\n';
    if (options.budget_glyph_width > 0 && options.budget_glyph_height > 0) {
        std::cout << "font_budget glyph=" << options.budget_glyph_width << "x" << options.budget_glyph_height
                  << " non_ascii_count=" << non_ascii_count
                  << " estimated_bytes="
                  << estimated_bitmap_font_bytes(non_ascii_count,
                                                 options.budget_glyph_width,
                                                 options.budget_glyph_height)
                  << '\n';
    }
    if (!options.font_coverage_path.empty()) {
        std::size_t covered = 0;
        std::size_t missing = 0;
        for (std::uint32_t codepoint : all_used_codepoints) {
            if (codepoint < 0x80U) {
                continue;
            }
            if (font_coverage.find(codepoint) != font_coverage.end()) {
                ++covered;
            } else {
                ++missing;
            }
        }
        std::cout << "font_coverage used_non_ascii=" << non_ascii_count
                  << " covered=" << covered
                  << " missing=" << missing << '\n';
    }
    return unsupported == 0 ? 0 : 1;
}
