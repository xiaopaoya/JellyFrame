#include "render_core/css_parser.h"
#include "render_core/animation_timeline.h"
#include "render_core/feature_config.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/hit_test.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/text_scan.h"

#include <iostream>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

struct BuiltPipeline {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
    LayerNodePtr layer_tree;

    BuiltPipeline(std::unique_ptr<Node> document_in,
                  Stylesheet stylesheet_in,
                  StyleResolver resolver_in,
                  RenderObjectPtr render_tree_in,
                  LayoutBoxPtr layout_tree_in,
                  LayerNodePtr layer_tree_in)
        : document(std::move(document_in)),
          stylesheet(std::move(stylesheet_in)),
          resolver(std::move(resolver_in)),
          render_tree(std::move(render_tree_in)),
          layout_tree(std::move(layout_tree_in)),
          layer_tree(std::move(layer_tree_in)) {}
};

BuiltPipeline build_pipeline(const char* html, const char* css) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    return BuiltPipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                         std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

Node* find_node_by_id(Node& node, const std::string& id) {
    if (node.attribute("id") == id) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (Node* found = find_node_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

const LayerNode* find_layer_with_reason(const LayerNode& layer, LayerReason reason) {
    if ((layer.reasons & reason) != 0U) {
        return &layer;
    }
    for (const auto& child : layer.children) {
        const LayerNode* found = find_layer_with_reason(*child, reason);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_layout_by_class(const LayoutBox& box, const std::string& class_name) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->has_class(class_name)) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_layout_by_class(*child, class_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_layout_by_id(const LayoutBox& box, const std::string& id) {
    if (box.node != nullptr && box.node->attribute("id") == id) {
        return &box;
    }
    for (const auto& child : box.children) {
        if (const LayoutBox* found = find_layout_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_first_text_layout(const LayoutBox& box) {
    if (box.node != nullptr && box.node->type == NodeType::Text) {
        return &box;
    }
    for (const auto& child : box.children) {
        if (const LayoutBox* found = find_first_text_layout(*child)) {
            return found;
        }
    }
    return nullptr;
}

int fixed_scroll_offset(const Node& node, int max_scroll_y, void*) {
    if (node.attribute("id") == "list") {
        return std::min(24, max_scroll_y);
    }
    return 0;
}

void overflow_hidden_creates_clip_layer() {
    auto pipeline = build_pipeline("<body><section class='clip'><p>Visible</p></section></body>",
                                   ".clip { overflow: hidden; height: 20px; background: #ffffff; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOverflowClip);
    check(layer != nullptr, "overflow layer exists");
    check(layer->type == LayerType::Clip, "overflow layer is clip layer");
    check(layer->has_clip, "overflow layer has clip");
}

void overflow_y_auto_creates_vertical_scroll_clip_layer() {
    auto pipeline = build_pipeline("<body><section class='list'><p>One</p><p>Two</p></section></body>",
                                   ".list { overflow-y: auto; height: 20px; background: #ffffff; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOverflowClip);
    check(layer != nullptr, "overflow-y auto layer exists");
    check(layer->type == LayerType::Clip, "overflow-y auto uses the vertical scroll clip layer");
    check(layer->has_clip, "overflow-y auto layer has clip");
}

void visibility_preserves_layout_and_suppresses_hidden_paint_and_hit_testing() {
    auto pipeline = build_pipeline(
        "<body><section id='hidden'><span id='visible-child'></span></section><div id='after'></div></body>",
        "body { margin: 0; }"
        "#hidden { width: 80px; height: 24px; visibility: hidden; background: #000000; }"
        "#visible-child { display: block; width: 20px; height: 24px; visibility: visible; background: #ff0000; }"
        "#after { width: 80px; height: 12px; background: #0000ff; }");

    const LayoutBox* hidden = find_layout_by_id(*pipeline.layout_tree, "hidden");
    const LayoutBox* visible_child = find_layout_by_id(*pipeline.layout_tree, "visible-child");
    const LayoutBox* after = find_layout_by_id(*pipeline.layout_tree, "after");
    check(hidden != nullptr && visible_child != nullptr && after != nullptr, "visibility fixture nodes exist");
    check(hidden->rect.height == 24, "hidden box preserves its layout height");
    check(after->rect.y == 24, "following sibling keeps the hidden box flow position");

    LayerTreeBuilder builder;
    const DisplayList commands = builder.flatten(*pipeline.layer_tree);
    bool found_hidden_black = false;
    bool found_visible_red = false;
    bool found_after_blue = false;
    for (const DisplayCommand& command : commands) {
        if (command.type != DisplayCommandType::FillRect) {
            continue;
        }
        found_hidden_black |= command.color.r == 0 && command.color.g == 0 && command.color.b == 0;
        found_visible_red |= command.color.r == 255 && command.color.g == 0 && command.color.b == 0;
        found_after_blue |= command.color.r == 0 && command.color.g == 0 && command.color.b == 255;
    }
    check(!found_hidden_black, "visibility hidden suppresses the element's paint");
    check(found_visible_red, "explicit visible descendant can paint through hidden inheritance");
    check(found_after_blue, "following visible sibling still paints");

    auto shown = build_pipeline(
        "<body><section id='hidden'><span id='visible-child'></span></section><div id='after'></div></body>",
        "body { margin: 0; }"
        "#hidden { width: 80px; height: 24px; visibility: visible; background: #000000; }"
        "#visible-child { display: block; width: 20px; height: 24px; visibility: visible; background: #ff0000; }"
        "#after { width: 80px; height: 12px; background: #0000ff; }");
    const DisplayList shown_commands = builder.flatten(*shown.layer_tree);
    check(commands.size() < shown_commands.size(),
          "visibility hidden emits fewer display commands without changing layout");

    HitTester hit_tester;
    const HitTestResult child_hit = hit_tester.hit_test(*pipeline.layer_tree, 8, 8);
    check(child_hit.node == visible_child->node, "visible descendant remains hit-testable");
    const HitTestResult hidden_hit = hit_tester.hit_test(*pipeline.layer_tree, 48, 8);
    check(hidden_hit.node != hidden->node, "hidden element is not a hit-test target");
}

void text_spacing_and_anywhere_wrap_emit_only_declared_extra_commands() {
    auto normal = build_pipeline("<body><p>AB</p></body>", "p { font-size: 10px; }");
    LayerTreeBuilder builder;
    const DisplayList normal_commands = builder.flatten(*normal.layer_tree);
    int normal_text_count = 0;
    for (const DisplayCommand& command : normal_commands) {
        normal_text_count += command.type == DisplayCommandType::Text ? 1 : 0;
    }
    check(normal_text_count == 1, "normal text remains one display command");

    auto spaced = build_pipeline("<body><p>AB</p></body>", "p { font-size: 10px; letter-spacing: 2px; }");
    const LayoutBox* spaced_layout = find_first_text_layout(*spaced.layout_tree);
    check(spaced_layout != nullptr && spaced_layout->style.letter_spacing == 2,
          "letter spacing reaches text layout style");
    const DisplayList spaced_commands = builder.flatten(*spaced.layer_tree);
    int spaced_text_count = 0;
    for (const DisplayCommand& command : spaced_commands) {
        spaced_text_count += command.type == DisplayCommandType::Text ? 1 : 0;
    }
    check(spaced_text_count == 2, "declared letter spacing emits one command per scalar");

    auto wrapped = build_pipeline("<body><p>ABCDE</p></body>",
                                  "p { width: 12px; font-size: 10px; overflow-wrap: anywhere; }");
    const DisplayList wrapped_commands = builder.flatten(*wrapped.layer_tree);
    int wrapped_text_count = 0;
    for (const DisplayCommand& command : wrapped_commands) {
        wrapped_text_count += command.type == DisplayCommandType::Text ? 1 : 0;
    }
    check(wrapped_text_count > 1, "declared overflow-wrap emits individual wrapped line commands");
}

void scroll_container_offsets_descendant_paint() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><section id='list'><div id='one'></div><div id='two'></div><div id='three'></div></section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#list { width: 80px; height: 24px; overflow: scroll; background: #ffffff; }"
        "#one { width: 80px; height: 24px; background: #000000; }"
        "#two { width: 80px; height: 24px; background: #000000; }"
        "#three { width: 80px; height: 24px; background: #000000; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);
    LayerTreeBuilderOptions options;
    options.scroll_resolver = ScrollOffsetResolver{fixed_scroll_offset, nullptr};
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);

    const LayerNode* scroll_layer = find_layer_with_reason(*layer_tree, LayerReasonOverflowClip);
    check(scroll_layer != nullptr && scroll_layer->scroll_y == 24 && scroll_layer->max_scroll_y >= 24,
          "scroll layer records clamped offset");

    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);
    bool found_shifted_child = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.color.r == 0 &&
            command.rect.y == 0 && command.rect.height == 24) {
            found_shifted_child = true;
        }
    }
    check(found_shifted_child, "scroll offset moves second row into viewport");
}

void scroll_container_keeps_absolute_sibling_navigation_fixed() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='list'><div></div><div></div><div></div></section>"
        "<nav id='bottom-nav'></nav></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { position: relative; width: 100px; height: 120px; }"
        "#list { width: 100px; height: 80px; overflow: scroll; }"
        "#list div { width: 100px; height: 40px; background: #000000; }"
        "#bottom-nav { position: absolute; left: 0; bottom: 0; width: 100px; height: 20px; background: #ff0000; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120, 120);
    LayerTreeBuilderOptions options;
    options.scroll_resolver = ScrollOffsetResolver{fixed_scroll_offset, nullptr};
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

    bool found_shifted_list_row = false;
    bool found_fixed_navigation = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::FillRect) {
            continue;
        }
        if (command.color.r == 0 && command.color.g == 0 && command.color.b == 0 &&
            command.rect.y == 16 && command.rect.height == 40) {
            found_shifted_list_row = true;
        }
        if (command.color.r == 255 && command.color.g == 0 && command.color.b == 0 &&
            command.rect.x == 0 && command.rect.y == 100 &&
            command.rect.width == 100 && command.rect.height == 20) {
            found_fixed_navigation = true;
        }
    }
    check(found_shifted_list_row, "scroll container shifts only its own content");
    check(found_fixed_navigation, "absolute navigation sibling stays outside the scroll transform");
}

void scroll_indicator_is_opt_in_overlay() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><section id='list'><div></div><div></div><div></div></section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#list { width: 80px; height: 32px; overflow: scroll; background: #101010; }"
        "div { width: 80px; height: 24px; background: #000000; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    LayerTreeBuilderOptions options;
    options.paint_scroll_indicators = true;
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

    bool found_thumb = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect &&
            command.rect.width == 3 &&
            command.color.a == 176) {
            found_thumb = true;
        }
    }
    check(found_thumb, "opt-in scroll indicator emits a thumb overlay");
}

void opacity_layer_flattens_alpha() {
    auto pipeline = build_pipeline("<body><section class='fade'>Faded</section></body>",
                                   ".fade { opacity: .5; background: #000000; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOpacity);
    check(layer != nullptr, "opacity layer exists");
    check(layer->type == LayerType::Composited, "opacity layer is composited");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_translucent_fill = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.color.a > 0 && command.color.a < 255) {
            found_translucent_fill = true;
        }
    }
    check(found_translucent_fill, "flatten applies layer opacity");
}

void opacity_overrides_reuse_existing_composited_layers() {
    auto pipeline = build_pipeline("<body><section class='fade'>Faded</section></body>",
                                   ".fade { opacity: .5; background: #000000; }");
    const LayoutBox* box = find_layout_by_class(*pipeline.layout_tree, "fade");
    check(box != nullptr && box->node != nullptr, "opacity override target exists");

    LayerTreeOverrideScratch scratch;
    scratch.pending.reserve(count_layers(*pipeline.layer_tree));
    StyleOverride opacity;
    opacity.node = box->node;
    opacity.has_opacity = true;
    opacity.opacity = 0.25F;
    check(apply_opacity_overrides_to_layer_tree(*pipeline.layer_tree, {opacity}, scratch),
          "opacity-only override reuses existing layer");
    const LayerNode* updated = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOpacity);
    check(updated != nullptr && updated->opacity > 0.24F && updated->opacity < 0.26F,
          "opacity override updates compositing state without rebuilding display commands");
    check(scratch.pending.empty(), "opacity override scratch is cleared after use");

    StyleOverride unsupported = opacity;
    unsupported.has_color = true;
    unsupported.color = Color{255, 0, 0, 255};
    check(!apply_opacity_overrides_to_layer_tree(*pipeline.layer_tree, {unsupported}, scratch),
          "color override conservatively rejects layer reuse");
    check(updated->opacity > 0.24F && updated->opacity < 0.26F,
          "rejected override leaves cached layer unchanged");
}

void flatten_into_reuses_storage_and_matches_flatten() {
    auto pipeline = build_pipeline(
        "<body><section class='card'><button>Open</button><p>Text</p></section></body>",
        ".card { padding: 4px; background: #ffffff; border-radius: 8px; }"
        "button { display: inline-block; width: 48px; height: 20px; background: #222222; }");

    LayerTreeBuilder layer_tree_builder;
    const DisplayList expected = layer_tree_builder.flatten(*pipeline.layer_tree);
    DisplayList reused;
    reused.reserve(expected.size() + 8);
    const std::size_t capacity = reused.capacity();
    layer_tree_builder.flatten_into(*pipeline.layer_tree, reused);

    check(reused.size() == expected.size(), "flatten_into command count matches flatten");
    check(reused.capacity() == capacity, "flatten_into keeps caller storage when capacity is sufficient");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check(reused[i].type == expected[i].type, "flatten_into command type matches");
        check(reused[i].rect.x == expected[i].rect.x &&
                  reused[i].rect.y == expected[i].rect.y &&
                  reused[i].rect.width == expected[i].rect.width &&
                  reused[i].rect.height == expected[i].rect.height,
              "flatten_into command rect matches");
        check(reused[i].color.r == expected[i].color.r &&
                  reused[i].color.g == expected[i].color.g &&
                  reused[i].color.b == expected[i].color.b &&
                  reused[i].color.a == expected[i].color.a,
              "flatten_into command color matches");
    }
}

void rounded_equal_border_emits_stroke_command() {
    auto pipeline = build_pipeline("<body><section class='pill'>Rounded</section></body>",
                                   ".pill { width: 80px; height: 24px; border: 2px solid #ffffff; "
                                   "border-radius: 12px; background: #000000; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_rounded_stroke = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::StrokeRect && command.border_radius == 12 &&
            command.stroke_width == 2) {
            found_rounded_stroke = true;
        }
    }
    check(found_rounded_stroke, "rounded equal-width border uses stroke command");
}

void linear_gradient_background_emits_gradient_command() {
    auto pipeline = build_pipeline("<body><section class='gel'>Gel</section></body>",
                                   ".gel { display: block; width: 60px; height: 30px; "
                                   "background: linear-gradient(to right, #102030, rgba(80, 120, 160, 0.5)); "
                                   "border-radius: 8px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_gradient = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::LinearGradient) {
            found_gradient = true;
            check(command.color.r == 0x10 && command.color2.r == 80, "gradient command carries colors");
            check(command.gradient_axis == GradientAxis::Horizontal, "gradient command carries axis");
            check(command.border_radius == 8, "gradient command carries radius");
        }
    }
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    check(found_gradient, "linear-gradient background emits gradient command");
#else
    check(!found_gradient, "linear-gradient background does not emit a modern command when disabled");
#endif
}

void outline_and_text_shadow_emit_paint_commands() {
    auto pipeline = build_pipeline("<body><button class='cta'>Glow</button></body>",
                                   ".cta { display: block; width: 80px; height: 32px; "
                                   "outline: 2px solid rgba(255,255,255,0.5); "
                                   "text-shadow: 1px 1px 2px rgba(0,0,0,0.35); "
                                   "text-decoration: underline; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_outline = false;
    bool found_decoration = false;
    int glow_text_commands = 0;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::StrokeRect && command.stroke_width == 2 &&
            command.color.a >= 126 && command.color.a <= 128) {
            found_outline = true;
        }
        if (command.type == DisplayCommandType::FillRect && command.rect.height <= 2 &&
            command.rect.width > 20 && command.color.a == 255) {
            found_decoration = true;
        }
        if (command.type == DisplayCommandType::Text && command.text == "Glow") {
            ++glow_text_commands;
        }
    }
    check(found_outline, "outline emits stroke command");
    check(found_decoration, "text-decoration emits a small paint command");
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    check(glow_text_commands >= 2, "text-shadow emits shadow text before main text");
#else
    check(glow_text_commands == 1, "disabled text-shadow keeps only the main text command");
#endif
}

void outline_offset_expands_focus_stroke_without_affecting_layout() {
    auto pipeline = build_pipeline("<body><button class='cta'>Open</button></body>",
                                   "body { margin: 0; } .cta { display: block; width: 40px; height: 20px; "
                                   "outline: 2px solid #ffffff; outline-offset: 3px; }");

    const LayoutBox* button = find_layout_by_class(*pipeline.layout_tree, "cta");
    check(button != nullptr, "outline offset fixture button exists");
    LayerTreeBuilder layer_tree_builder;
    const DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::StrokeRect || command.stroke_width != 2) {
            continue;
        }
        check(command.rect.x == button->rect.x - 5 && command.rect.y == button->rect.y - 5 &&
                  command.rect.width == button->rect.width + 10 && command.rect.height == button->rect.height + 10,
              "outline offset expands only the non-layout stroke geometry");
        check(command.border_radius == 0, "square outline remains square after offset");
        return;
    }
    check(false, "outline offset fixture emits a stroke command");
}

void z_index_orders_child_layers() {
    auto pipeline = build_pipeline("<body><div class='back'>Back</div><div class='front'>Front</div></body>",
                                   ".back { position: relative; z-index: 5; }"
                                   ".front { position: relative; z-index: 10; }");

    check(pipeline.layer_tree->children.size() >= 2, "positioned children create layers");
    const LayerNode* previous = nullptr;
    for (const auto& child : pipeline.layer_tree->children) {
        if ((child->reasons & LayerReasonZIndex) == 0U) {
            continue;
        }
        if (previous != nullptr) {
            check(previous->z_index <= child->z_index, "z-index children are sorted");
        }
        previous = child.get();
    }
}

void progress_and_meter_emit_value_fill() {
    auto pipeline = build_pipeline("<body><progress value='70' max='100'></progress><meter min='0' max='10' value='8'></meter></body>",
                                   "");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    int colored_bar_count = 0;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect &&
            ((command.color.r == 37 && command.color.g == 99 && command.color.b == 235) ||
             (command.color.r == 22 && command.color.g == 163 && command.color.b == 74))) {
            ++colored_bar_count;
        }
    }
    check(colored_bar_count == 2, "progress and meter emit filled bars");
}

void inline_mark_background_shrinks_to_text() {
    auto pipeline = build_pipeline("<body><p>Use <mark>mark</mark> text</p></body>", "");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_compact_mark = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect &&
            command.color.r == 254 && command.color.g == 240 && command.color.b == 138 &&
            command.rect.width > 0 && command.rect.width < 120) {
            found_compact_mark = true;
        }
    }
    check(found_compact_mark, "mark background shrinks to text bounds");
}

void inline_run_flows_horizontally() {
    auto pipeline = build_pipeline("<body><p>A <mark>B</mark> C</p></body>", "");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    std::vector<DisplayCommand> text_commands;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Text) {
            text_commands.push_back(command);
        }
    }

    check(text_commands.size() >= 3, "inline text commands exist");
    check(text_commands[0].rect.y == text_commands[1].rect.y &&
              text_commands[1].rect.y == text_commands[2].rect.y,
          "inline run stays on one line");
    check(text_commands[0].rect.x < text_commands[1].rect.x &&
              text_commands[1].rect.x < text_commands[2].rect.x,
          "inline run advances horizontally");
}

void centered_inline_text_aligns_in_parent() {
    auto pipeline = build_pipeline("<body><h1>Centered</h1></body>", "h1 { text-align: center; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Text && command.text == "Centered") {
            check(command.rect.x > 40, "centered heading text is shifted from the left edge");
            return;
        }
    }
    check(false, "centered heading text command exists");
}

void button_inline_block_shrink_wraps_text() {
    auto pipeline = build_pipeline("<body><button>Submit</button></body>",
                                   "button { padding: 8px 24px; border: none; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect &&
            command.color.r == 243 && command.color.g == 244 && command.color.b == 246) {
            check(command.rect.width > 60 && command.rect.width < 160,
                  "button shrink-wraps instead of filling the line");
            return;
        }
    }
    check(false, "button background command exists");
}

void select_does_not_paint_option_list_inline() {
    auto pipeline = build_pipeline(
        "<body><select><option>Alpha</option><option>Beta</option></select></body>", "");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool painted_selected_option = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::Text) {
            continue;
        }
        check(command.text != "Beta", "select does not paint the collapsed option list");
        if (command.text == "Alpha") {
            painted_selected_option = true;
        }
    }
    check(painted_selected_option, "select paints the selected option text");
}

void form_paint_only_state_changes_rebuild_visible_commands() {
    auto range_pipeline = build_pipeline(
        "<body><input id='volume' type='range' min='0' max='100' value='0'></body>",
        "input { display: block; width: 120px; height: 24px; }");
    Node* range = find_node_by_id(*range_pipeline.document, "volume");
    check(range != nullptr, "range paint fixture exists");

    LayerTreeBuilder builder;
    const DisplayList before_range = builder.flatten(*range_pipeline.layer_tree);
    check(set_form_control_value(*range, "100"), "range value becomes paint dirty");
    check((subtree_dirty_flags(*range_pipeline.document) & DomDirtyPaint) != 0U,
          "range value marks paint dirty");
    auto after_range_tree = builder.build(*range_pipeline.layout_tree);
    const DisplayList after_range = builder.flatten(*after_range_tree);

    auto widest_blue_fill = [](const DisplayList& commands) {
        int width = 0;
        for (const DisplayCommand& command : commands) {
            if (command.type == DisplayCommandType::FillRect &&
                command.color.r == 37 && command.color.g == 99 && command.color.b == 235) {
                width = std::max(width, command.rect.width);
            }
        }
        return width;
    };
    check(widest_blue_fill(after_range) > widest_blue_fill(before_range),
          "range paint-only rebuild changes the visible fill command");

    auto select_pipeline = build_pipeline(
        "<body><select id='choice'><option>Alpha</option><option>Beta</option></select></body>",
        "select { display: block; width: 120px; height: 24px; }");
    Node* select = find_node_by_id(*select_pipeline.document, "choice");
    check(select != nullptr, "select paint fixture exists");
    const DisplayList before_select = builder.flatten(*select_pipeline.layer_tree);
    check(set_form_control_selected_index(*select, 1), "select value becomes paint dirty");
    check((subtree_dirty_flags(*select_pipeline.document) & DomDirtyPaint) != 0U,
          "select value marks paint dirty");
    auto after_select_tree = builder.build(*select_pipeline.layout_tree);
    const DisplayList after_select = builder.flatten(*after_select_tree);
    auto has_text = [](const DisplayList& commands, const char* text) {
        for (const DisplayCommand& command : commands) {
            if (command.type == DisplayCommandType::Text && command.text == text) {
                return true;
            }
        }
        return false;
    };
    check(has_text(before_select, "Alpha") && has_text(after_select, "Beta"),
          "select paint-only rebuild changes the visible option text");
}

void select_popup_paints_above_document_and_hit_tests_to_owner() {
#if !JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    return;
#else
    auto pipeline = build_pipeline(
        "<body><select id='choice'><option>Alpha</option><option>Beta</option></select><p>Behind</p></body>",
        "body { height: 120px; } select { display: block; width: 100px; height: 24px; } p { margin: 0; }");
    Node* select = find_node_by_id(*pipeline.document, "choice");
    const LayoutBox* select_box = find_layout_by_id(*pipeline.layout_tree, "choice");
    check(select != nullptr && select_box != nullptr, "popup select fixture exists");
    check(activate_form_control(*select), "popup select opens");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyOverlay) != 0U,
          "popup lifecycle uses transient-overlay invalidation");

    LayerTreeBuilder builder;
    auto popup_tree = builder.build(*pipeline.layout_tree);
    const DisplayList commands = builder.flatten(*popup_tree);
    bool painted_beta = false;
    for (const DisplayCommand& command : commands) {
        painted_beta = painted_beta || (command.type == DisplayCommandType::Text && command.text == "Beta");
    }
    check(painted_beta, "open select paints the option overlay");

    const SelectPopupGeometry geometry = select_popup_geometry(
        select_box->rect,
        popup_tree->bounds,
        form_control_option_count(*select),
        std::max(20, select_box->style.font_size + 6));
    const HitTestResult hit = HitTester{}.hit_test(*popup_tree,
                                                    geometry.rect.x + 2,
                                                    geometry.rect.y + geometry.row_height + 1);
    check(hit.node == select && hit.box == select_box, "popup option hit resolves to select owner");
#endif
}

void grid_auto_fit_gap_span_and_aspect_ratio_layout() {
    auto pipeline = build_pipeline(
        "<body><div class='grid'>"
        "<section class='card a'>A</section>"
        "<section class='card b'>B</section>"
        "<section class='card wide'>Wide</section>"
        "<section class='media'></section>"
        "</div></body>",
        ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(80px, 1fr));"
        "grid-auto-rows: minmax(40px, auto); gap: 10px; }"
        ".card { padding: 4px; background: #ffffff; }"
        ".wide { grid-column: span 2; }"
        ".media { aspect-ratio: 16 / 9; background: #e2e8f0; }");

    const LayoutBox* first = find_layout_by_class(*pipeline.layout_tree, "a");
    const LayoutBox* second = find_layout_by_class(*pipeline.layout_tree, "b");
    const LayoutBox* wide = find_layout_by_class(*pipeline.layout_tree, "wide");
    const LayoutBox* media = find_layout_by_class(*pipeline.layout_tree, "media");
    check(first != nullptr && second != nullptr && wide != nullptr && media != nullptr, "grid fixture boxes exist");
    check(second->rect.x > first->rect.x, "grid places items into columns");
    check(second->rect.x - first->rect.x >= first->rect.width + 8, "grid gap separates columns");
    check(wide->rect.width > first->rect.width * 2, "grid-column span increases item width");
    check(media->rect.height > 0 && media->rect.width > media->rect.height, "aspect ratio creates wide media box");
}

void layer_builder_respects_layer_and_display_command_budgets() {
    auto pipeline = build_pipeline(
        "<body><div class='a'>A</div><div class='b'>B</div><div class='c'>C</div></body>",
        "div { position: relative; z-index: 1; background: #ffffff; border: 1px solid #000000; }");

    VectorDiagnosticSink diagnostics;
    LayerTreeBuilderOptions options;
    options.max_layers = 2;
    options.max_display_commands = 4;
    options.diagnostics = &diagnostics;
    LayerTreeBuilder tight_layer_builder(options);
    auto tight_layer_tree = tight_layer_builder.build(*pipeline.layout_tree);
    DisplayList flattened = tight_layer_builder.flatten(*tight_layer_tree);

    check(count_layers(*tight_layer_tree) <= 2, "layer budget caps own layers");
    check(count_layer_display_commands(*tight_layer_tree) <= 4,
          "display command budget caps the retained layer tree globally");
    check(flattened.size() <= 4, "display command budget caps flattened output");
    check(has_diagnostic_code(diagnostics, "layer-limit"), "layer budget diagnostic is reported");
    check(has_diagnostic_code(diagnostics, "display-command-limit"), "display command budget diagnostic is reported");
}

void layer_tree_can_use_monotonic_arena() {
    auto pipeline = build_pipeline(
        "<body><section class='clip'><p>A</p></section><section class='fade'>B</section></body>",
        ".clip { overflow: hidden; height: 20px; } .fade { opacity: .5; background: #000000; }");

    LayerTreeBuilder layer_tree_builder;
    MonotonicArena arena(512);
    auto layer_tree = layer_tree_builder.build(*pipeline.layout_tree, arena);

    check(layer_tree != nullptr, "arena layer tree root exists");
    check(arena.used_bytes() > 0, "arena layer tree consumes arena storage");
    check(count_layers(*layer_tree) >= 2, "arena layer tree contains child layers");
    check(find_layer_with_reason(*layer_tree, LayerReasonOverflowClip) != nullptr,
          "arena layer tree keeps clip reason");
}

void box_shadow_emits_bounded_soft_shadow_command() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto pipeline = build_pipeline(
        "<body><section class='card'>Shadow</section><section class='card color-first'>Shadow</section></body>",
        ".card { background: #ffffff; box-shadow: 0 4px 12px rgba(0,0,0,0.08); }"
        ".color-first { box-shadow: rgba(0,0,0,0.12) 0 2px 8px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    int found_shadows = 0;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::BoxShadow && command.color.a > 0 && command.color.a < 80 &&
            command.stroke_width > 0 && command.gradient_stop_percent > 0) {
            ++found_shadows;
        }
    }
    check(found_shadows >= 2, "box-shadow emits bounded soft-shadow commands");
}

void colored_spread_box_shadow_keeps_author_color() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto pipeline = build_pipeline(
        "<body><section class='card'>Glow</section></body>",
        ".card { width: 64px; height: 32px; background: #121820; "
        "box-shadow: 1px 3px 8px 2px rgba(98,223,247,.28); }");

    LayerTreeBuilder layer_tree_builder;
    const DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::BoxShadow) {
            continue;
        }
        check(command.color.r == 98 && command.color.g == 223 && command.color.b == 247,
              "box-shadow command retains authored RGB color");
        check(command.color.a >= 70 && command.color.a <= 72,
              "box-shadow command retains authored alpha");
        check(command.rect.width > 64 && command.rect.height > 32,
              "positive box-shadow spread expands the bounded paint rect");
        return;
    }
    throw std::runtime_error("colored spread box-shadow command is missing");
}

void two_layer_background_emits_base_then_highlight() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto pipeline = build_pipeline(
        "<body><section class='card'>Glow</section></body>",
        ".card { width: 64px; height: 36px; background: "
        "radial-gradient(circle at 80% 20%, rgba(255,255,255,.24) 0%, transparent 100%), "
        "linear-gradient(to bottom right, #315a7a, #142331); }");
    LayerTreeBuilder layer_tree_builder;
    const DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    int linear_index = -1;
    int radial_index = -1;
    for (std::size_t index = 0; index < flattened.size(); ++index) {
        if (flattened[index].type == DisplayCommandType::LinearGradient) {
            linear_index = static_cast<int>(index);
        } else if (flattened[index].type == DisplayCommandType::RadialGradient) {
            radial_index = static_cast<int>(index);
        }
    }
    check(linear_index >= 0 && radial_index > linear_index,
          "two-layer background paints the base before its translucent highlight");
}

void box_shadow_none_and_large_shadow_are_diagnosed() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto none_pipeline = build_pipeline(
        "<body><section class='card'>No shadow</section></body>",
        ".card { background: #ffffff; box-shadow: 0 4px 12px rgba(0,0,0,0.08); box-shadow: none; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList none_flattened = layer_tree_builder.flatten(*none_pipeline.layer_tree);
    int shadow_commands = 0;
    for (const DisplayCommand& command : none_flattened) {
        if (command.type == DisplayCommandType::BoxShadow) {
            ++shadow_commands;
        }
    }
    check(shadow_commands == 0, "box-shadow:none suppresses soft shadow command");

    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='card'>Large</section></body>");
    Stylesheet stylesheet = css_parser.parse(
        ".card { width: 320px; height: 240px; box-shadow: 0 0 48px rgba(0,0,0,0.16); }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 360);

    VectorDiagnosticSink diagnostics;
    LayerTreeBuilderOptions options;
    options.diagnostics = &diagnostics;
    LayerTreeBuilder diagnostic_builder(options);
    auto layer_tree = diagnostic_builder.build(*layout_tree);
    (void)layer_tree;

    check(has_diagnostic_code(diagnostics, "layer-box-shadow-area-budget"),
          "large box-shadow emits area budget diagnostic");
}

void list_markers_and_generated_counters_emit_text() {
    auto pipeline = build_pipeline(
        "<body><ol class='custom'><li>Alpha</li><li>Beta<ul><li>Nested</li></ul></li></ol></body>",
        ".custom { list-style: none; }"
        ".custom > li { padding-left: 20px; position: relative; }"
        ".custom > li::before { content: counter(list-num) \".\"; color: #2b6cb0; font-weight: 600; left: 0; }"
        ".custom ul { list-style: disc; margin-left: 16px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_counter = false;
    bool found_disc = false;
    bool found_bold_text = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::Text) {
            continue;
        }
        if (command.text == "1." && command.color.b == 0xb0) {
            found_counter = true;
        }
        if (command.text == "*") {
            found_disc = true;
        }
        if (command.text == "1." && command.font_weight >= 600) {
            found_bold_text = true;
        }
    }
    check(found_counter, "generated counter marker is painted");
    check(found_disc, "nested native disc marker is painted");
    check(found_bold_text, "marker font-weight reaches display command");
}

void generated_after_and_percentage_radius_emit_commands() {
    auto pipeline = build_pipeline(
        "<body><section class='dial'><span>76</span></section></body>",
        ".dial { width: 80px; height: 80px; background: #102030; border-radius: 50%; }"
        ".dial::after { content: \"%\"; color: #22cc88; font-weight: 700; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_round_fill = false;
    bool found_after = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.border_radius == 40) {
            found_round_fill = true;
        }
        if (command.type == DisplayCommandType::Text && command.text == "%" &&
            command.text_align == TextCommandAlign::End && command.font_weight >= 700) {
            found_after = true;
        }
    }
    check(found_round_fill, "percentage border-radius resolves against box");
    check(found_after, "generated after content is painted after children");
}

void conic_gradient_background_emits_progress_command() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto pipeline = build_pipeline(
        "<body><section class='ring'></section></body>",
        ".ring { width: 64px; height: 64px; border-radius: 50%; "
        "background: conic-gradient(#22cc88 0% 75%, rgba(16,32,48,.35) 75% 100%); }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_conic = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::ConicGradient &&
            command.gradient_stop_percent == 75 &&
            command.border_radius == 32) {
            found_conic = true;
            break;
        }
    }
    check(found_conic, "conic-gradient emits bounded progress display command");
}

void radial_gradient_background_emits_center_circle_command() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto pipeline = build_pipeline(
        "<body><section class='gel'></section></body>",
        ".gel { width: 80px; height: 48px; border-radius: 18px; "
        "background: radial-gradient(circle, rgba(240,255,252,.86) 0%, rgba(36,126,160,.18) 100%); }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_radial = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::RadialGradient &&
            command.border_radius == 18 &&
            command.color.r == 240 &&
            command.color2.b == 160) {
            found_radial = true;
            break;
        }
    }
    check(found_radial, "radial-gradient emits center-circle display command");
}

void large_conic_gradient_reports_area_budget_diagnostic() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='ring'></section></body>");
    Stylesheet stylesheet = css_parser.parse(
        ".ring { width: 400px; height: 400px; "
        "background: conic-gradient(#22cc88 0% 75%, rgba(16,32,48,.35) 75% 100%); }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 480);

    VectorDiagnosticSink diagnostics;
    LayerTreeBuilderOptions options;
    options.diagnostics = &diagnostics;
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    (void)layer_tree;

    check(has_diagnostic_code(diagnostics, "layer-conic-gradient-area-budget"),
          "large conic-gradient emits area budget diagnostic");
}

void large_radial_gradient_reports_area_budget_diagnostic() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='gel'></section></body>");
    Stylesheet stylesheet = css_parser.parse(
        ".gel { width: 240px; height: 180px; "
        "background: radial-gradient(circle, rgba(255,255,255,.6) 0%, rgba(40,120,160,.2) 100%); }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 320);

    VectorDiagnosticSink diagnostics;
    LayerTreeBuilderOptions options;
    options.diagnostics = &diagnostics;
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    (void)layer_tree;

    check(has_diagnostic_code(diagnostics, "layer-radial-gradient-area-budget"),
          "large radial-gradient emits area budget diagnostic");
}

void fixed_grid_places_description_list_in_columns() {
    auto pipeline = build_pipeline(
        "<body><dl><dt>Name</dt><dd>JellyFrame</dd><dt>Mode</dt><dd>Embedded</dd></dl></body>",
        "dl { display: grid; grid-template-columns: 80px 1fr; gap: 4px; } dd { margin: 0; }");

    const LayoutBox* term = nullptr;
    const LayoutBox* description = nullptr;
    const auto find_text_parent = [&](const LayoutBox& root, const char* text, const LayoutBox*& output, const auto& self) -> void {
        if (root.node != nullptr && root.node->type == NodeType::Text && root.node->text == text) {
            output = &root;
            return;
        }
        for (const auto& child : root.children) {
            self(*child, text, output, self);
            if (output != nullptr) {
                return;
            }
        }
    };
    find_text_parent(*pipeline.layout_tree, "Name", term, find_text_parent);
    find_text_parent(*pipeline.layout_tree, "JellyFrame", description, find_text_parent);
    check(term != nullptr && description != nullptr, "description list text boxes exist");
    check(description->rect.x > term->rect.x + 70, "fixed grid places dd in second column");
}

void flex_order_changes_same_stack_paint_order() {
    auto pipeline = build_pipeline(
        "<body><main><div class='late'></div><div class='early'></div></main></body>",
        "body { margin: 0; } main { display: flex; }"
        "div { width: 20px; height: 20px; }"
        ".late { order: 2; background: #ff0000; }"
        ".early { order: -1; background: #0000ff; }");

    LayerTreeBuilder builder;
    const DisplayList flattened = builder.flatten(*pipeline.layer_tree);
    std::vector<Color> fills;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.rect.width == 20 && command.rect.height == 20) {
            fills.push_back(command.color);
        }
    }
    check(fills.size() == 2, "flex order paint fixture emits both child fills");
    check(fills[0].b == 255 && fills[1].r == 255,
          "flex order changes same-stack paint order with the layout order");
}

void unbreakable_symbol_stays_single_line() {
    auto pipeline = build_pipeline("<body><button class='delete'>&#215;</button></body>",
                                   ".delete { width: 34px; height: 34px; font-size: 24px;"
                                   "line-height: 34px; text-align: center; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Text && command.text == "\xC3\x97") {
            check(command.rect.height == 34, "single unbreakable symbol does not wrap taller than the control");
            check(command.text_single_line, "single unbreakable symbol is marked single-line");
            return;
        }
    }
    check(false, "symbol text command exists");
}

void text_overflow_ellipsis_truncates_painted_text() {
    auto pipeline = build_pipeline(
        "<body><p>SuperLongStatusLabelWithoutBreaks</p></body>",
        "p { width: 42px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }");

    LayerTreeBuilder layer_tree_builder;
    const DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::Text) {
            continue;
        }
        check(command.text != "SuperLongStatusLabelWithoutBreaks",
              "ellipsis does not emit the unclipped source text");
        check(command.text.size() >= 3 && command.text.compare(command.text.size() - 3, 3, "...") == 0,
              "ellipsis paints an ASCII fallback marker when text exceeds the box");
        return;
    }
    check(false, "ellipsis fixture emits a text command");
}

void utf8_text_overflow_ellipsis_keeps_scalar_boundaries() {
    const std::string source = "å¤©æ°é¢æ¥çé¢";
    const std::string html = std::string("<body><p>") + source + "</p></body>";
    auto pipeline = build_pipeline(
        html.c_str(),
        "p { width: 72px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }");

    std::vector<std::size_t> scalar_boundaries;
    for (std::size_t index = 0; index < source.size();) {
        consume_utf8_codepoint(source, index);
        scalar_boundaries.push_back(index);
    }

    LayerTreeBuilder layer_tree_builder;
    const DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::Text) {
            continue;
        }
        check(command.text.size() >= 3 && command.text.compare(command.text.size() - 3, 3, "...") == 0,
              "utf8 ellipsis paints an ASCII fallback marker");
        const std::string prefix = command.text.substr(0, command.text.size() - 3);
        check(source.compare(0, prefix.size(), prefix) == 0,
              "utf8 ellipsis keeps the original byte prefix");
        check(std::find(scalar_boundaries.begin(), scalar_boundaries.end(), prefix.size()) != scalar_boundaries.end(),
              "utf8 ellipsis truncates only at scalar boundaries");
        check(command.text != source, "utf8 ellipsis truncates long text");
        return;
    }
    check(false, "utf8 ellipsis fixture emits a text command");
}

void text_transform_paints_transformed_text() {
    auto pipeline = build_pipeline("<body><p>hello ui</p><p class='cap'>jelly-frame kit</p></body>",
                                   "p { text-transform: uppercase; }"
                                   ".cap { text-transform: capitalize; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_upper = false;
    bool found_capitalized = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type != DisplayCommandType::Text) {
            continue;
        }
        found_upper = found_upper || command.text == "HELLO UI";
        found_capitalized = found_capitalized || command.text == "Jelly-Frame Kit";
    }
    check(found_upper, "text-transform uppercase reaches paint commands");
    check(found_capitalized, "text-transform capitalize reaches paint commands");
}

void grid_item_auto_width_reflows_centered_text() {
    auto pipeline = build_pipeline(
        "<body><section class='grid'><button>7</button><button>8</button></section></body>",
        ".grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 6px; width: 180px; }"
        "button { width: auto; height: 40px; border: 0; padding: 0; text-align: center; font-size: 24px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    Rect first_button{};
    Rect first_text{};
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.rect.width > 70 &&
            command.color.r == 243 && command.color.g == 244 && command.color.b == 246 &&
            first_button.width == 0) {
            first_button = command.rect;
        }
        if (command.type == DisplayCommandType::Text && command.text == "7") {
            first_text = command.rect;
        }
    }
    check(first_button.width > 70, "grid button stretches to track width");
    check(first_text.x > first_button.x + 20, "grid button text is centered after stretch");
}

void text_input_respects_text_align() {
    auto pipeline = build_pipeline("<body><input class='display' value='42'></body>",
                                   ".display { width: 120px; height: 32px; padding: 0; "
                                   "border: 0; text-align: right; font-size: 20px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Text && command.text == "42") {
            check(command.text_align == TextCommandAlign::End, "input text follows text-align right");
            return;
        }
    }
    check(false, "input text command exists");
}

void flex_wrap_places_items_on_new_lines() {
    auto pipeline = build_pipeline(
        "<body><section class='row'><div class='item a'>A</div><div class='item b'>B</div><div class='item c'>C</div></section></body>",
        ".row { display: flex; flex-wrap: wrap; gap: 4px; }"
        ".item { width: 100px; height: 20px; }");

    const LayoutBox* first = find_layout_by_class(*pipeline.layout_tree, "a");
    const LayoutBox* third = find_layout_by_class(*pipeline.layout_tree, "c");
    check(first != nullptr && third != nullptr, "flex wrap fixture boxes exist");
    check(third->rect.y > first->rect.y, "flex-wrap places overflowing item on next line");
}

struct ImageResolveContext {
    std::string url;
    std::uint32_t handle = 0;
};

bool resolve_test_image(const Node& node,
                        ImageResolveKind kind,
                        std::uint16_t,
                        std::uint32_t& handle,
                        void* raw_context) {
    auto* context = static_cast<ImageResolveContext*>(raw_context);
    if (context == nullptr || kind != ImageResolveKind::Content || node.attribute("src") != context->url) {
        return false;
    }
    handle = context->handle;
    return true;
}

bool resolve_test_canvas(const Node& node,
                         ImageResolveKind kind,
                         std::uint16_t,
                         std::uint32_t& handle,
                         void* raw_context) {
    auto* expected_handle = static_cast<std::uint32_t*>(raw_context);
    if (expected_handle == nullptr || kind != ImageResolveKind::Content ||
        node.type != NodeType::Element || node.tag_name != "canvas") {
        return false;
    }
    handle = *expected_handle;
    return true;
}

struct BackgroundImageResolveContext {
    std::uint16_t resource_id = 0;
    std::uint32_t handle = 0;
};

bool resolve_test_background_image(const Node&,
                                   ImageResolveKind kind,
                                   std::uint16_t resource_id,
                                   std::uint32_t& handle,
                                   void* raw_context) {
    auto* context = static_cast<BackgroundImageResolveContext*>(raw_context);
    if (context == nullptr || kind != ImageResolveKind::Background ||
        resource_id == 0 || resource_id != context->resource_id) {
        return false;
    }
    handle = context->handle;
    return true;
}

void package_background_image_emits_image_display_command_when_surface_resolves() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='cover'></section></body>");
    StyleResolver resolver(css_parser.parse(
        ".cover { width: 48px; height: 32px; border-radius: 8px; background-color: #112233; "
        "background-image: url('/assets/cover.bmp'); background-size: cover; background-position: right top; "
        "background-repeat: no-repeat; image-rendering: crisp-edges; }"));
    const std::uint16_t resource_id = resolver.background_image_resource_id_for("/assets/cover.bmp");
    check(resource_id != 0 && resolver.background_image_resource_url(resource_id) != nullptr,
          "package background image owns a stylesheet-local resource id");

    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    BackgroundImageResolveContext context{resource_id, 91};
    LayerTreeBuilderOptions options;
    options.image_resolver = ImageHandleResolver{resolve_test_background_image, &context};
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

    bool found_image = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Image && command.image_handle == context.handle) {
            found_image = true;
            check(command.rect.width == 48 && command.rect.height == 32,
                  "background image fills the documented element paint area");
            check(has_corner_radius(command.border_radius),
                  "background image inherits the element rounded paint clip");
            check(command.object_fit == ObjectFit::Cover && command.object_position.x_percent == 100 &&
                      command.object_position.y_percent == 0 && command.image_rendering == ImageRendering::CrispEdges,
                  "background image command carries bounded size, position and sampling controls");
        }
    }
    check(found_image, "resolved package background image emits image display command");
}

void image_element_emits_image_display_command_when_surface_resolves() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><img src='/debug/icon.raw'></body>");
    Stylesheet stylesheet = css_parser.parse(
        "img { width: 32px; height: 24px; border-radius: 8px; object-fit: cover; object-position: right top; image-rendering: crisp-edges; }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    ImageResolveContext context{"/debug/icon.raw", 77};
    LayerTreeBuilderOptions options;
    options.image_resolver = ImageHandleResolver{resolve_test_image, &context};
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

    bool found_image = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Image) {
            found_image = true;
            check(command.image_handle == 77, "image command carries resolved handle");
            check(command.rect.width == 32 && command.rect.height == 24, "image command uses content rect");
            check(command.object_fit == ObjectFit::Cover, "image command carries object-fit");
            check(command.object_position.x_percent == 100 && command.object_position.y_percent == 0,
                  "image command carries object-position");
            check(command.image_rendering == ImageRendering::CrispEdges,
                  "image command carries image-rendering");
            check(has_corner_radius(command.border_radius),
                  "image command carries the declared rounded paint clip");
        }
    }
    check(found_image, "resolved img emits image command");
}

void canvas_element_emits_image_display_command_when_surface_resolves() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><canvas width='32' height='24'></canvas></body>");
    Stylesheet stylesheet = css_parser.parse(
        "canvas { width: 32px; height: 24px; object-fit: contain; image-rendering: pixelated; }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    std::uint32_t handle = 0x80000011U;
    LayerTreeBuilderOptions options;
    options.image_resolver = ImageHandleResolver{resolve_test_canvas, &handle};
    LayerTreeBuilder layer_tree_builder(options);
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    DisplayList flattened = layer_tree_builder.flatten(*layer_tree);

    bool found_canvas = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::Image) {
            found_canvas = true;
            check(command.image_handle == handle, "canvas image command carries resolved handle");
            check(command.rect.width == 32 && command.rect.height == 24, "canvas command uses content rect");
            check(command.object_fit == ObjectFit::Contain, "canvas command carries object-fit");
            check(command.image_rendering == ImageRendering::Pixelated, "canvas command carries image-rendering");
        }
    }
    check(found_canvas, "resolved canvas emits image command");
}

} // namespace

int main() {
    try {
        overflow_hidden_creates_clip_layer();
        overflow_y_auto_creates_vertical_scroll_clip_layer();
        visibility_preserves_layout_and_suppresses_hidden_paint_and_hit_testing();
        text_spacing_and_anywhere_wrap_emit_only_declared_extra_commands();
        scroll_container_offsets_descendant_paint();
        scroll_container_keeps_absolute_sibling_navigation_fixed();
        scroll_indicator_is_opt_in_overlay();
        opacity_layer_flattens_alpha();
        opacity_overrides_reuse_existing_composited_layers();
        flatten_into_reuses_storage_and_matches_flatten();
        rounded_equal_border_emits_stroke_command();
        linear_gradient_background_emits_gradient_command();
        outline_and_text_shadow_emit_paint_commands();
        outline_offset_expands_focus_stroke_without_affecting_layout();
        z_index_orders_child_layers();
        progress_and_meter_emit_value_fill();
        inline_mark_background_shrinks_to_text();
        inline_run_flows_horizontally();
        centered_inline_text_aligns_in_parent();
        button_inline_block_shrink_wraps_text();
        select_does_not_paint_option_list_inline();
        form_paint_only_state_changes_rebuild_visible_commands();
        select_popup_paints_above_document_and_hit_tests_to_owner();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        grid_auto_fit_gap_span_and_aspect_ratio_layout();
#endif
        box_shadow_emits_bounded_soft_shadow_command();
        colored_spread_box_shadow_keeps_author_color();
        two_layer_background_emits_base_then_highlight();
        box_shadow_none_and_large_shadow_are_diagnosed();
        list_markers_and_generated_counters_emit_text();
        generated_after_and_percentage_radius_emit_commands();
        package_background_image_emits_image_display_command_when_surface_resolves();
        conic_gradient_background_emits_progress_command();
        radial_gradient_background_emits_center_circle_command();
        large_conic_gradient_reports_area_budget_diagnostic();
        large_radial_gradient_reports_area_budget_diagnostic();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        fixed_grid_places_description_list_in_columns();
        flex_order_changes_same_stack_paint_order();
#endif
        unbreakable_symbol_stays_single_line();
        text_overflow_ellipsis_truncates_painted_text();
        utf8_text_overflow_ellipsis_keeps_scalar_boundaries();
        text_transform_paints_transformed_text();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        grid_item_auto_width_reflows_centered_text();
#endif
        text_input_respects_text_align();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        flex_wrap_places_items_on_new_lines();
#endif
        image_element_emits_image_display_command_when_surface_resolves();
        canvas_element_emits_image_display_command_when_surface_resolves();
        layer_builder_respects_layer_and_display_command_budgets();
        layer_tree_can_use_monotonic_arena();
    } catch (const std::exception& error) {
        std::cerr << "layer tree test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layer tree tests passed\n";
    return 0;
}
