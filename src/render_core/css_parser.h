#pragma once

#include "render_core/style.h"
#include "render_core/diagnostics.h"

#include <cstddef>
#include <string>

namespace jellyframe {

struct CssParserOptions {
    std::size_t max_rules = 4096;
    std::size_t max_declarations_per_rule = 256;
    std::size_t max_nesting_depth = 8;
    bool flatten_layer_blocks = true;
    bool parse_plain_media_blocks = true;
    bool parse_supports_blocks = true;
    int media_viewport_width = 360;
    int media_viewport_height = 240;
    DiagnosticSink* diagnostics = nullptr;
};

class CssParser {
public:
    Stylesheet parse(const std::string& source) const;
    Stylesheet parse(const std::string& source, const CssParserOptions& options) const;
};

} // namespace jellyframe
