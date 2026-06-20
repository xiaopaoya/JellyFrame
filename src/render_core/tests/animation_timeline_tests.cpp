#include "render_core/animation_invalidation.h"
#include "render_core/animation_timeline.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Node* find_first_by_class(Node& node, const std::string& class_name) {
    if (node.type == NodeType::Element && node.has_class(class_name)) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_first_by_class(*child, class_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const RenderObject* find_first_by_class(const RenderObject& object, const std::string& class_name) {
    if (object.node != nullptr && object.node->type == NodeType::Element && object.node->has_class(class_name)) {
        return &object;
    }
    for (const auto& child : object.children) {
        const RenderObject* found = find_first_by_class(*child, class_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void css_transition_and_transform_subset_is_parsed() {
    CssParser parser;
    auto element = make_element("button");
    element->attributes["class"] = "card";
    StyleResolver resolver(parser.parse(
        ".card { transition: opacity 200ms linear, transform .1s ease-out; "
        "opacity: .5; transform: translate(10px, 4px) scale(.9); }"));
    const Style style = resolver.resolve(*element);
    check(style.transition_count == 2, "transition shorthand creates bounded transition entries");
    check(style.transitions[0].duration_ms == 200, "transition duration ms parsed");
    check(style.transitions[0].property == AnimatableProperty::Opacity, "transition property parsed");
    check(style.opacity > 0.49F && style.opacity < 0.51F, "opacity parsed");

    Transform2D transform;
    check(parse_css_transform_2d(style.transform, transform), "normalized transform parses");
    check(transform.translate_x > 9.0F && transform.translate_x < 11.0F, "translate x parsed");
    check(transform.translate_y > 3.0F && transform.translate_y < 5.0F, "translate y parsed");
    check(transform.scale_x > 0.89F && transform.scale_x < 0.91F, "scale x parsed");
}

void animation_timeline_samples_paint_only_properties() {
    auto node = make_element("button");
    Style from;
    from.opacity = 0.0F;
    from.background_color = Color{0, 0, 0, 255};
    from.color = Color{255, 255, 255, 255};
    Style to = from;
    to.opacity = 1.0F;
    to.background_color = Color{100, 0, 0, 255};
    to.color = Color{0, 255, 255, 255};
    to.transitions[0] = StyleTransition{AnimatableProperty::Opacity, 100, 0, AnimationTimingFunction::Linear};
    to.transitions[1] = StyleTransition{AnimatableProperty::BackgroundColor, 100, 0, AnimationTimingFunction::Linear};
    to.transitions[2] = StyleTransition{AnimatableProperty::Color, 100, 0, AnimationTimingFunction::Linear};
    to.transition_count = 3;

    AnimationTimeline timeline;
    check(timeline.start_transitions(*node, from, to, 0), "timeline starts transitions");
    std::vector<StyleOverride> overrides;
    check(timeline.sample(50, overrides), "timeline samples active transition");
    check(overrides.size() == 1, "paint property overrides are grouped by node");
    check(overrides[0].has_opacity && overrides[0].opacity > 0.45F && overrides[0].opacity < 0.55F,
          "opacity is interpolated");
    check(overrides[0].has_background_color && overrides[0].background_color.r >= 49 &&
              overrides[0].background_color.r <= 51,
          "background color is interpolated");
    check(overrides[0].has_color && overrides[0].color.g >= 127, "text color is interpolated");
    check(timeline.sample(100, overrides), "timeline samples final frame");
    check(timeline.empty(), "finished transition is removed");
}

void render_tree_applies_animation_style_overrides() {
    HtmlParser html;
    CssParser css;
    auto document = html.parse("<body><button class='card'>Go</button></body>");
    Node* button_node = find_first_by_class(*document, "card");
    check(button_node != nullptr, "button node exists");
    StyleResolver resolver(css.parse(".card { opacity: 1; background: #000000; }"));

    std::vector<StyleOverride> overrides;
    StyleOverride override;
    override.node = button_node;
    override.has_opacity = true;
    override.opacity = 0.25F;
    override.has_background_color = true;
    override.background_color = Color{255, 0, 0, 255};
    overrides.push_back(override);

    RenderTreeOptions options;
    options.style_overrides = &overrides;
    RenderTreeBuilder builder(resolver, options);
    auto tree = builder.build(*document);
    const RenderObject* button = find_first_by_class(*tree, "card");
    check(button != nullptr, "button render object exists");
    check(button->style.opacity > 0.24F && button->style.opacity < 0.26F, "opacity override applied");
    check(button->style.background_color.r == 255, "background override applied");
}

void software_compositor_applies_translate_transform() {
    HtmlParser html;
    CssParser css;
    auto document = html.parse("<body><div class='box'></div></body>");
    StyleResolver resolver(css.parse(
        "body { margin: 0; }"
        ".box { width: 20px; height: 20px; background: #ff0000; transform: translate(10px, 0); }"));
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout(resolver);
    auto layout_tree = layout.layout(*render_tree, 80);
    LayerTreeBuilder layer_builder;
    auto layer_tree = layer_builder.build(*layout_tree);
    SoftwareCompositor compositor;
    FrameBuffer frame = compositor.render(*layer_tree, 80, 40, Color{255, 255, 255, 255});
    check(frame.pixel(2, 2).r == 255 && frame.pixel(2, 2).g == 255, "original position remains background");
    check(frame.pixel(12, 2).r > 200 && frame.pixel(12, 2).g < 80, "translated box is painted");
}

void animation_invalidation_covers_previous_and_current_transform_bounds() {
    HtmlParser html;
    CssParser css;
    auto document = html.parse("<body><div class='box'></div></body>");
    Node* box_node = find_first_by_class(*document, "box");
    check(box_node != nullptr, "animated node exists");
    StyleResolver resolver(css.parse(
        "body { margin: 0; }"
        ".box { width: 20px; height: 20px; background: #ff0000; transform: translate(0px, 0px); }"));
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout(resolver);
    auto layout_tree = layout.layout(*render_tree, 120);

    StyleOverride previous;
    previous.node = box_node;
    previous.has_transform = true;
    previous.transform = "translate(0px,0px)";
    StyleOverride current;
    current.node = box_node;
    current.has_transform = true;
    current.transform = "translate(30px,0px)";
    const DirtyRegionResult region = compute_animation_dirty_region(
        *layout_tree,
        std::vector<StyleOverride>{previous},
        std::vector<StyleOverride>{current},
        AnimationInvalidationOptions{Rect{0, 0, 120, 80}, 4, 0});

    check(region.mode == DirtyRegionMode::DirtyRects, "animation invalidation produces dirty rects");
    check(region.rects.size() == 1, "single animated box stays one dirty rect");
    check(region.rects.front().width >= 50 && region.rects.front().width < 80,
          "dirty rect covers previous and current transform bounds without full viewport");
    check(region.rects.front().height == 20, "dirty rect keeps box height");
}

} // namespace

int main() {
    try {
        css_transition_and_transform_subset_is_parsed();
        animation_timeline_samples_paint_only_properties();
        render_tree_applies_animation_style_overrides();
        software_compositor_applies_translate_transform();
        animation_invalidation_covers_previous_and_current_transform_bounds();
    } catch (const std::exception& error) {
        std::cerr << "animation timeline test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "animation timeline tests passed\n";
    return 0;
}
