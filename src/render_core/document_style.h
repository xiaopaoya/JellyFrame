#pragma once

#include "render_core/dom.h"

#include <string>
#include <string_view>

namespace jellyframe {

using StylesheetLoadCallback = bool (*)(std::string_view href, std::string& output, void* context);

std::string collect_embedded_style_text(const Node& document);
std::string combine_author_css(const std::string& external_css, const Node& document);
std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context);

} // namespace jellyframe
