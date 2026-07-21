#include "render_core/render_tree.h"

#include "render_core/text_normalization.h"

#include <algorithm>
#include <utility>

namespace jellyframe {
namespace {

Style inherit_text_style(Style style, const Style& parent_style) {
    if (!style.visibility_specified) {
        style.visibility_hidden = parent_style.visibility_hidden;
    }
    if (!style.color_specified) {
        style.color = parent_style.color;
    }
    if (!style.font_size_specified) {
        style.font_size = parent_style.font_size;
    }
    if (!style.font_weight_specified) {
        style.font_weight = parent_style.font_weight;
    }
    if (!style.font_family_specified) {
        style.font_family_hash = parent_style.font_family_hash;
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
    if (!style.letter_spacing_specified) {
        style.letter_spacing = parent_style.letter_spacing;
    }
    if (!style.text_transform_specified) {
        style.text_transform = parent_style.text_transform;
    }
    if (!style.text_decoration_specified) {
        style.text_decoration_underline = parent_style.text_decoration_underline;
        style.text_decoration_line_through = parent_style.text_decoration_line_through;
    }
    if (!style.text_shadow_specified) {
        style.text_shadow = parent_style.text_shadow;
    }
    if (!style.white_space_specified) {
        style.white_space_nowrap = parent_style.white_space_nowrap;
    }
    // text-overflow is not generally inherited. The render tree represents a
    // direct text run as its own node, so carry the owning inline container's
    // ellipsis intent only to that anonymous-equivalent run.
    if (style.text_overflow_ellipsis == false) {
        style.text_overflow_ellipsis = parent_style.text_overflow_ellipsis;
    }
    if (!style.overflow_wrap_specified) {
        style.overflow_wrap_anywhere = parent_style.overflow_wrap_anywhere;
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
        return !node.text.empty() && !is_collapsible_whitespace_text(node);
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

bool is_first_summary_child(const Node& parent, const Node& child) {
    for (const auto& sibling : parent.children) {
        if (sibling->type == NodeType::Element && sibling->tag_name == "summary") {
            return sibling.get() == &child;
        }
    }
    return false;
}

bool should_descend_into_child(const Node& parent, const Node& child) {
    if (parent.type == NodeType::Element && parent.tag_name == "details" &&
        parent.attributes.find("open") == parent.attributes.end()) {
        return child.type == NodeType::Element && child.tag_name == "summary" &&
            is_first_summary_child(parent, child);
    }
    return true;
}

void apply_style_override(Style& style, const Node& node, const std::vector<StyleOverride>* overrides) {
    if (overrides == nullptr || overrides->empty()) {
        return;
    }
    for (const StyleOverride& override : *overrides) {
        if (override.node != &node) {
            continue;
        }
        if (override.has_opacity) {
            style.opacity = override.opacity;
        }
        if (override.has_color) {
            style.color = override.color;
            style.color_specified = true;
        }
        if (override.has_background_color) {
            style.background_paint = BackgroundPaintKind::Solid;
            style.background_gradient_axis = GradientAxis::Vertical;
            style.background_gradient_stop_percent = 100;
            style.background_color = override.background_color;
            style.background_color2 = override.background_color;
            style.background_overlay_packed = 0;
        }
        if (override.has_transform) {
            style.transform = override.transform;
        }
        return;
    }
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
    struct PendingNode {
        const Node* node = nullptr;
        RenderObject* parent = nullptr;
    };

    StyleResolveContext style_context;
    auto view = make_render_object(arena);
    RenderObject* view_raw = view.get();
    view->type = RenderObjectType::View;
    view->node = &document;
    view->style = style_resolver_.resolve(document, style_context);
    apply_style_override(view->style, document, options_.style_overrides);

    std::size_t render_object_count = 1;
    bool budget_reported = false;
    const std::size_t max_render_objects = std::max<std::size_t>(1, options_.max_render_objects);

    std::vector<PendingNode> pending;
    pending.reserve(document.children.size());
    for (auto it = document.children.rbegin(); it != document.children.rend(); ++it) {
        pending.push_back(PendingNode{it->get(), view_raw});
    }

    while (!pending.empty()) {
        const PendingNode pending_node = pending.back();
        pending.pop_back();
        const Node& node = *pending_node.node;
        RenderObject& parent = *pending_node.parent;

        if (render_object_count >= max_render_objects) {
            if (!budget_reported) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::RenderTree,
                                  DiagnosticSeverity::Warning,
                                  "render-object-limit",
                                  "Render object budget was reached; remaining visible nodes were skipped",
                                  "Increase max_render_objects or simplify the view hierarchy.");
                budget_reported = true;
            }
            continue;
        }

        Style style = style_resolver_.resolve(node, style_context);
        style = node.type == NodeType::Text
            ? inherit_text_style(style, parent.style)
            : inherit_element_style(style, parent.style);
        apply_style_override(style, node, options_.style_overrides);

        if (!creates_render_object(node, style)) {
            continue;
        }

        auto object = make_render_object(arena);
        RenderObject* object_raw = object.get();
        ++render_object_count;
        object->type = render_type_for(node, style);
        object->node = &node;
        object->style = style;
        parent.children.push_back(std::move(object));

        if (!is_replaced_control(node)) {
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                if (should_descend_into_child(node, *it->get())) {
                    pending.push_back(PendingNode{it->get(), object_raw});
                }
            }
        }
    }

    return view;
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

std::size_t count_render_objects(const RenderObject& root) {
    std::size_t count = 0;
    std::vector<const RenderObject*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const RenderObject* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
