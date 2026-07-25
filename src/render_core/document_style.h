#pragma once

#include "render_core/diagnostics.h"
#include "render_core/dom.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace jellyframe {

using StylesheetLoadCallback = bool (*)(std::string_view href, std::string& output, void* context);

struct DocumentStyleCollectionOptions {
    std::size_t max_stylesheets = 64;
    std::size_t max_total_bytes = 512 * 1024;
    DiagnosticSink* diagnostics = nullptr;
};

std::string collect_embedded_style_text(const Node& document);
std::string combine_author_css(const std::string& external_css, const Node& document);
std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               const DocumentStyleCollectionOptions& options);
std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context);
std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context,
                               const DocumentStyleCollectionOptions& options);

} // namespace jellyframe
