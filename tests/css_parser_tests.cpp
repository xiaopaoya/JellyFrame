#include "core/css_parser.h"
#include "core/document_style.h"
#include "core/dom.h"
#include "core/html_parser.h"
#include "core/style.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace wearweb;

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

    check(stylesheet.size() == 2, "unsupported group rules skipped");
    check(stylesheet[0].selector == ".base", "base selector");
    check(stylesheet[1].selector == ".after", "following selector");
}

void flattens_layers_and_plain_media() {
    const Stylesheet stylesheet = parse(
        "@layer components { .button { color: red; } }"
        "@media screen { .screen { color: blue; } }");

    check(stylesheet.size() == 2, "layer and plain media flattened");
    check(stylesheet[0].selector == ".button", "layer selector");
    check(stylesheet[1].selector == ".screen", "screen media selector");
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

} // namespace

int main() {
    try {
        parses_comments_strings_and_functions();
        splits_selector_lists();
        skips_enhancement_blocks_without_corrupting_following_rules();
        flattens_layers_and_plain_media();
        preserves_declaration_fallback_order();
        matches_simple_compound_selectors();
        builds_cssom_metadata();
        cascade_uses_specificity_and_importance();
        matches_descendant_and_attribute_selectors();
        controls_have_usable_default_boxes();
        embedded_styles_and_common_lengths_apply();
        linked_stylesheets_merge_into_author_css();
        html5_semantic_defaults_are_visible();
        border_none_removes_default_control_border();
        grid_and_aspect_ratio_properties_apply();
        physical_edge_longhands_apply_per_side();
    } catch (const std::exception& error) {
        std::cerr << "css parser test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "css parser tests passed\n";
    return 0;
}
