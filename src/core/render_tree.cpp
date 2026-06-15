#include "core/render_tree.h"

#include <memory>

namespace wearweb {
namespace {

Style inherit_text_style(Style style, const Style& parent_style) {
    if (!style.color_specified) {
        style.color = parent_style.color;
    }
    if (!style.font_size_specified) {
        style.font_size = parent_style.font_size;
    }
    if (!style.font_weight_specified) {
        style.font_weight = parent_style.font_weight;
    }
    if (!style.text_align_specified) {
        style.text_align = parent_style.text_align;
    }
    if (!style.line_height_specified) {
        style.line_height = parent_style.line_height;
    }
    if (!style.text_indent_specified) {
        style.text_indent = parent_style.text_indent;
    }
    if (!style.list_style_type_specified) {
        style.list_style_type = parent_style.list_style_type;
    }
    return style;
}

Style inherit_element_style(Style style, const Style& parent_style) {
    return inherit_text_style(style, parent_style);
}

bool creates_render_object(const Node& node, const Style& style) {
    if (style.display == Display::None) {
        return false;
    }
    if (node.type == NodeType::Text) {
        return !node.text.empty();
    }
    return true;
}

bool is_replaced_control(const Node& node) {
    if (node.type != NodeType::Element) {
        return false;
    }
    return node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea" ||
        node.tag_name == "img" || node.tag_name == "picture" || node.tag_name == "video" ||
        node.tag_name == "audio" || node.tag_name == "iframe";
}

} // namespace

RenderTreeBuilder::RenderTreeBuilder(const StyleResolver& style_resolver)
    : style_resolver_(style_resolver) {}

std::unique_ptr<RenderObject> RenderTreeBuilder::build(const Node& document) const {
    auto view = std::make_unique<RenderObject>();
    view->type = RenderObjectType::View;
    view->node = &document;
    view->style = style_resolver_.resolve(document);

    for (const auto& child : document.children) {
        auto object = build_object(*child, &view->style);
        if (object != nullptr) {
            view->children.push_back(std::move(object));
        }
    }
    return view;
}

std::unique_ptr<RenderObject> RenderTreeBuilder::build_object(const Node& node, const Style* parent_style) const {
    Style style = style_resolver_.resolve(node);
    if (parent_style != nullptr) {
        style = node.type == NodeType::Text
            ? inherit_text_style(style, *parent_style)
            : inherit_element_style(style, *parent_style);
    }

    if (!creates_render_object(node, style)) {
        return nullptr;
    }

    auto object = std::make_unique<RenderObject>();
    object->type = render_type_for(node, style);
    object->node = &node;
    object->style = style;

    if (!is_replaced_control(node)) {
        for (const auto& child : node.children) {
            auto child_object = build_object(*child, &object->style);
            if (child_object != nullptr) {
                object->children.push_back(std::move(child_object));
            }
        }
    }

    return object;
}

RenderObjectType RenderTreeBuilder::render_type_for(const Node& node, const Style& style) const {
    if (node.type == NodeType::Text) {
        return RenderObjectType::Text;
    }
    if (style.display == Display::Inline || style.display == Display::InlineBlock) {
        return RenderObjectType::Inline;
    }
    return RenderObjectType::Block;
}

} // namespace wearweb
