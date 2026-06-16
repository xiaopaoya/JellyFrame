#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/dom.h"
#include "core/form_control.h"
#include "core/html_parser.h"
#include "core/style.h"

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

void supports_queries_flatten_safe_declaration_subset() {
    const Stylesheet stylesheet = parse(
        "@supports (display: grid) { .grid { display: grid; } }"
        "@supports ((display: flex) and (gap: 8px)) { .flex { display: flex; gap: 8px; } }"
        "@supports not (color: oklch(50% 0.2 30)) { .fallback { color: #123456; } }"
        "@supports ((display: grid) or (unknown-prop: 1px)) { .either { display: block; } }"
        "@supports ((display: grid) and (gap: 8px) or (color: red)) { .mixed { color: red; } }"
        "@supports selector(:has(*)) { .has { color: red; } }");

    check(stylesheet.size() == 4, "supported @supports subset flattens matching safe blocks");
    check(stylesheet[0].selector == ".grid", "display grid supports selector");
    check(stylesheet[1].selector == ".flex", "and supports selector");
    check(stylesheet[2].selector == ".fallback", "not unsupported supports selector");
    check(stylesheet[3].selector == ".either", "or supports selector");
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
        ".story img { width: 240px; height: 120px; }"
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
        "grid-auto-rows: minmax(140px, auto); gap: 1.2rem; }"
        ".wide { grid-column: span 2; grid-row: span 3; }"
        ".media { aspect-ratio: auto 1.5 / 1; }"));

    const Style grid_style = resolver.resolve(*grid);
    const Style card_style = resolver.resolve(*card);
    const Style media_style = resolver.resolve(*media);

    check(grid_style.display == Display::Grid, "grid display parsed");
    check(grid_style.grid_min_track_width == 220, "grid min track parsed");
    check(grid_style.grid_auto_row_min == 140, "grid auto row min parsed");
    check(grid_style.column_gap == 19 && grid_style.row_gap == 19, "rem gap parsed");
    check(card_style.grid_column_span == 2 && card_style.grid_row_span == 3, "grid span parsed");
    check(media_style.aspect_ratio_width == 1500 && media_style.aspect_ratio_height == 1000,
          "aspect ratio parsed");
}

void physical_edge_longhands_apply_per_side() {
    auto element = make_element("section");
    element->attributes["id"] = "panel";
    element->attributes["class"] = "card";

    StyleResolver resolver(parse(
        "#panel { margin-top: 18px; border-bottom-width: 5px; }"
        ".card { margin: 4px; padding: 2px; border: 1px solid #111111; }"
        ".card { margin-left: auto; padding-left: 12px; border-left-width: 3px; }"));

    const Style style = resolver.resolve(*element);
    check(style.margin.top == 18, "higher-specificity margin-top survives shorthand");
    check(style.margin.right == 4, "margin shorthand right applies");
    check(style.margin_left_auto, "margin-left auto applies");
    check(style.padding.top == 2 && style.padding.left == 12, "padding longhand applies");
    check(style.border_width.top == 1 && style.border_width.left == 3, "border width longhand applies");
    check(style.border_width.bottom == 5, "higher-specificity border-bottom-width survives shorthand");
    check(style.border_color.r == 0x11, "border shorthand color applies");
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

    check(first_style.color.b == 0xeb, "cached candidates keep descendant match");
    check(second_style.color.r == 0x11 && second_style.color.b == 0x11,
          "cached candidates do not leak ancestor match");
    check(first_style_again.color.b == 0xeb, "repeated resolve keeps descendant style");
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

    check(primary_style.color.b == 0xeb, "tiny style cache resolves first class");
    check(danger_style.color.r == 0xff && danger_style.color.g == 0, "inline style survives tiny cache");
    check(primary_style_again.color.b == 0xeb, "tiny style cache rebuilds evicted class");
}

} // namespace

int main() {
    try {
        parses_comments_strings_and_functions();
        splits_selector_lists();
        skips_enhancement_blocks_without_corrupting_following_rules();
        supports_queries_flatten_safe_declaration_subset();
        flattens_layers_and_plain_media();
        conditional_media_queries_respect_viewport();
        preserves_declaration_fallback_order();
        resolves_simple_css_custom_properties();
        matches_simple_compound_selectors();
        builds_cssom_metadata();
        cascade_uses_specificity_and_importance();
        matches_descendant_and_attribute_selectors();
        matches_sibling_selectors();
        matches_dynamic_pseudo_classes();
        matches_is_where_with_specificity();
        controls_have_usable_default_boxes();
        embedded_styles_and_common_lengths_apply();
        linked_stylesheets_merge_into_author_css();
        html5_semantic_defaults_are_visible();
        border_none_removes_default_control_border();
        grid_and_aspect_ratio_properties_apply();
        physical_edge_longhands_apply_per_side();
        font_weight_list_style_and_generated_counter_apply();
        fixed_two_column_grid_template_applies();
        repeated_fixed_grid_template_applies();
        modern_length_functions_and_flex_wrap_apply();
        style_candidate_cache_preserves_selector_context();
        style_candidate_cache_respects_tiny_budget_and_inline_style();
    } catch (const std::exception& error) {
        std::cerr << "css parser test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "css parser tests passed\n";
    return 0;
}
