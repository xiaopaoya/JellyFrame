#pragma once

#include "render_core/dom.h"
#include "render_core/geometry.h"

#include <string>

namespace jellyframe {

bool preserves_dom_text_whitespace(const Node& node);
bool is_collapsible_whitespace_text(const Node& text_node);
std::string normalized_render_text(const Node& text_node);
std::string transformed_render_text(const Node& text_node, TextTransform transform);

} // namespace jellyframe
