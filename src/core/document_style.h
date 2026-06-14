#pragma once

#include "core/dom.h"

#include <string>

namespace wearweb {

std::string collect_embedded_style_text(const Node& document);
std::string combine_author_css(const std::string& external_css, const Node& document);

} // namespace wearweb
