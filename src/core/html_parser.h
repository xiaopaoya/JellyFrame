#pragma once

#include "core/dom.h"

#include <cstddef>
#include <memory>
#include <string>

namespace wearweb {

struct HtmlParserOptions {
    std::size_t max_nodes = 8192;
    std::size_t max_depth = 64;
    std::size_t max_attributes_per_element = 64;
    bool synthesize_document_structure = true;
    bool collapse_whitespace = true;
};

class HtmlParser {
public:
    std::unique_ptr<Node> parse(const std::string& source) const;
    std::unique_ptr<Node> parse(const std::string& source, const HtmlParserOptions& options) const;
};

} // namespace wearweb
