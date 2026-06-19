#pragma once

#include "render_core/dom.h"
#include "render_core/diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace jellyframe {

struct HtmlParserOptions {
    std::size_t max_nodes = 8192;
    std::size_t max_depth = 64;
    std::size_t max_attributes_per_element = 64;
    DiagnosticSink* diagnostics = nullptr;
    bool synthesize_document_structure = true;
    bool collapse_whitespace = false;
};

enum HtmlParserDiagnosticFlag : std::uint32_t {
    HtmlParserDiagnosticNone = 0,
    HtmlParserDiagnosticNodeLimit = 1U << 0,
    HtmlParserDiagnosticDepthLimit = 1U << 1,
    HtmlParserDiagnosticAttributeLimit = 1U << 2,
};

using HtmlParserDiagnosticFlags = std::uint32_t;

struct HtmlParseResult {
    std::unique_ptr<Node> document;
    HtmlParserDiagnosticFlags diagnostics = HtmlParserDiagnosticNone;
};

class HtmlParser {
public:
    std::unique_ptr<Node> parse(const std::string& source) const;
    std::unique_ptr<Node> parse(const std::string& source, const HtmlParserOptions& options) const;
    HtmlParseResult parse_with_diagnostics(const std::string& source) const;
    HtmlParseResult parse_with_diagnostics(const std::string& source, const HtmlParserOptions& options) const;
};

} // namespace jellyframe
