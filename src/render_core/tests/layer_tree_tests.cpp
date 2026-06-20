#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

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

void overflow_hidden_creates_clip_layer() {
    auto pipeline = build_pipeline("<body><section class='clip'><p>Visible</p></section></body>",
                                   ".clip { overflow: hidden; height: 20px; background: #ffffff; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOverflowClip);
    check(layer != nullptr, "overflow layer exists");
    check(layer->type == LayerType::Clip, "overflow layer is clip layer");
    check(layer->has_clip, "overflow layer has clip");
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
    check(found_gradient, "linear-gradient background emits gradient command");
}

void outline_and_text_shadow_emit_paint_commands() {
    auto pipeline = build_pipeline("<body><button class='cta'>Glow</button></body>",
                                   ".cta { display: block; width: 80px; height: 32px; "
                                   "outline: 2px solid rgba(255,255,255,0.5); "
                                   "text-shadow: 1px 1px 2px rgba(0,0,0,0.35); }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_outline = false;
    int glow_text_commands = 0;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::StrokeRect && command.stroke_width == 2 &&
            command.color.a >= 126 && command.color.a <= 128) {
            found_outline = true;
        }
        if (command.type == DisplayCommandType::Text && command.text == "Glow") {
            ++glow_text_commands;
        }
    }
    check(found_outline, "outline emits stroke command");
    check(glow_text_commands >= 2, "text-shadow emits shadow text before main text");
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

void box_shadow_emits_cheap_translucent_fill() {
    auto pipeline = build_pipeline(
        "<body><section class='card'>Shadow</section><section class='card color-first'>Shadow</section></body>",
        ".card { background: #ffffff; box-shadow: 0 4px 12px rgba(0,0,0,0.08); }"
        ".color-first { box-shadow: rgba(0,0,0,0.12) 0 2px 8px; }");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    int found_shadows = 0;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.color.a > 0 && command.color.a < 80) {
            ++found_shadows;
        }
    }
    check(found_shadows >= 2, "box-shadow emits approximate translucent fills");
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

bool resolve_test_image(const Node& node, std::uint32_t& handle, void* raw_context) {
    auto* context = static_cast<ImageResolveContext*>(raw_context);
    if (context == nullptr || node.attribute("src") != context->url) {
        return false;
    }
    handle = context->handle;
    return true;
}

void image_element_emits_image_display_command_when_surface_resolves() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><img src='app://icon.raw'></body>");
    Stylesheet stylesheet = css_parser.parse("img { width: 32px; height: 24px; object-fit: cover; }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    ImageResolveContext context{"app://icon.raw", 77};
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
        }
    }
    check(found_image, "resolved img emits image command");
}

} // namespace

int main() {
    try {
        overflow_hidden_creates_clip_layer();
        opacity_layer_flattens_alpha();
        rounded_equal_border_emits_stroke_command();
        linear_gradient_background_emits_gradient_command();
        outline_and_text_shadow_emit_paint_commands();
        z_index_orders_child_layers();
        progress_and_meter_emit_value_fill();
        inline_mark_background_shrinks_to_text();
        inline_run_flows_horizontally();
        centered_inline_text_aligns_in_parent();
        button_inline_block_shrink_wraps_text();
        select_does_not_paint_option_list_inline();
        grid_auto_fit_gap_span_and_aspect_ratio_layout();
        box_shadow_emits_cheap_translucent_fill();
        list_markers_and_generated_counters_emit_text();
        fixed_grid_places_description_list_in_columns();
        unbreakable_symbol_stays_single_line();
        grid_item_auto_width_reflows_centered_text();
        text_input_respects_text_align();
        flex_wrap_places_items_on_new_lines();
        image_element_emits_image_display_command_when_surface_resolves();
        layer_builder_respects_layer_and_display_command_budgets();
        layer_tree_can_use_monotonic_arena();
    } catch (const std::exception& error) {
        std::cerr << "layer tree test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layer tree tests passed\n";
    return 0;
}
