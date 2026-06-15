#include "core/render_tree.h"

#include <algorithm>
#include <utility>

namespace jellyframe {
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

void RenderObjectDeleter::operator()(RenderObject* object) const {
    if (!arena_owned) {
        delete object;
    }
}

RenderTreeBuilder::RenderTreeBuilder(const StyleResolver& style_resolver, RenderTreeOptions options)
    : style_resolver_(style_resolver),
      options_(options) {}

RenderObjectPtr RenderTreeBuilder::build(const Node& document) const {
    return build_with_arena(document, nullptr);
}

RenderObjectPtr RenderTreeBuilder::build(const Node& document, MonotonicArena& arena) const {
    return build_with_arena(document, &arena);
}

RenderObjectPtr RenderTreeBuilder::build_with_arena(const Node& document, MonotonicArena* arena) const {
    auto view = make_render_object(arena);
    view->type = RenderObjectType::View;
    view->node = &document;
    view->style = style_resolver_.resolve(document);

    std::size_t render_object_count = 1;
    for (const auto& child : document.children) {
        auto object = build_object(*child, &view->style, render_object_count, arena);
        if (object != nullptr) {
            view->children.push_back(std::move(object));
        }
    }
    return view;
}

RenderObjectPtr RenderTreeBuilder::build_object(const Node& node,
                                                const Style* parent_style,
                                                std::size_t& render_object_count,
                                                MonotonicArena* arena) const {
    const std::size_t max_render_objects = std::max<std::size_t>(1, options_.max_render_objects);
    if (render_object_count >= max_render_objects) {
        return nullptr;
    }

    Style style = style_resolver_.resolve(node);
    if (parent_style != nullptr) {
        style = node.type == NodeType::Text
            ? inherit_text_style(style, *parent_style)
            : inherit_element_style(style, *parent_style);
    }

    if (!creates_render_object(node, style)) {
        return nullptr;
    }

    auto object = make_render_object(arena);
    ++render_object_count;
    object->type = render_type_for(node, style);
    object->node = &node;
    object->style = style;

    if (!is_replaced_control(node)) {
        for (const auto& child : node.children) {
            auto child_object = build_object(*child, &object->style, render_object_count, arena);
            if (child_object != nullptr) {
                object->children.push_back(std::move(child_object));
            }
        }
    }

    return object;
}

RenderObjectPtr RenderTreeBuilder::make_render_object(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return RenderObjectPtr(new RenderObject, RenderObjectDeleter{false});
    }
    return RenderObjectPtr(&arena->create<RenderObject>(), RenderObjectDeleter{true});
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

} // namespace jellyframe
