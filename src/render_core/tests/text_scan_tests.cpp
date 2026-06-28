#include "render_core/text_scan.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void utf8_codepoint_scanner_advances_safely() {
    const std::string text = "A\xe4\xb8\xad\xf0\x9f\x8c\xa4";
    std::size_t index = 0;
    check(consume_utf8_codepoint(text, index) == 'A', "ASCII codepoint");
    check(index == 1, "ASCII advances one byte");
    check(consume_utf8_codepoint(text, index) == 0x4e2dU, "CJK codepoint");
    check(index == 4, "CJK advances three bytes");
    check(consume_utf8_codepoint(text, index) == 0x1f324U, "four-byte codepoint");
    check(index == text.size(), "four-byte advances to end");
    check(consume_utf8_codepoint(text, index) == 0, "EOF scanner is safe");
}

void wrap_opportunities_are_shared() {
    check(!has_text_wrap_opportunity("Clock"), "plain ASCII word has no wrap opportunity");
    check(has_text_wrap_opportunity("hello world"), "space wraps");
    check(has_text_wrap_opportunity("weather/status"), "slash wraps");
    check(has_text_wrap_opportunity("ui-kit"), "hyphen wraps");
    check(has_text_wrap_opportunity("\xe5\xa4\xa9\xe6\xb0\x94"), "two CJK glyphs wrap");
    check(!has_text_wrap_opportunity("\xe5\xa4\xa9"), "single CJK glyph is treated as single-line");
    check(has_text_wrap_opportunity("\xe9\xa3\x8e\xe3\x80\x81\xe9\x9b\xa8"), "CJK punctuation wraps");
    check(!has_text_wrap_opportunity("\xe2\x84\x83"), "common symbol alone is stable");
}

} // namespace

int main() {
    try {
        utf8_codepoint_scanner_advances_safely();
        wrap_opportunities_are_shared();
    } catch (const std::exception& error) {
        std::cerr << "text scan test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "text scan tests passed\n";
    return 0;
}
