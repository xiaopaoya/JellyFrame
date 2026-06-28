#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

std::uint32_t consume_utf8_codepoint(std::string_view text, std::size_t& index);
bool is_cjk_codepoint(std::uint32_t codepoint);
bool has_text_wrap_opportunity(std::string_view text);

} // namespace jellyframe
