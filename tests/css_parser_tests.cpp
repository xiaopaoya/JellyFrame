#include "core/css_parser.h"
#include "core/dom.h"
#include "core/style.h"

#include <iostream>
#include <stdexcept>
#include <string>

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
    } catch (const std::exception& error) {
        std::cerr << "css parser test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "css parser tests passed\n";
    return 0;
}
