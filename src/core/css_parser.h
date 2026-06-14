#pragma once

#include "core/style.h"

#include <cstddef>
#include <string>

namespace wearweb {

struct CssParserOptions {
    std::size_t max_rules = 4096;
    std::size_t max_declarations_per_rule = 256;
    std::size_t max_nesting_depth = 8;
    bool flatten_layer_blocks = true;
    bool parse_plain_media_blocks = true;
};

class CssParser {
public:
    Stylesheet parse(const std::string& source) const;
    Stylesheet parse(const std::string& source, const CssParserOptions& options) const;
};

} // namespace wearweb
