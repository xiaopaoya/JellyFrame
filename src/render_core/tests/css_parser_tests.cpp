#include "render_core/css_parser.h"
#include "render_core/document_style.h"
#include "render_core/feature_config.h"
#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/render_tree.h"
#include "render_core/style.h"
#include "render_core/text_backend.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Stylesheet parse(const std::string& source) {
    CssParser parser;
    return parser.parse(source);
}

Node* find_first_by_tag(Node& node, const std::string& tag_name);

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

void parses_comments_strings_and_functions() {
    const Stylesheet stylesheet = parse(
        "/* reset */"
        ".card {"
        "  color: #111;"
        "  background: url(\"data:image/svg+xml;utf8,<svg>{}</svg>\");"
        "  padding: calc(4px + 2px);"
        "}"
        ".next { color: blue; }");

    check(stylesheet.size() == 2, "rule count after string/function parsing");
    check(stylesheet[0].selector == ".card", "first selector");
    check(stylesheet[0].declarations.size() == 3, "declaration count");
    check(stylesheet[0].declarations[1].property == "background", "background property");
    check(stylesheet[1].selector == ".next", "next selector survives");
}

void splits_selector_lists() {
    const Stylesheet stylesheet = parse("h1, h2, .title { color: red; }");
    check(stylesheet.size() == 3, "selector list split");
    check(stylesheet[0].selector == "h1", "h1 selector");
    check(stylesheet[1].selector == "h2", "h2 selector");
    check(stylesheet[2].selector == ".title", "class selector");
}

void expands_single_level_explicit_css_nesting() {
    const Stylesheet stylesheet = parse(
        ".card { color: #111111; &:hover { color: #ff0000; } & .label { color: #0000ff; } }");
    check(stylesheet.size() == 3, "single-level nesting expands parent declarations and both nested rules");
    check(stylesheet[0].selector == ".card", "nested source keeps parent declaration order");
    check(stylesheet[1].selector == ".card:hover", "nested pseudo selector substitutes explicit parent marker");
    check(stylesheet[2].selector == ".card .label", "nested descendant selector substitutes explicit parent marker");

    auto card = make_element("div");
    card->attributes["class"] = "card";
    auto label = make_element("span");
    label->attributes["class"] = "label";
    Node& label_node = card->append_child(std::move(label));
    StyleResolver resolver(stylesheet, StyleResolverOptions{128, card.get(), nullptr, nullptr, nullptr});
    const Style hovered_card = resolver.resolve(*card);
    const Style label_style = resolver.resolve(label_node);
    check(hovered_card.color.r == 255 && hovered_card.color.g == 0,
          "expanded hover selector enters the existing interaction selector path");
    check(label_style.color.b == 255, "expanded descendant selector enters the existing selector path");
}

void rejects_nested_css_outside_explicit_single_level_subset() {
    VectorDiagnosticSink diagnostics;
    CssParser parser;
    CssParserOptions options;
    options.diagnostics = &diagnostics;
    const Stylesheet stylesheet = parser.parse(
        ".card { color: #111111; .label { color: #ff0000; } & .nested { & .deep { color: #0000ff; } } }",
        options);
    check(stylesheet.size() == 1 && stylesheet[0].selector == ".card",
          "unsupported nesting does not corrupt surrounding parent declarations");
    check(has_diagnostic_code(diagnostics, "css-nesting-skipped"),
          "unsupported nesting reports a stable diagnostic");
}

void nesting_preprocessor_respects_depth_and_output_budgets() {
    CssParser parser;
    VectorDiagnosticSink diagnostics;
    CssParserOptions options;
    options.diagnostics = &diagnostics;
    options.max_nesting_depth = 2;
    options.max_nesting_expansion_bytes = 96;
    const Stylesheet stylesheet = parser.parse(
        ".before { color: #123456; }"
        "@media screen { @media screen { @media screen { .card { &:hover { color: #ff0000; } } } } }",
        options);

    check(!stylesheet.empty() && stylesheet[0].selector == ".before",
          "nesting budget failure retains already expanded safe rules");
    check(has_diagnostic_code(diagnostics, "css-nesting-expansion-limit"),
          "nesting preprocessor reports bounded depth or output expansion");
}

void skips_enhancement_blocks_without_corrupting_following_rules() {
    const Stylesheet stylesheet = parse(
        "@supports (color: oklch(50% 0.2 30)) { .modern { color: oklch(50% 0.2 30); } }"
        ".base { color: #333; }"
        "@media (max-width: 400px) { .narrow { color: red; } }"
        ".after { color: blue; }");

    check(stylesheet.size() == 3, "unsupported group rules skipped and supported media survives");
    check(stylesheet[0].selector == ".base", "base selector");
    check(stylesheet[1].selector == ".narrow", "matching conditional media selector");
    check(stylesheet[2].selector == ".after", "following selector");
}

void pipeline_diagnostics_report_css_and_style_degradation() {
    VectorDiagnosticSink diagnostics;
    CssParser parser;
    CssParserOptions css_options;
    css_options.diagnostics = &diagnostics;
    Stylesheet stylesheet = parser.parse(
        "@container card (min-width: 120px) { .card { color: red; } }"
        ".card:has(button) { color: blue; }"
        ".card { color: oklch(50% 0.2 30); backdrop-filter: blur(8px); }",
        css_options);

    auto node = make_element("div");
    node->attributes["class"] = "card";
    node->attributes["style"] = "color; background:";
    StyleResolverOptions style_options;
    style_options.diagnostics = &diagnostics;
    StyleResolver resolver(std::move(stylesheet), style_options);
    (void)resolver.resolve(*node);

    check(has_diagnostic_code(diagnostics, "css-at-rule-skipped"), "unsupported at-rule is reported");
    check(has_diagnostic_code(diagnostics, "css-selector-skipped"), "unsupported selector is reported");
    check(has_diagnostic_code(diagnostics, "style-declaration-ignored"), "unsupported value is reported");
    check(has_diagnostic_code(diagnostics, "style-property-unsupported"), "unsupported property is reported");
    check(has_diagnostic_code(diagnostics, "style-inline-declaration-malformed"), "bad inline style is reported");
}

void supports_queries_flatten_safe_declaration_subset() {
    const Stylesheet stylesheet = parse(
        "@supports (display: grid) { .grid { display: grid; } }"
        "@supports ((display: flex) and (gap: 8px)) { .flex { display: flex; gap: 8px; } }"
        "@supports (background: conic-gradient(#22cc88 0% 76%, rgba(16, 32, 48, .35) 76% 100%)) { .ring { width: 44px; } }"
        "@supports (background: radial-gradient(circle at 80% 20%, rgba(255,255,255,.2), transparent), linear-gradient(to bottom right, #315a7a, #142331)) { .gel { height: 40px; } }"
        "@supports (color: color-mix(in srgb, #ffffff 80%, #62dff7)) { .tint { color: #62dff7; } }"
        "@supports not (color: oklch(50% 0.2 30)) { .fallback { color: #123456; } }"
        "@supports ((display: grid) or (unknown-prop: 1px)) { .either { display: block; } }"
        "@supports ((display: grid) and (gap: 8px) or (color: red)) { .mixed { color: red; } }"
        "@supports selector(:has(*)) { .has { color: red; } }");

    check(stylesheet.size() == 7, "supported @supports subset flattens matching safe blocks");
    check(stylesheet[0].selector == ".grid", "display grid supports selector");
    check(stylesheet[1].selector == ".flex", "and supports selector");
    check(stylesheet[2].selector == ".ring", "conic background supports selector");
    check(stylesheet[3].selector == ".gel", "two-layer background supports selector");
    check(stylesheet[4].selector == ".tint", "color-mix supports selector");
    check(stylesheet[5].selector == ".fallback", "not unsupported supports selector");
    check(stylesheet[6].selector == ".either", "or supports selector");
}

void supports_queries_apply_representative_supported_properties() {
    const Stylesheet stylesheet = parse(
        "@supports (display: flex) { .probe { display: flex; flex-wrap: wrap; gap: 6px; } }"
        "@supports (flex-direction: column) { .probe { flex-direction: column; } }"
        "@supports (order: -2) { .probe { order: -2; } }"
        "@supports (align-self: flex-end) { .probe { align-self: flex-end; } }"
        "@supports (align-content: space-between) { .probe { align-content: space-between; } }"
        "@supports (grid-template-columns: repeat(2, 1fr)) { .probe { display: grid; grid-template-columns: repeat(2, 1fr); } }"
        "@supports (object-fit: cover) { .probe { object-fit: cover; image-rendering: pixelated; } }"
        "@supports (border-right: 2px solid #123456) { .probe { border-right: 2px solid #123456; } }"
        "@supports (text-overflow: ellipsis) { .probe { white-space: nowrap; text-overflow: ellipsis; } }"
        "@supports (visibility: hidden) { .probe { visibility: hidden; } }"
        "@supports (visibility: collapse) { .probe { color: #ff0000; } }"
        "@supports (justify-content: space-evenly) { .probe { justify-content: space-evenly; } }"
        "@supports (overflow-y: auto) { .probe { overflow-y: auto; } }"
        "@supports (overflow-y: hidden) { .probe { color: #ff0000; } }"
        "@supports (background-image: radial-gradient(#fff, #000)) { .probe { background-image: radial-gradient(#fff, #000); } }"
        "@supports (unknown-property: 1px) { .probe { color: #ff0000; } }");

    auto element = make_element("div");
    element->attributes["class"] = "probe";
    StyleResolver resolver(stylesheet);
    const Style style = resolver.resolve(*element);

    check(style.display == Display::Grid, "supported grid declaration survives @supports and applies");
    check(style.grid_template_column_count == 2, "grid-template-columns applies after @supports");
    check(style.flex_wrap, "flex-wrap applies after @supports");
    check(style.flex_direction == FlexDirection::Column, "flex-direction column applies after @supports");
    check(style.flex_order == -2, "integer flex order applies after @supports");
    check(style.align_self == AlignItems::End, "align-self applies after @supports");
    check(style.align_content == JustifyContent::SpaceBetween, "align-content applies after @supports");
    check(style.column_gap == 6 && style.row_gap == 6, "gap applies after @supports");
    check(style.object_fit == ObjectFit::Cover, "object-fit applies after @supports");
    check(style.image_rendering == ImageRendering::Pixelated, "image-rendering applies after @supports");
    check(style.border_width.right == 2, "border-right applies after @supports");
    check(style.white_space_nowrap && style.text_overflow_ellipsis, "text overflow controls apply after @supports");
    check(style.visibility_hidden, "visibility hidden applies after @supports");
    check(style.justify_content == JustifyContent::SpaceEvenly,
          "space-evenly justification applies after @supports");
    check(style.overflow == "auto", "overflow-y auto applies after @supports");
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    check(style.background_paint == BackgroundPaintKind::RadialGradient,
          "background-image radial-gradient applies after @supports");
#else
    check(style.background_paint == BackgroundPaintKind::Solid,
          "background-image radial-gradient falls back when modern paint is disabled");
#endif
    check(style.color.r == 0 && style.color.g == 0 && style.color.b == 0, "unsupported @supports block is not applied");
}

#if !JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
void disabled_flex_grid_queries_and_declarations_fall_back() {
    const Stylesheet stylesheet = parse(
        "@supports (display: grid) { .grid { display: grid; gap: 4px; } }"
        ".fallback { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }"
        ".plain { display: block; }");
    check(stylesheet.size() == 2, "disabled flex-grid removes the conditional grid rule");
    check(stylesheet[0].selector == ".fallback" && stylesheet[1].selector == ".plain",
          "disabled flex-grid preserves surrounding rules");

    auto element = make_element("div");
    element->attributes["class"] = "fallback";
    StyleResolver resolver(stylesheet);
    const Style style = resolver.resolve(*element);
    check(style.display != Display::Flex && style.display != Display::Grid,
          "disabled flex-grid never leaves a flex/grid display value");
    check(style.column_gap == 0 && style.row_gap == 0,
          "disabled flex-grid does not retain layout-family gaps");
}
#endif

void text_wrap_alias_reuses_bounded_white_space_behavior() {
    auto label = make_element("span");
    label->attributes["class"] = "label";
    StyleResolver resolver(parse(
        ".label { white-space: nowrap; }"
        ".label { text-wrap: wrap; }"
        "@supports (text-wrap: nowrap) { .label { text-wrap: nowrap; } }"));

    const Style style = resolver.resolve(*label);
    check(style.white_space_nowrap,
          "text-wrap nowrap participates in the white-space cascade and @supports subset");
}

void style_struct_size_has_embedded_guardrail() {
#if !defined(JELLYFRAME_TEST_CONFIG_DEBUG)
    check(sizeof(Style) <= 776, "Style should stay within the embedded size guardrail");
    check(sizeof(DisplayCommand) <= 160, "DisplayCommand should stay compact for display-list reuse");
#endif
    const Style idle_style;
    check(idle_style.transitions.empty() && idle_style.transitions.capacity() == 0,
          "idle styles do not reserve transition storage");
    check(idle_style.animations.empty() && idle_style.animations.capacity() == 0,
          "idle styles do not reserve animation storage");
}

void flattens_layers_and_plain_media() {
    const Stylesheet stylesheet = parse(
        "@layer components { .button { color: red; } }"
        "@media screen { .screen { color: blue; } }");

    check(stylesheet.size() == 2, "layer and plain media flattened");
    check(stylesheet[0].selector == ".button", "layer selector");
    check(stylesheet[1].selector == ".screen", "screen media selector");
}

void conditional_media_queries_respect_viewport() {
    CssParser parser;
    CssParserOptions options;
    options.media_viewport_width = 360;
    options.media_viewport_height = 240;
    const Stylesheet stylesheet = parser.parse(
        "@media (min-width: 320px) and (max-width: 400px) { .compact { color: red; } }"
        "@media screen and (max-height: 240px) { .short { color: blue; } }"
        "@media (min-height: 241px) { .tall { color: green; } }"
        "@media print { .print { color: black; } }"
        "@media print, screen and (max-width: 360px) { .listed { color: purple; } }"
        "@media screen and (width <= 360px) { .range { color: orange; } }",
        options);

    check(stylesheet.size() == 3, "conditional media subset applies only matching supported queries");
    check(stylesheet[0].selector == ".compact", "width media selector");
    check(stylesheet[1].selector == ".short", "height media selector");
    check(stylesheet[2].selector == ".listed", "comma media selector");
}

void preserves_declaration_fallback_order() {
    const Stylesheet stylesheet = parse(".x { color: #123456; color: oklch(50% 0.2 30); }");
    check(stylesheet.size() == 1, "fallback rule count");
    check(stylesheet[0].declarations.size() == 2, "fallback declaration count");

    auto element = make_element("div");
    element->attributes["class"] = "x";
    StyleResolver resolver(stylesheet);
    const Style style = resolver.resolve(*element);
    check(style.color.r == 0x12 && style.color.g == 0x34 && style.color.b == 0x56, "unsupported value keeps fallback");
}

void resolves_simple_css_custom_properties() {
    HtmlParser html_parser;
    auto document = html_parser.parse(
        "<html><body><main class='theme'><button id='action' class='primary' "
        "style='--inline-bg:#123456;background:var(--inline-bg)'>Go</button></main></body></html>");

    Node* main = find_first_by_tag(*document, "main");
    Node* button = find_first_by_tag(*document, "button");
    check(main != nullptr && button != nullptr, "custom property fixture nodes exist");

    StyleResolver resolver(parse(
        ":root { --accent: #2563eb; --panel: #f8fafc; }"
        ".theme { --accent: #dc2626; }"
        ".primary { color: #111111; color: var(--accent); border-color: var(--missing, #334455); }"
        ".primary { width: var(--missing-width); }"));

    const Style main_style = resolver.resolve(*main);
    const Style button_style = resolver.resolve(*button);

    check(main_style.color.r == 0 && main_style.color.g == 0 && main_style.color.b == 0,
          "custom property declarations do not style directly");
    check(button_style.color.r == 0xdc && button_style.color.g == 0x26 && button_style.color.b == 0x26,
          "inherited custom property resolves");
    check(button_style.background_color.r == 0x12 && button_style.background_color.g == 0x34 &&
              button_style.background_color.b == 0x56,
          "inline custom property resolves");
    check(button_style.border_color.r == 0x33 && button_style.border_color.g == 0x44 &&
              button_style.border_color.b == 0x55,
          "var fallback resolves");
    check(button_style.width == -1, "unresolved var keeps property fallback");

    StyleResolveContext custom_context;
    const Style contextual_button_style = resolver.resolve(*button, custom_context);
    check(contextual_button_style.color.r == 0xdc && contextual_button_style.color.g == 0x26 &&
              contextual_button_style.color.b == 0x26,
          "contextual custom property resolution keeps inherited value");
    check(custom_context.custom_property_scopes.size() == 3,
          "contextual custom properties allocate scopes only for root, theme and inline overrides");
    check(custom_context.matched_rule_cache.size() >= custom_context.custom_property_scopes.size(),
          "contextual custom properties reuse matched selectors during style application");

    auto plain_document = html_parser.parse("<body><main><button>Go</button></main></body>");
    Node* plain_button = find_first_by_tag(*plain_document, "button");
    StyleResolver plain_resolver(parse("button { color: #123456; }"));
    StyleResolveContext plain_context;
    const Style plain_style = plain_resolver.resolve(*plain_button, plain_context);
    check(plain_style.color.r == 0x12 && plain_style.color.g == 0x34 && plain_style.color.b == 0x56,
          "plain contextual style resolves without custom properties");
    check(plain_context.custom_property_cache.empty(),
          "plain contextual style does not allocate empty custom property cache entries");
    check(plain_context.custom_property_scopes.empty() && plain_context.matched_rule_cache.empty(),
          "plain contextual style keeps custom-property scopes and selector matches empty");

    auto inline_document = html_parser.parse(
        "<body style='--accent:#654321'><button class='inline'>Go</button></body>");
    Node* inline_button = find_first_by_tag(*inline_document, "button");
    StyleResolver inline_resolver(parse(".inline { color: var(--accent); }"));
    StyleResolveContext inline_context;
    const Style inline_style = inline_resolver.resolve(*inline_button, inline_context);
    check(inline_style.color.r == 0x65 && inline_style.color.g == 0x43 && inline_style.color.b == 0x21,
          "inline custom property still inherits through contextual resolution");
}

void style_resolution_context_tracks_its_inputs() {
    HtmlParser html_parser;
    auto document = html_parser.parse("<body><button>Go</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "context fixture button exists");

    StyleResolver resolver(parse(
        ":root { --tone: #112233; }"
        "button { color: var(--tone); }"
        "button:hover { color: #aabbcc; }"));
    StyleResolveContext context;
    const Style before_hover = resolver.resolve(*button, context);
    check(before_hover.color.r == 0x11 && before_hover.color.g == 0x22 && before_hover.color.b == 0x33,
          "context resolves the initial custom property cascade");

    resolver.set_interaction_state(button, nullptr, nullptr);
    const Style while_hovered = resolver.resolve(*button, context);
    check(while_hovered.color.r == 0xaa && while_hovered.color.g == 0xbb && while_hovered.color.b == 0xcc,
          "context refreshes cached selector matches after interaction changes");

    StyleResolver replacement_resolver(parse(
        ":root { --tone: #445566; }"
        "button { color: var(--tone); }"));
    const Style replacement_style = replacement_resolver.resolve(*button, context);
    check(replacement_style.color.r == 0x44 && replacement_style.color.g == 0x55 && replacement_style.color.b == 0x66,
          "context does not retain custom properties from a different resolver");

    auto replacement_document = html_parser.parse("<body><button>Again</button></body>");
    Node* replacement_button = find_first_by_tag(*replacement_document, "button");
    check(replacement_button != nullptr, "replacement context fixture button exists");
    (void)replacement_resolver.resolve(*replacement_document, context);
    check(context.document_root == replacement_document.get(),
          "resolving a replacement document root refreshes the context boundary");
    const Style replacement_document_style = replacement_resolver.resolve(*replacement_button, context);
    check(replacement_document_style.color.r == 0x44 && replacement_document_style.color.g == 0x55 &&
              replacement_document_style.color.b == 0x66,
          "context refreshes its DOM-scoped caches for a replacement document");
}

void linear_gradient_background_applies_without_breaking_fallbacks() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    VectorDiagnosticSink diagnostics;
    CssParser parser;
    CssParserOptions css_options;
    css_options.diagnostics = &diagnostics;
    Stylesheet stylesheet = parser.parse(
        ".gel { background: #102030; background: linear-gradient(to right, #102030, rgba(80, 120, 160, 0.5)); }"
        ".bad-color { color: #123456; color: linear-gradient(#ffffff, #000000); }"
        ".fallback { background: #111111; background: linear-gradient(46deg, #ffffff, #000000); }"
        ".fx { text-shadow: 0 1px 2px rgba(0,0,0,0.35); outline: 2px solid rgba(255,255,255,0.5); outline-offset: 3px; "
        "text-decoration: underline; }",
        css_options);

    auto gel = make_element("div");
    gel->attributes["class"] = "gel";
    auto bad_color = make_element("div");
    bad_color->attributes["class"] = "bad-color";
    auto fallback = make_element("div");
    fallback->attributes["class"] = "fallback";
    auto fx = make_element("div");
    fx->attributes["class"] = "fx";

    StyleResolverOptions options;
    options.diagnostics = &diagnostics;
    StyleResolver resolver(std::move(stylesheet), options);
    const Style gel_style = resolver.resolve(*gel);
    const Style bad_color_style = resolver.resolve(*bad_color);
    const Style fallback_style = resolver.resolve(*fallback);
    const Style fx_style = resolver.resolve(*fx);

    check(gel_style.background_paint == BackgroundPaintKind::LinearGradient,
          "linear-gradient background selects gradient paint");
    check(gel_style.background_gradient_axis == GradientAxis::Horizontal,
          "linear-gradient direction stores horizontal axis");
    check(gel_style.background_color.r == 0x10 && gel_style.background_color2.r == 80,
          "linear-gradient stores both colors");
    check(gel_style.background_color2.a >= 126 && gel_style.background_color2.a <= 128,
          "linear-gradient stores rgba alpha");
    check(bad_color_style.color.r == 0x12 && bad_color_style.color.g == 0x34,
          "gradient is not accepted as text color");
    check(fallback_style.background_paint == BackgroundPaintKind::Solid &&
              fallback_style.background_color.r == 0x11,
          "unsupported gradient does not replace earlier solid fallback");
    check(fx_style.text_shadow.enabled && fx_style.text_shadow.color.a >= 88 && fx_style.text_shadow.color.a <= 90 &&
              fx_style.outline_width == 2 &&
              fx_style.outline_offset == 3 &&
              fx_style.outline_color.a >= 126 && fx_style.outline_color.a <= 128,
          "text-shadow and outline subset apply");
    check(fx_style.text_decoration_underline && !fx_style.text_decoration_line_through,
          "text-decoration underline applies");
    check(has_diagnostic_code(diagnostics, "style-declaration-ignored"),
          "unsupported gradient value is diagnosed by style resolver");

    auto diagonal = make_element("div");
    diagonal->attributes["class"] = "diagonal";
    StyleResolver diagonal_resolver(parse(
        ".diagonal { background: linear-gradient(to bottom left, #ffffff, #000000); }"));
    const Style diagonal_style = diagonal_resolver.resolve(*diagonal);
    check(diagonal_style.background_paint == BackgroundPaintKind::LinearGradient &&
              diagonal_style.background_gradient_axis == GradientAxis::DiagonalDownLeft,
          "linear-gradient stores diagonal direction subset");

    auto angle = make_element("div");
    angle->attributes["class"] = "angle";
    StyleResolver angle_resolver(parse(
        ".angle { background: linear-gradient(135deg, #ffffff, #000000); }"));
    const Style angle_style = angle_resolver.resolve(*angle);
    check(angle_style.background_paint == BackgroundPaintKind::LinearGradient &&
              angle_style.background_gradient_axis == GradientAxis::DiagonalDownRight,
          "linear-gradient maps common design-tool degree angles to the bounded diagonal raster path");
}

void color_mix_and_bounded_box_shadow_apply() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto card = make_element("div");
    card->attributes["class"] = "card";
    StyleResolver resolver(parse(
        ".card { color: color-mix(in srgb, #ffffff 80%, #62dff7); "
        "border: 1px solid color-mix(in srgb, #ffffff 20%, transparent); "
        "box-shadow: 1px 4px 10px 2px rgba(98,223,247,.28); }"));
    const Style style = resolver.resolve(*card);
    check(style.color.r == 224 && style.color.g == 249 && style.color.b == 253,
          "two-color srgb color-mix resolves explicit and implicit weights");
    check(style.border_color.a >= 50 && style.border_color.a <= 52,
          "color-mix can produce translucent borders");
    check(style.box_shadow.enabled && style.box_shadow.offset_x == 1 && style.box_shadow.offset_y == 4 &&
              style.box_shadow.blur == 10 && style.box_shadow.spread == 2,
          "single bounded box-shadow stores geometry in computed style");
    check(style.box_shadow.color.r == 98 && style.box_shadow.color.g == 223 &&
              style.box_shadow.color.b == 247 && style.box_shadow.color.a >= 70 && style.box_shadow.color.a <= 72,
          "box-shadow retains the authored shadow color and alpha");
}

void two_layer_background_keeps_base_and_highlight() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto card = make_element("div");
    card->attributes["class"] = "card";
    StyleResolver resolver(parse(
        ".card { background: radial-gradient(circle at 78% 12%, rgba(255, 255, 255, 0.22) 0%, transparent 100%), "
        "linear-gradient(to bottom right, #315a7a, #142331); }"));
    const Style style = resolver.resolve(*card);
    check(style.background_paint == BackgroundPaintKind::LinearGradient &&
              style.background_gradient_axis == GradientAxis::DiagonalDownRight,
          "two-layer background keeps the last CSS layer as the base paint");
    const BackgroundPaint overlay = unpack_background_overlay(style.background_overlay_packed);
    check(has_background_overlay(style.background_overlay_packed) && overlay.kind == BackgroundPaintKind::RadialGradient &&
              overlay.axis == GradientAxis::RadialPosition,
          "two-layer background keeps the first CSS layer as the top highlight");
    check(overlay.stop_percent == 78 * 101 + 12,
          "two-layer radial highlight retains percentage position");
}

void conic_gradient_background_applies_progress_subset() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto ring = make_element("div");
    ring->attributes["class"] = "ring";
    auto fallback = make_element("div");
    fallback->attributes["class"] = "fallback";

    StyleResolver resolver(parse(
        ".ring { background: conic-gradient(#22cc88 0% 76%, rgba(16,32,48,.35) 76% 100%); }"
        ".fallback { background: #102030; background: conic-gradient(#fff 20% 80%, #000 80% 100%); }"));

    const Style ring_style = resolver.resolve(*ring);
    const Style fallback_style = resolver.resolve(*fallback);
    check(ring_style.background_paint == BackgroundPaintKind::ConicGradient,
          "conic-gradient background selects conic paint");
    check(ring_style.background_gradient_stop_percent == 76,
          "conic-gradient stores progress stop");
    check(ring_style.background_color.g == 0xcc && ring_style.background_color2.a >= 88,
          "conic-gradient stores progress and track colors");
    check(fallback_style.background_paint == BackgroundPaintKind::Solid &&
              fallback_style.background_color.r == 0x10,
          "unsupported conic-gradient does not clear earlier fallback");
}

void radial_gradient_background_applies_center_circle_subset() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    auto gel = make_element("div");
    gel->attributes["class"] = "gel";
    auto image = make_element("div");
    image->attributes["class"] = "image";
    auto fallback = make_element("div");
    fallback->attributes["class"] = "fallback";
    auto highlight = make_element("div");
    highlight->attributes["class"] = "highlight";

    StyleResolver resolver(parse(
        ".gel { background: radial-gradient(circle at center, rgba(240,255,252,.85) 0%, rgba(36,126,160,.20) 100%); }"
        ".image { background-color: #102030; background-image: radial-gradient(#ffffff, rgba(36,126,160,.20)); }"
        ".highlight { background: radial-gradient(circle at 82% 12%, #ffffff 0%, #102030 100%); }"
        ".fallback { background: #102030; background: radial-gradient(ellipse, #fff, #000); }"));

    const Style gel_style = resolver.resolve(*gel);
    const Style image_style = resolver.resolve(*image);
    const Style highlight_style = resolver.resolve(*highlight);
    const Style fallback_style = resolver.resolve(*fallback);
    check(gel_style.background_paint == BackgroundPaintKind::RadialGradient,
          "radial-gradient background selects radial paint");
    check(gel_style.background_color.r == 240 && gel_style.background_color2.b == 160,
          "radial-gradient stores center and edge colors");
    check(gel_style.background_color.a >= 216 && gel_style.background_color.a <= 217,
          "radial-gradient stores rgba center alpha");
    check(image_style.background_paint == BackgroundPaintKind::RadialGradient &&
              image_style.background_color.r == 255,
          "background-image accepts radial-gradient image subset");
    check(highlight_style.background_paint == BackgroundPaintKind::RadialGradient &&
              highlight_style.background_gradient_axis == GradientAxis::RadialPosition &&
              highlight_style.background_gradient_stop_percent == 82 * 101 + 12,
          "radial-gradient stores bounded percentage center position");
    check(fallback_style.background_paint == BackgroundPaintKind::Solid &&
              fallback_style.background_color.r == 0x10,
          "unsupported radial-gradient does not clear earlier fallback");
}

void package_background_image_url_is_bounded_and_preserves_background_color() {
    auto cover = make_element("div");
    cover->attributes["class"] = "cover";
    auto invalid = make_element("div");
    invalid->attributes["class"] = "invalid";
    auto replacement = make_element("div");
    replacement->attributes["class"] = "replacement";

    VectorDiagnosticSink diagnostics;
    StyleResolverOptions options;
    options.diagnostics = &diagnostics;
    StyleResolver resolver(parse(
        ".cover { background-color: #102030; background-size: cover; background-position: right top; "
        "background-repeat: no-repeat; image-rendering: crisp-edges; background-image: url('/assets/Cover.bmp'); }"
        ".replacement { background: linear-gradient(#ffffff, #000000); "
        "background-image: url('/assets/cover.bmp'); }"
        ".invalid { background-color: #102030; background-image: url('https://example.invalid/cover.bmp'); }"),
        options);

    const Style cover_style = resolver.resolve(*cover);
    const Style invalid_style = resolver.resolve(*invalid);
    const Style replacement_style = resolver.resolve(*replacement);
    const std::uint16_t resource_id = background_image_resource_id(cover_style.background_overlay_packed);
    check(resource_id != 0 && has_background_image_resource(cover_style.background_overlay_packed),
          "absolute package url stores a compact background image resource id");
    const std::string* resource_url = resolver.background_image_resource_url(resource_id);
    check(resource_url != nullptr && *resource_url == "/assets/Cover.bmp",
          "background image resource keeps the case-sensitive package path");
    check(cover_style.background_color.r == 0x10 && cover_style.background_color.g == 0x20,
          "background-image url preserves the existing background color fallback");
    check(background_image_object_fit(cover_style.background_overlay_packed) == ObjectFit::Cover &&
              background_image_object_position(cover_style.background_overlay_packed).x_percent == 100 &&
              background_image_object_position(cover_style.background_overlay_packed).y_percent == 0 &&
              background_image_rendering(cover_style.background_overlay_packed) == ImageRendering::CrispEdges,
          "background image packs size, position and sampling without extending Style");
    check(replacement_style.background_paint == BackgroundPaintKind::Solid &&
              replacement_style.background_color.a == 0 &&
              has_background_image_resource(replacement_style.background_overlay_packed),
          "background-image URL replaces an earlier gradient image layer");
    check(!has_background_image_resource(invalid_style.background_overlay_packed) &&
              invalid_style.background_color.r == 0x10,
          "remote background image URL is rejected without clearing the fallback");
}

void unsupported_conic_gradient_reports_specific_diagnostic() {
    auto fallback = make_element("div");
    fallback->attributes["class"] = "fallback";

    VectorDiagnosticSink diagnostics;
    StyleResolverOptions options;
    options.diagnostics = &diagnostics;
    StyleResolver resolver(parse(
        ".fallback { background: #102030; "
        "background: conic-gradient(#fff -10% 80%, #000 80% 100%); }"),
        options);

    const Style style = resolver.resolve(*fallback);
    check(style.background_paint == BackgroundPaintKind::Solid &&
              style.background_color.r == 0x10,
          "invalid conic-gradient preserves earlier fallback background");
    check(has_diagnostic_code(diagnostics, "style-conic-gradient-unsupported"),
          "unsupported conic-gradient emits specific diagnostic");
}

void unsupported_radial_gradient_reports_specific_diagnostic() {
    auto fallback = make_element("div");
    fallback->attributes["class"] = "fallback";

    VectorDiagnosticSink diagnostics;
    StyleResolverOptions options;
    options.diagnostics = &diagnostics;
    StyleResolver resolver(parse(
        ".fallback { background: #102030; "
        "background-image: radial-gradient(ellipse at 20% 30%, #fff 0%, #000 100%); }"),
        options);

    const Style style = resolver.resolve(*fallback);
    check(style.background_paint == BackgroundPaintKind::Solid &&
              style.background_color.r == 0x10,
          "invalid radial-gradient preserves earlier fallback background");
    check(has_diagnostic_code(diagnostics, "style-radial-gradient-unsupported"),
          "unsupported radial-gradient emits specific diagnostic");
}

void matches_simple_compound_selectors() {
    const Stylesheet stylesheet = parse("button.primary.large { color: #abcdef; }");
    auto element = make_element("button");
    element->attributes["class"] = "primary large";
    StyleResolver resolver(stylesheet);
    const Style style = resolver.resolve(*element);
    check(style.color.r == 0xab && style.color.g == 0xcd && style.color.b == 0xef, "compound selector match");
}

void builds_cssom_metadata() {
    const Stylesheet stylesheet = parse(".button { color: red; } #search.box { color: blue; }");
    check(stylesheet.size() == 2, "cssom rule count");
    check(stylesheet[0].source_order == 0, "first source order");
    check(stylesheet[1].source_order == 1, "second source order");
    check(stylesheet[0].specificity.classes == 1, "class specificity");
    check(stylesheet[1].specificity.ids == 1, "id specificity");
    check(stylesheet[1].specificity.classes == 1, "compound specificity");
}

void cascade_uses_specificity_and_importance() {
    const Stylesheet stylesheet = parse(
        ".box { color: red !important; }"
        "#search { color: blue; }"
        "input.box { background: #111; }"
        ".box { background: #222; }");
    auto element = make_element("input");
    element->attributes["id"] = "search";
    element->attributes["class"] = "box";

    StyleResolver resolver(stylesheet);
    const Style style = resolver.resolve(*element);
    check(style.color.r == 220 && style.color.g == 38 && style.color.b == 38, "important beats id");
    check(style.background_color.r == 0x11, "more specific background wins");
}

void matches_descendant_and_attribute_selectors() {
    const Stylesheet stylesheet = parse(
        ".story img { width: 240px; height: 120px; object-fit: contain; object-position: 25% bottom; image-rendering: pixelated; }"
        "dialog[open] { background: #ffffff; border: 2px solid #123456; }"
        "main > form { padding: 12px; }");

    auto main = make_element("main");
    auto form = make_element("form");
    Node& form_node = main->append_child(std::move(form));

    auto story = make_element("article");
    story->attributes["class"] = "story";
    auto image = make_element("img");
    Node& image_node = story->append_child(std::move(image));

    auto dialog = make_element("dialog");
    dialog->attributes["open"] = "";

    StyleResolver resolver(stylesheet);
    const Style form_style = resolver.resolve(form_node);
    const Style image_style = resolver.resolve(image_node);
    const Style dialog_style = resolver.resolve(*dialog);

    check(form_style.padding.top == 12, "child selector applies");
    check(image_style.width == 240 && image_style.height == 120, "descendant selector applies");
    check(image_style.object_fit == ObjectFit::Contain, "object-fit applies");
    check(image_style.object_position.x_percent == 25 && image_style.object_position.y_percent == 100,
          "object-position applies");
    check(image_style.image_rendering == ImageRendering::Pixelated, "image-rendering applies");
    check(dialog_style.border_width.top == 2, "attribute selector border applies");
    check(dialog_style.background_color.r == 255, "attribute selector background applies");
}

void matches_sibling_selectors() {
    auto root = make_element("section");
    auto first = make_element("button");
    first->attributes["class"] = "primary";
    auto text = make_text(" ");
    auto second = make_element("button");
    second->attributes["class"] = "secondary";
    auto third = make_element("button");
    third->attributes["class"] = "tertiary";

    Node& first_node = root->append_child(std::move(first));
    root->append_child(std::move(text));
    Node& second_node = root->append_child(std::move(second));
    Node& third_node = root->append_child(std::move(third));

    StyleResolver resolver(parse(
        ".primary + .secondary { color: #123456; }"
        ".primary ~ .tertiary { background: #abcdef; }"
        ".secondary + .primary { width: 99px; }"));

    const Style first_style = resolver.resolve(first_node);
    const Style second_style = resolver.resolve(second_node);
    const Style third_style = resolver.resolve(third_node);

    check(first_style.width == -1, "reverse adjacent sibling does not match");
    check(second_style.color.r == 0x12 && second_style.color.g == 0x34 && second_style.color.b == 0x56,
          "adjacent sibling selector matches across text nodes");
    check(third_style.background_color.r == 0xab && third_style.background_color.g == 0xcd &&
              third_style.background_color.b == 0xef,
          "general sibling selector matches");
}

void matches_dynamic_pseudo_classes() {
    auto root = make_element("section");
    auto panel = make_element("div");
    panel->attributes["class"] = "panel";
    auto button = make_element("button");
    button->attributes["id"] = "go";
    auto checkbox = make_element("input");
    checkbox->attributes["id"] = "agree";
    checkbox->attributes["type"] = "checkbox";
    auto disabled = make_element("button");
    disabled->attributes["id"] = "off";
    disabled->attributes["disabled"] = "";

    Node& panel_node = root->append_child(std::move(panel));
    Node& button_node = panel_node.append_child(std::move(button));
    Node& checkbox_node = root->append_child(std::move(checkbox));
    Node& disabled_node = root->append_child(std::move(disabled));
    set_form_control_checked(checkbox_node, true);

    StyleResolverOptions options;
    options.hovered_node = &button_node;
    options.active_node = &button_node;
    options.focused_node = &button_node;
    StyleResolver resolver(parse(
        ".panel:hover { background: #101010; }"
        "button:hover { color: #123456; }"
        "button:active { border-color: #abcdef; }"
        "button:focus { width: 88px; }"
        ".panel:focus-within { padding: 9px; }"
        "input:checked { background: #0f172a; }"
        "button:disabled { color: #777777; }"),
        options);

    const Style panel_style = resolver.resolve(panel_node);
    const Style button_style = resolver.resolve(button_node);
    const Style checkbox_style = resolver.resolve(checkbox_node);
    const Style disabled_style = resolver.resolve(disabled_node);

    check(panel_style.background_color.r == 0x10, "ancestor hover matches");
    check(panel_style.padding.top == 9, "focus-within matches ancestor");
    check(button_style.color.r == 0x12 && button_style.color.g == 0x34 && button_style.color.b == 0x56,
          "hover style matches");
    check(button_style.border_color.r == 0xab && button_style.border_color.g == 0xcd,
          "active style matches");
    check(button_style.width == 88, "focus style matches");
    check(checkbox_style.background_color.r == 0x0f && checkbox_style.background_color.g == 0x17,
          "checked style matches");
    check(disabled_style.color.r == 0x77, "disabled style matches");
}

void reports_interaction_invalidation_hints_from_selectors() {
    StyleResolver static_resolver(parse(
        ".card { color: #111111; }"
        "input:checked { background: #101010; }"
        "button:disabled { opacity: .5; }"));
    InteractionInvalidationHints static_hints = static_resolver.interaction_invalidation_hints();
    check(!static_hints.hover && !static_hints.active && !static_hints.focus,
          "static selectors do not request interaction invalidation");

    StyleResolver dynamic_resolver(parse(
        ".card:hover { color: #222222; }"
        "button:active { color: #333333; }"
        ".panel:focus-within { padding: 4px; }"
        "article:is(.selected, :hover) { background: #444444; }"
        "input:where(:focus) { width: 120px; }"));
    InteractionInvalidationHints dynamic_hints = dynamic_resolver.interaction_invalidation_hints();
    check(dynamic_hints.hover, "hover selector requests hover invalidation");
    check(dynamic_hints.active, "active selector requests active invalidation");
    check(dynamic_hints.focus, "focus selector requests focus invalidation");
}

void matches_is_where_with_specificity() {
    auto card = make_element("article");
    card->attributes["class"] = "card selected";
    auto button = make_element("button");
    button->attributes["class"] = "action";
    Node& button_node = card->append_child(std::move(button));

    StyleResolverOptions options;
    options.hovered_node = &button_node;
    StyleResolver resolver(parse(
        ".card { color: #111111; }"
        ":where(.card) { color: #222222; }"
        ":is(.card) { background: #333333; }"
        "article:is(.missing, .selected) > button:is(.action, .other):hover { width: 77px; }"),
        options);

    const Style card_style = resolver.resolve(*card);
    const Style button_style = resolver.resolve(button_node);

    check(card_style.color.r == 0x11, ":where has zero specificity and does not override class");
    check(card_style.background_color.r == 0x33, ":is selector matches and cascades");
    check(button_style.width == 77, ":is selector list with pseudo state matches");
}

void controls_have_usable_default_boxes() {
    auto input = make_element("input");
    auto button = make_element("button");
    StyleResolver resolver(Stylesheet{});

    const Style input_style = resolver.resolve(*input);
    const Style button_style = resolver.resolve(*button);
    check(input_style.display == Display::InlineBlock, "input default display");
    check(input_style.min_width >= 80, "input default min width");
    check(input_style.border_width.top == 1, "input default border");
    check(button_style.display == Display::InlineBlock, "button default display");
    check(button_style.padding.left > 0, "button default padding");
}

Node* find_first_by_tag(Node& node, const std::string& tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void embedded_styles_and_common_lengths_apply() {
    HtmlParser html_parser;
    auto document = html_parser.parse(
        "<html><head><style>"
        "body{background:#f5f7fa;color:#333;line-height:1.6;padding:2rem;}"
        ".container{max-width:800px;margin:0 auto;background:#fff;padding:3rem;}"
        "h1{color:#2c3e50;text-align:center;margin-bottom:1.5rem;}"
        ".intro{font-size:1.05rem;text-indent:2em;}"
        "</style></head><body><div class='container'><h1>Title</h1><p class='intro'>Text</p></div></body></html>");
    CssParser css_parser;
    StyleResolver resolver(css_parser.parse(combine_author_css("", *document)));

    Node* body = find_first_by_tag(*document, "body");
    Node* container = find_first_by_tag(*document, "div");
    Node* heading = find_first_by_tag(*document, "h1");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(body != nullptr && container != nullptr && heading != nullptr && paragraph != nullptr, "fixture nodes exist");

    const Style body_style = resolver.resolve(*body);
    const Style container_style = resolver.resolve(*container);
    const Style heading_style = resolver.resolve(*heading);
    const Style paragraph_style = resolver.resolve(*paragraph);

    check(body_style.background_color.r == 0xf5 && body_style.background_color.g == 0xf7, "embedded body background");
    check(body_style.padding.top == 32, "rem padding parsed");
    check(container_style.background_color.r == 255, "container background");
    check(container_style.max_width == 800, "max-width parsed");
    check(container_style.margin_left_auto && container_style.margin_right_auto, "auto margins parsed");
    check(heading_style.color.r == 0x2c && heading_style.color.g == 0x3e, "heading color parsed");
    check(heading_style.text_align == TextAlign::Center, "heading text-align parsed");
    check(paragraph_style.font_size == 17, "fractional rem font-size parsed");
    check(paragraph_style.text_indent == 34, "em text-indent parsed against font size");
}

bool linked_stylesheet_callback(std::string_view href, std::string& output, void*) {
    if (href == "style1.css") {
        output = "h1 { color: #123456; }";
        return true;
    }
    return false;
}

void linked_stylesheets_merge_into_author_css() {
    HtmlParser html_parser;
    auto document = html_parser.parse(
        "<html><head><link rel='preconnect' href='ignored.css'>"
        "<link rel='stylesheet' href='style1.css'></head><body><h1>Title</h1></body></html>");
    CssParser css_parser;
    StyleResolver resolver(css_parser.parse(
        combine_author_css("", *document, linked_stylesheet_callback, nullptr)));

    Node* heading = find_first_by_tag(*document, "h1");
    check(heading != nullptr, "heading exists");
    const Style style = resolver.resolve(*heading);
    check(style.color.r == 0x12 && style.color.g == 0x34 && style.color.b == 0x56,
          "linked stylesheet applies");
}

void deep_author_css_collection_is_iterative() {
    auto document = make_element("document");
    Node& head = document->append_child(make_element("head"));
    Node& linked = head.append_child(make_element("link"));
    linked.attributes["rel"] = "stylesheet";
    linked.attributes["href"] = "style1.css";

    Node* current = &head;
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }
    Node& style = current->append_child(make_element("style"));
    style.append_child(make_text("h1 { background: #abcdef; }"));

    const std::string css = combine_author_css("", *document, linked_stylesheet_callback, nullptr);
    const std::size_t linked_pos = css.find("color: #123456");
    const std::size_t embedded_pos = css.find("background: #abcdef");
    check(linked_pos != std::string::npos, "deep author css keeps linked stylesheet");
    check(embedded_pos != std::string::npos, "deep author css keeps embedded stylesheet");
    check(linked_pos < embedded_pos, "deep author css preserves order");
}

void author_css_collection_respects_aggregate_resource_limits() {
    HtmlParser html_parser;
    auto document = html_parser.parse(
        "<html><head>"
        "<style>.first { color: red; }</style>"
        "<link rel='stylesheet' href='style1.css'>"
        "<style>.third { color: blue; }</style>"
        "</head></html>");
    VectorDiagnosticSink diagnostics;
    DocumentStyleCollectionOptions options;
    options.max_stylesheets = 2;
    options.max_total_bytes = 1024;
    options.diagnostics = &diagnostics;
    const std::string count_limited = combine_author_css(
        "", *document, linked_stylesheet_callback, nullptr, options);
    check(count_limited.find(".first") != std::string::npos, "first stylesheet is retained");
    check(count_limited.find("#123456") != std::string::npos, "second stylesheet is retained");
    check(count_limited.find(".third") == std::string::npos, "later stylesheet is skipped at count limit");
    check(has_diagnostic_code(diagnostics, "css-document-resource-limit"), "stylesheet count limit is diagnosed");

    diagnostics.clear();
    options.max_stylesheets = 8;
    options.max_total_bytes = 25;
    const std::string byte_limited = combine_author_css("", *document, linked_stylesheet_callback, nullptr, options);
    check(byte_limited.find(".first") != std::string::npos, "complete stylesheet fitting byte budget is retained");
    check(byte_limited.find("#123456") == std::string::npos, "oversized later stylesheet is not truncated");
    check(has_diagnostic_code(diagnostics, "css-document-resource-limit"), "stylesheet byte limit is diagnosed");
}

void html5_semantic_defaults_are_visible() {
    auto mark = make_element("mark");
    auto blockquote = make_element("blockquote");
    auto progress = make_element("progress");
    StyleResolver resolver(Stylesheet{});

    const Style mark_style = resolver.resolve(*mark);
    const Style quote_style = resolver.resolve(*blockquote);
    const Style progress_style = resolver.resolve(*progress);

    check(mark_style.background_color.a == 255, "mark has visible background");
    check(quote_style.display == Display::Block && quote_style.border_width.left > 0,
          "blockquote has block fallback");
    check(progress_style.display == Display::InlineBlock && progress_style.width > 0 && progress_style.height > 0,
          "progress has visible fallback box");
}

void border_none_removes_default_control_border() {
    auto button = make_element("button");
    StyleResolver resolver(parse("button { border: none; }"));

    const Style style = resolver.resolve(*button);
    check(style.border_width.top == 0 && style.border_width.right == 0 &&
              style.border_width.bottom == 0 && style.border_width.left == 0,
          "border none removes default control border");
}

void grid_and_aspect_ratio_properties_apply() {
    auto grid = make_element("div");
    grid->attributes["class"] = "grid";
    auto card = make_element("section");
    card->attributes["class"] = "wide";
    auto media = make_element("div");
    media->attributes["class"] = "media";

    StyleResolver resolver(parse(
        ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));"
        "grid-template-rows: 36px 1fr 1fr; grid-auto-rows: minmax(140px, auto); gap: 1.2rem; }"
        ".wide { grid-column: 2 / span 2; grid-row: 2 / 4; }"
        ".media { aspect-ratio: auto 1.5 / 1; }"));

    const Style grid_style = resolver.resolve(*grid);
    const Style card_style = resolver.resolve(*card);
    const Style media_style = resolver.resolve(*media);

    check(grid_style.display == Display::Grid, "grid display parsed");
    check(grid_style.grid_min_track_width == 220, "grid min track parsed");
    check(grid_style.grid_template_row_count == 3 && grid_style.grid_template_row_heights[0] == 36 &&
              grid_style.grid_template_row_heights[1] == 0 && grid_style.grid_template_row_heights[2] == 0,
          "fixed and fr grid row tracks parse");
    check(grid_style.grid_auto_row_min == 140, "grid auto row min parsed");
    check(grid_style.column_gap == 19 && grid_style.row_gap == 19, "rem gap parsed");
    check(card_style.grid_column_start == 1 && card_style.grid_column_span == 2 &&
              card_style.grid_row_start == 1 && card_style.grid_row_span == 2,
          "numeric grid placement and span parse");
    check(media_style.aspect_ratio_width == 1500 && media_style.aspect_ratio_height == 1000,
          "aspect ratio parsed");
}

void physical_edge_longhands_apply_per_side() {
    auto element = make_element("section");
    element->attributes["id"] = "panel";
    element->attributes["class"] = "card";

    StyleResolver resolver(parse(
        "#panel { margin-top: 18px; border-bottom: 5px solid #222222; }"
        ".card { margin: 4px; padding: 2px; border: 1px solid #111111; }"
        ".card { margin-left: auto; padding-left: 12px; border-left-width: 3px; "
        "border-right: 4px solid #334455; }"));

    const Style style = resolver.resolve(*element);
    check(style.margin.top == 18, "higher-specificity margin-top survives shorthand");
    check(style.margin.right == 4, "margin shorthand right applies");
    check(style.margin_left_auto, "margin-left auto applies");
    check(style.padding.top == 2 && style.padding.left == 12, "padding longhand applies");
    check(style.border_width.top == 1 && style.border_width.left == 3, "border width longhand applies");
    check(style.border_width.right == 4, "border-right shorthand applies");
    check(style.border_width.bottom == 5, "higher-specificity border-bottom shorthand survives shorthand");
    check(style.border_color.r == 0x22, "higher-specificity single-side border color wins global border color");
}

void font_weight_list_style_and_generated_counter_apply() {
    auto list = make_element("ol");
    list->attributes["class"] = "custom-list";
    auto item = make_element("li");
    Node& item_node = list->append_child(std::move(item));

    StyleResolver resolver(parse(
        ".custom-list { list-style: none; }"
        ".custom-list > li { font-weight: 500; }"
        ".custom-list > li::before { content: counter(list-num) \".\"; color: #2b6cb0; font-weight: 600; left: 0; }"));

    const Style list_style = resolver.resolve(*list);
    const Style item_style = resolver.resolve(item_node);
    check(list_style.list_style_type == ListStyleType::None, "list-style none parsed");
    check(item_style.font_weight == 500, "font-weight numeric parsed");
    check(item_style.before_content_kind == GeneratedContentKind::Counter, "counter before content parsed");
    check(item_style.before_color.b == 0xb0, "before color parsed");
    check(item_style.before_font_weight == 600, "before font-weight parsed");
}

void text_transform_parses_and_inherits() {
    auto root = make_element("section");
    root->attributes["class"] = "screen";
    auto label = make_element("span");
    root->append_child(std::move(label));

    StyleResolver resolver(parse(".screen { text-transform: uppercase; }"));
    const Style root_style = resolver.resolve(*root);
    check(root_style.text_transform == TextTransform::Uppercase, "text-transform uppercase parsed");
    check(root_style.text_transform_specified, "text-transform marks style as specified");

    RenderTreeBuilder builder(resolver);
    auto tree = builder.build(*root);
    check(!tree->children.empty(), "text-transform inheritance fixture builds child render object");
    check(tree->children.front()->style.text_transform == TextTransform::Uppercase,
          "text-transform inherits through render tree");
}

void font_family_declares_runtime_family_hash_and_inherits() {
    auto root = make_element("section");
    root->attributes["class"] = "screen";
    auto label = make_element("span");
    Node& label_node = root->append_child(std::move(label));

    StyleResolver resolver(parse(
        ".screen { font-family: \"Jelly Tiny\", system-ui, sans-serif; }"
        ".screen .generic { font-family: system-ui; }"));

    const Style root_style = resolver.resolve(*root);
    check(root_style.font_family_hash == normalized_font_family_hash("Jelly Tiny"),
          "custom font-family hashes to runtime family");
    check(root_style.font_family_specified, "font-family declaration marks style as specified");

    RenderTreeBuilder builder(resolver);
    auto tree = builder.build(*root);
    check(!tree->children.empty(), "font-family inheritance fixture builds child render object");
    check(tree->children.front()->style.font_family_hash == root_style.font_family_hash,
          "font-family inherits through render tree");

    label_node.attributes["class"] = "generic";
    const Style generic_style = resolver.resolve(label_node);
    check(generic_style.font_family_hash == 0, "generic font-family maps to host/system fallback");
}

void after_generated_content_and_text_overflow_apply() {
    auto badge = make_element("span");
    badge->attributes["class"] = "badge";

    StyleResolver resolver(parse(
        ".badge { border-radius: 50%; white-space: nowrap; text-overflow: ellipsis; overflow: hidden; }"
        ".badge::after { content: \"%\"; color: #22cc88; font-weight: 700; left: 4px; }"));

    const Style style = resolver.resolve(*badge);
    check(style.border_radius_percent == 50, "percentage border-radius parsed");
    check(style.white_space_nowrap, "white-space nowrap parsed");
    check(style.text_overflow_ellipsis, "text-overflow ellipsis parsed");
    check(style.after_content_kind == GeneratedContentKind::Text, "after generated content parsed");
    check(style.after_content_text == "%", "after generated text stored");
    check(style.after_color.g == 0xcc, "after color parsed");
    check(style.after_font_weight == 700, "after font-weight parsed");
    check(style.after_left_specified && style.after_left == 4, "after left parsed");
}

void per_corner_border_radius_applies() {
    auto card = make_element("div");
    card->attributes["class"] = "card";
    StyleResolver resolver(parse(".card { border-radius: 16px 10px 4px 0; }"));
    const Style style = resolver.resolve(*card);
    const CornerRadii radii = decode_corner_radii(style.border_radius);
    check(radii.top_left == 16 && radii.top_right == 10 && radii.bottom_right == 4 && radii.bottom_left == 0,
          "border-radius 1-4 length syntax retains physical corner radii");
}

void overflow_y_uses_the_vertical_scroll_subset() {
    auto list = make_element("section");
    StyleResolver resolver(parse(
        "section { overflow: visible; overflow-y: auto; }"
        "section.reverse { overflow-y: scroll; overflow: hidden; }"
        "section.hidden-y { overflow: auto; overflow-y: hidden; }"));

    const Style vertical_style = resolver.resolve(*list);
    check(vertical_style.overflow == "auto", "overflow-y overrides an earlier overflow declaration");

    list->attributes["class"] = "reverse";
    const Style shorthand_style = resolver.resolve(*list);
    check(shorthand_style.overflow == "hidden", "later overflow shorthand overrides overflow-y in the shared subset");

    list->attributes["class"] = "hidden-y";
    const Style unsupported_axis_style = resolver.resolve(*list);
    check(unsupported_axis_style.overflow == "auto",
          "unsupported overflow-y hidden preserves the earlier supported overflow fallback");
}

void fixed_two_column_grid_template_applies() {
    auto list = make_element("dl");
    StyleResolver resolver(parse("dl { display: grid; grid-template-columns: 120px 1fr; gap: .8rem; }"));

    const Style style = resolver.resolve(*list);
    check(style.display == Display::Grid, "dl grid display parsed");
    check(style.grid_template_column_count == 2, "fixed grid column count parsed");
    check(style.grid_template_column_widths[0] == 120, "fixed grid first column parsed");
    check(style.grid_template_column_widths[1] == 0, "fr grid column stored as flexible");
    check(style.column_gap == 13 && style.row_gap == 13, "fractional rem gap parsed for fixed grid");
}

void repeated_fixed_grid_template_applies() {
    auto keys = make_element("section");
    keys->attributes["class"] = "keys";
    StyleResolver resolver(parse(".keys { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }"));

    const Style style = resolver.resolve(*keys);
    check(style.display == Display::Grid, "repeat grid display parsed");
    check(style.grid_template_column_count == 4, "repeat grid column count parsed");
    check(style.grid_template_column_widths[0] == 0 &&
              style.grid_template_column_widths[3] == 0,
          "repeat fr columns stored as flexible");
    check(style.column_gap == 8 && style.row_gap == 8, "repeat grid gap parsed");
}

void repeated_minmax_zero_grid_template_applies() {
    auto keys = make_element("section");
    keys->attributes["class"] = "keys";
    StyleResolver resolver(parse(".keys { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); }"));

    const Style style = resolver.resolve(*keys);
    check(style.display == Display::Grid, "repeat minmax grid display parsed");
    check(style.grid_template_column_count == 4, "repeat minmax grid column count parsed");
    check(style.grid_template_column_widths[0] == 0 &&
              style.grid_template_column_widths[3] == 0,
          "repeat minmax zero columns stored as flexible");
}

void modern_length_functions_and_flex_wrap_apply() {
    auto hero = make_element("h1");
    auto panel = make_element("section");
    auto card = make_element("div");
    card->attributes["class"] = "card";

    StyleResolver resolver(parse(
        "h1 { font-size: clamp(2rem, 8vw, 4rem); }"
        "section { padding: 4rem clamp(1rem, 5vw, 4rem); display: flex; flex-wrap: wrap; }"
        ".card { width: calc(33% - 0.8rem); max-width: min(320px, 100%); }"));

    const Style hero_style = resolver.resolve(*hero);
    const Style panel_style = resolver.resolve(*panel);
    const Style card_style = resolver.resolve(*card);

    check(hero_style.font_size == 32, "clamp font-size parsed with conservative viewport fallback");
    check(panel_style.padding.top == 64 && panel_style.padding.left == 18, "clamp padding parsed");
    check(panel_style.display == Display::Flex && panel_style.flex_wrap, "flex-wrap parsed");
    check(card_style.width > 80 && card_style.width < 130, "calc width parsed with percentage fallback");
    check(card_style.max_width == 320, "min max-width parsed with percentage fallback");
}

void flex_sizing_properties_apply() {
    auto item = make_element("div");
    item->attributes["class"] = "item";
    StyleResolver resolver(parse(
        ".item { flex: 2 1 40px; }"
        ".item { flex-grow: 3; flex-shrink: 0; flex-basis: 24px; }"));

    const Style style = resolver.resolve(*item);
    check(style.flex_grow == 3000, "flex-grow parsed");
    check(style.flex_shrink == 0, "flex-shrink parsed");
    check(style.flex_basis == 24, "flex-basis parsed");
}

void flex_direction_column_applies() {
    auto panel = make_element("section");
    StyleResolver resolver(parse("section { display: flex; flex-direction: column; }"));

    const Style style = resolver.resolve(*panel);
    check(style.display == Display::Flex, "flex display remains available with column direction");
    check(style.flex_direction == FlexDirection::Column, "column flex direction parses");
}

void align_self_applies() {
    auto item = make_element("div");
    StyleResolver resolver(parse("div { align-self: center; }"));

    const Style style = resolver.resolve(*item);
    check(style.align_self == AlignItems::Center, "align-self center parses");
}

void align_content_applies() {
    auto panel = make_element("section");
    StyleResolver resolver(parse("section { align-content: center; }"));

    const Style style = resolver.resolve(*panel);
    check(style.align_content == JustifyContent::Center, "align-content center parses");
}

void positioned_offsets_apply() {
    auto panel = make_element("section");
    panel->attributes["class"] = "panel";
    StyleResolver resolver(parse(
        ".panel { position: absolute; top: 8px; right: 12px; bottom: auto; left: 4px; z-index: 3; }"));

    const Style style = resolver.resolve(*panel);
    check(style.position == "absolute", "position absolute parsed");
    check(style.inset_top_specified && style.inset_top == 8, "top offset parsed");
    check(style.inset_right_specified && style.inset_right == 12, "right offset parsed");
    check(!style.inset_bottom_specified, "bottom auto clears offset");
    check(style.inset_left_specified && style.inset_left == 4, "left offset parsed");
    check(!style.z_index_auto && style.z_index == 3, "z-index still applies with positioned offsets");
}

void logical_properties_and_hsl_apply() {
    auto panel = make_element("section");
    panel->attributes["class"] = "panel";
    StyleResolver resolver(parse(
        ".panel { margin-inline: 8px 12px !important; padding-block: 3px 5px; "
        "border-inline-width: 1px 2px; "
        "inline-size: 72px; min-block-size: 24px; position: absolute; "
        "inset-inline: 7px 9px; inset-block-start: 4px; place-content: end center; "
        "color: hsl(210 50% 40% / 50%); background-color: hsla(120, 100%, 25%, .5); }"
        ".panel { margin-left: 1px; }"));

    const Style style = resolver.resolve(*panel);
    check(style.margin.left == 8 && style.margin.right == 12,
          "logical margin respects physical cascade slots and important");
    check(style.padding.top == 3 && style.padding.bottom == 5, "logical block padding maps to physical edges");
    check(style.border_width.left == 1 && style.border_width.right == 2,
          "logical border width maps to physical edges without changing border color semantics");
    check(style.width == 72 && style.min_height == 24, "logical sizing maps to physical sizing");
    check(style.inset_left_specified && style.inset_left == 7 &&
              style.inset_right_specified && style.inset_right == 9 &&
              style.inset_top_specified && style.inset_top == 4,
          "logical inset maps to LTR physical offsets");
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    check(style.align_content == JustifyContent::End && style.justify_content == JustifyContent::Center,
          "place-content expands through existing alignment properties");
#else
    check(style.align_content == JustifyContent::Start && style.justify_content == JustifyContent::Start,
          "disabled flex-grid ignores place-content without changing the block fallback");
#endif
    check(style.color.r == 51 && style.color.g == 102 && style.color.b == 153 && style.color.a == 128,
          "hsl color converts through the existing sRGB color path");
    check(style.background_color.r == 0 && style.background_color.g == 128 &&
              style.background_color.b == 0 && style.background_color.a == 128,
          "hsla background color keeps optional alpha");
}

void supports_queries_accept_logical_properties_and_hsl() {
    auto panel = make_element("div");
    panel->attributes["class"] = "panel";
    StyleResolver resolver(parse(
        "@supports (inline-size: 48px) and (color: hsl(210 50% 40%)) { "
        ".panel { inline-size: 48px; color: hsl(.583333turn 50% 40%); } }"));

    const Style style = resolver.resolve(*panel);
    check(style.width == 48 && style.color.r == 51 && style.color.g == 102 && style.color.b == 153,
          "supports uses the same logical-property and hsl subset as style resolution");
}

void invalid_hsl_preserves_prior_fallback() {
    auto panel = make_element("div");
    panel->attributes["class"] = "panel";
    StyleResolver resolver(parse(".panel { color: #123456; color: hsl(nan 50% 40%); }"));

    const Style style = resolver.resolve(*panel);
    check(style.color.r == 0x12 && style.color.g == 0x34 && style.color.b == 0x56,
          "invalid hsl cannot override an earlier supported color fallback");
}

void style_candidate_cache_preserves_selector_context() {
    auto root = make_element("main");
    auto sidebar = make_element("section");
    sidebar->attributes["class"] = "sidebar";
    auto plain = make_element("section");
    auto first = make_element("button");
    first->attributes["class"] = "action";
    auto second = make_element("button");
    second->attributes["class"] = "action";
    Node& first_node = sidebar->append_child(std::move(first));
    Node& second_node = plain->append_child(std::move(second));
    root->append_child(std::move(sidebar));
    root->append_child(std::move(plain));

    StyleResolver resolver(parse(
        ".action { color: #111111; }"
        ".sidebar .action { color: #2563eb; }"));

    const Style first_style = resolver.resolve(first_node);
    const Style second_style = resolver.resolve(second_node);
    const Style first_style_again = resolver.resolve(first_node);
    const StyleResolverStatistics statistics = resolver.statistics();

    check(first_style.color.b == 0xeb, "cached candidates keep descendant match");
    check(second_style.color.r == 0x11 && second_style.color.b == 0x11,
          "cached candidates do not leak ancestor match");
    check(first_style_again.color.b == 0xeb, "repeated resolve keeps descendant style");
    check(statistics.candidate_cache_hits == 2, "style candidate cache records repeated key hits");
    check(statistics.candidate_cache_misses == 1, "style candidate cache records first key miss");
    check(statistics.candidate_cache_entries == 1, "style candidate cache reuses equivalent candidate keys");
    check(statistics.candidate_cache_rule_refs >= 2, "style candidate cache reports cached rule references");
}

void style_candidate_cache_respects_tiny_budget_and_inline_style() {
    auto primary = make_element("button");
    primary->attributes["class"] = "primary";
    auto danger = make_element("button");
    danger->attributes["class"] = "danger";
    danger->attributes["style"] = "color: #ff0000";

    StyleResolver resolver(parse(
        ".primary { color: #2563eb; }"
        ".danger { color: #111111; }"),
        StyleResolverOptions{1});

    const Style primary_style = resolver.resolve(*primary);
    const Style danger_style = resolver.resolve(*danger);
    const Style primary_style_again = resolver.resolve(*primary);
    const StyleResolverStatistics statistics = resolver.statistics();

    check(primary_style.color.b == 0xeb, "tiny style cache resolves first class");
    check(danger_style.color.r == 0xff && danger_style.color.g == 0, "inline style survives tiny cache");
    check(primary_style_again.color.b == 0xeb, "tiny style cache retains the first hot class");
    check(statistics.candidate_cache_hits == 1, "tiny style cache reuses retained candidate entries");
    check(statistics.candidate_cache_misses == 2, "tiny style cache records uncached candidate keys");
    check(statistics.candidate_cache_clears == 0, "tiny style cache does not churn bounded entries");
    check(statistics.candidate_cache_bypasses == 1, "tiny style cache reports non-retained candidates");
    check(statistics.candidate_cache_entries == 1, "tiny style cache keeps one bounded entry");
}

void style_candidate_cache_ignores_irrelevant_identifiers() {
    auto first = make_element("button");
    first->attributes["id"] = "first-instance";
    first->attributes["class"] = "action telemetry-only";
    auto second = make_element("button");
    second->attributes["id"] = "second-instance";
    second->attributes["class"] = "action debug-marker";

    StyleResolver resolver(parse("button { font-size: 12px; }.action { color: #2563eb; }"));
    const Style first_style = resolver.resolve(*first);
    const Style second_style = resolver.resolve(*second);
    const StyleResolverStatistics statistics = resolver.statistics();

    check(first_style.color.b == 0xeb && second_style.color.b == 0xeb,
          "irrelevant identifiers cannot change selector matching");
    check(statistics.candidate_cache_misses == 1 && statistics.candidate_cache_hits == 1,
          "irrelevant ids and classes share the same candidate rule cache entry");
    check(statistics.candidate_cache_entries == 1,
          "irrelevant identifiers do not consume candidate cache capacity");
}

void parser_limits_unbounded_css_fields_without_losing_following_rules() {
    CssParser parser;
    VectorDiagnosticSink diagnostics;
    CssParserOptions options;
    options.diagnostics = &diagnostics;
    options.max_selector_bytes = 16;
    options.max_at_rule_prelude_bytes = 8;
    options.max_declaration_value_bytes = 8;
    const Stylesheet stylesheet = parser.parse(
        ".selector-that-is-too-long { color: red; }"
        "@media screen and (min-width: 320px) { .ignored { color: blue; } }"
        ".limited { color: rgb(123, 456, 789); }"
        ".kept { color: #123456; }",
        options);

    check(stylesheet.size() == 1 && stylesheet[0].selector == ".kept",
          "oversized parser fields are skipped without corrupting following rules");
    check(has_diagnostic_code(diagnostics, "css-selector-limit"), "selector cap is reported");
    check(has_diagnostic_code(diagnostics, "css-at-rule-prelude-limit"), "at-rule prelude cap is reported");
    check(has_diagnostic_code(diagnostics, "css-declaration-value-limit"), "declaration value cap is reported");
}

void nonfinite_and_out_of_range_numeric_values_preserve_safe_fallbacks() {
    auto element = make_element("div");
    element->attributes["class"] = "bounded";
    StyleResolver resolver(parse(
        ".bounded {"
        " width: 24px; width: 1e20px;"
        " height: 18px; height: nanpx;"
        " opacity: 0.75; opacity: inf;"
        " line-height: 2; line-height: 1e20;"
        " transform: translate(12px, 4px); transform: translate(infpx, 0);"
        " transform-origin: 25% 75%; transform-origin: nan% 0%;"
        "}"));

    const Style style = resolver.resolve(*element);
    check(style.width == 24 && style.height == 18,
          "nonfinite or out-of-range lengths preserve the earlier declaration");
    check(style.opacity == 0.75F, "nonfinite opacity preserves the earlier declaration");
    check(style.line_height == 28, "out-of-range line-height preserves the earlier declaration");
    Transform2D transform;
    check(parse_css_transform_2d(style.transform, transform) &&
              transform.translate_x == 12.0F && transform.translate_y == 4.0F,
          "nonfinite transforms preserve the earlier declaration");
    check(style.transform_origin_x_percent == 25 && style.transform_origin_y_percent == 75,
          "nonfinite transform origins preserve the earlier declaration");
}

void text_overflow_is_specified_but_not_inherited_by_nested_elements() {
    auto parent = make_element("div");
    parent->attributes["class"] = "parent";
    auto child = make_element("span");
    Node& child_node = parent->append_child(std::move(child));
    StyleResolver resolver(parse(".parent { text-overflow: ellipsis; }"));

    const Style parent_style = resolver.resolve(*parent);
    const Style child_style = resolver.resolve(child_node);
    check(parent_style.text_overflow_ellipsis && parent_style.text_overflow_specified,
          "ellipsis keeps its specified state");
    check(!child_style.text_overflow_ellipsis && !child_style.text_overflow_specified,
          "a child starts with an unspecified text-overflow value");

    RenderTreeBuilder builder(resolver);
    auto tree = builder.build(*parent);
    check(!tree->children.empty() && !tree->children.front()->style.text_overflow_ellipsis,
          "nested elements do not inherit the parent ellipsis state");
}

} // namespace

int main() {
    try {
        parses_comments_strings_and_functions();
        splits_selector_lists();
        expands_single_level_explicit_css_nesting();
        rejects_nested_css_outside_explicit_single_level_subset();
        nesting_preprocessor_respects_depth_and_output_budgets();
        skips_enhancement_blocks_without_corrupting_following_rules();
        pipeline_diagnostics_report_css_and_style_degradation();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        supports_queries_flatten_safe_declaration_subset();
        supports_queries_apply_representative_supported_properties();
#else
        disabled_flex_grid_queries_and_declarations_fall_back();
#endif
        text_wrap_alias_reuses_bounded_white_space_behavior();
        style_struct_size_has_embedded_guardrail();
        flattens_layers_and_plain_media();
        conditional_media_queries_respect_viewport();
        preserves_declaration_fallback_order();
        resolves_simple_css_custom_properties();
        style_resolution_context_tracks_its_inputs();
        linear_gradient_background_applies_without_breaking_fallbacks();
        color_mix_and_bounded_box_shadow_apply();
        two_layer_background_keeps_base_and_highlight();
        conic_gradient_background_applies_progress_subset();
        radial_gradient_background_applies_center_circle_subset();
        package_background_image_url_is_bounded_and_preserves_background_color();
        unsupported_conic_gradient_reports_specific_diagnostic();
        unsupported_radial_gradient_reports_specific_diagnostic();
        matches_simple_compound_selectors();
        builds_cssom_metadata();
        cascade_uses_specificity_and_importance();
        matches_descendant_and_attribute_selectors();
        matches_sibling_selectors();
        matches_dynamic_pseudo_classes();
        reports_interaction_invalidation_hints_from_selectors();
        matches_is_where_with_specificity();
        controls_have_usable_default_boxes();
        embedded_styles_and_common_lengths_apply();
        linked_stylesheets_merge_into_author_css();
        deep_author_css_collection_is_iterative();
        author_css_collection_respects_aggregate_resource_limits();
        html5_semantic_defaults_are_visible();
        border_none_removes_default_control_border();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        grid_and_aspect_ratio_properties_apply();
#endif
        physical_edge_longhands_apply_per_side();
        font_weight_list_style_and_generated_counter_apply();
        text_transform_parses_and_inherits();
        font_family_declares_runtime_family_hash_and_inherits();
        after_generated_content_and_text_overflow_apply();
        per_corner_border_radius_applies();
        overflow_y_uses_the_vertical_scroll_subset();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        fixed_two_column_grid_template_applies();
        repeated_fixed_grid_template_applies();
        modern_length_functions_and_flex_wrap_apply();
        flex_sizing_properties_apply();
        flex_direction_column_applies();
        align_self_applies();
        align_content_applies();
#endif
        positioned_offsets_apply();
        logical_properties_and_hsl_apply();
        supports_queries_accept_logical_properties_and_hsl();
        invalid_hsl_preserves_prior_fallback();
        style_candidate_cache_preserves_selector_context();
        style_candidate_cache_respects_tiny_budget_and_inline_style();
        style_candidate_cache_ignores_irrelevant_identifiers();
        parser_limits_unbounded_css_fields_without_losing_following_rules();
        nonfinite_and_out_of_range_numeric_values_preserve_safe_fallbacks();
        text_overflow_is_specified_but_not_inherited_by_nested_elements();
    } catch (const std::exception& error) {
        std::cerr << "css parser test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "css parser tests passed\n";
    return 0;
}
