#pragma once

#include "core/dom.h"

#include <string>

namespace jellyframe {

bool preserves_dom_text_whitespace(const Node& node);
bool is_collapsible_whitespace_text(const Node& text_node);
std::string normalized_render_text(const Node& text_node);

} // namespace jellyframe
